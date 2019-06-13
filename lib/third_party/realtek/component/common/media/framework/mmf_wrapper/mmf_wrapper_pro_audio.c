#include "mmf_wrapper/mmf_wrapper_pro_audio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include "mmf_common.h"

#include "audio_api.h"

#define SAFE_FREE(x) if ((x) != NULL) { free(x); x=NULL; }

#if ENABLE_SPEEX_AEC
#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"
#endif

#if ENABLE_AAC_ENC
#include "faac.h"
#include "faac_api.h"
#endif

#if ENABLE_AAC_DEC
#include "aacdec.h"
#endif

/*
 * reference_count keeps the total module count that use same hardware interface.
 * 0:   No module reference to this interface, and we should close it
 * >0:  One or more mudles reference to this interface, and this interface should be in active
 */
static uint8_t reference_count = 0;

static uint8_t first_init = 1;

static audio_t audio_obj;

static uint8_t dma_txdata[AUDIO_DMA_PAGE_SIZE*AUDIO_DMA_PAGE_NUM]__attribute__ ((aligned (0x20))); 
static uint8_t dma_rxdata[AUDIO_DMA_PAGE_SIZE*AUDIO_DMA_PAGE_NUM]__attribute__ ((aligned (0x20))); 

static uint8_t last_tx_buf[AUDIO_DMA_PAGE_SIZE];
static uint8_t last_rx_buf[AUDIO_DMA_PAGE_SIZE];

static xSemaphoreHandle audio_tx_done_sema = NULL;
static xSemaphoreHandle audio_rx_done_sema = NULL;

static int frame_source_type = FMT_A_PCMA;
static int frame_sink_type   = FMT_A_PCMA;

static uint16_t pcm_tx_cache_idx = 0;
static uint8_t pcm_tx_cache[AUDIO_DMA_PAGE_SIZE];

#define AUDIO_TX_PCM_QUEUE_LENGTH (20)
static xQueueHandle audio_tx_pcm_queue = NULL;

/* A buffer that cache PCM raw data for audio tx */
#define AUDIO_TX_ENCODED_QUEUE_LENGTH (20)
static xQueueHandle audio_tx_encoded_queue = NULL;

#define AUDIO_RX_ENCODED_QUEUE_LENGTH (20)
static xQueueHandle audio_rx_encoded_queue = NULL;

#if ENABLE_SPEEX_AEC
#define SPEEX_SAMPLE_RATE (8000)
#define NN (AUDIO_DMA_PAGE_SIZE/2)
#define TAIL_LENGTH_IN_MILISECONDS (20)
#define TAIL (TAIL_LENGTH_IN_MILISECONDS * (SPEEX_SAMPLE_RATE / 1000) )

static uint8_t e_buf[AUDIO_DMA_PAGE_SIZE];
#endif

#if ENABLE_AAC_ENC
static int samplerate;
static int channel;

static faacEncHandle hEncoder = NULL;
static int samplesInput;
static int maxBytesOutput;

static uint16_t aac_pcm_cache_idx = 0;
static uint8_t *aac_pcm_cache = NULL;
#endif

#if ENABLE_AAC_DEC
HAACDecoder	hAACDecoder;

static int aac_dec_channel = 1;
static int aac_dec_samplerate = 8000;
static int aac_dec_profile = 1;

static uint16_t aac_data_cache_len = 0;
static uint8_t aac_data_cache[4096]; // TODO: the size should equal to max(aac frame) * 2 + max(rtp payload)
static uint8_t aac_decode_buf[AAC_MAX_NCHANS * AAC_MAX_NSAMPS * sizeof(int16_t)];
#endif

typedef struct {
    int type;
    int len;
    uint8_t *data;
    uint32_t timestamp;
} audio_data_t;

static void audio_tx_complete(u32 arg, u8 *pbuf)
{
    uint8_t *ptx_buf;

    memcpy(last_tx_buf, pbuf, AUDIO_DMA_PAGE_SIZE);

    ptx_buf = (uint8_t *)audio_get_tx_page_adr(&audio_obj);
    if ( xQueueReceiveFromISR(audio_tx_pcm_queue, ptx_buf, NULL) != pdPASS ) {
        memset(ptx_buf, 0, AUDIO_DMA_PAGE_SIZE);
    }
    audio_set_tx_page(&audio_obj, (uint8_t *)ptx_buf);

    xSemaphoreGiveFromISR(audio_tx_done_sema, NULL);
}

static void audio_rx_complete(u32 arg, u8 *pbuf)
{
    memcpy(last_rx_buf, pbuf, AUDIO_DMA_PAGE_SIZE);
    xSemaphoreGiveFromISR(audio_rx_done_sema, NULL);
}

static void audio_rx_handle_thread(void *param)
{
    int16_t *pcm;

#if ENABLE_SPEEX_AEC
    SpeexEchoState *st;
    SpeexPreprocessState *den;
    int sampleRate = SPEEX_SAMPLE_RATE;

    st = speex_echo_state_init(NN, TAIL);
    den = speex_preprocess_state_init(NN, sampleRate);
    speex_echo_ctl(st, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_ECHO_STATE, st);
#endif

    while (1) {
        xSemaphoreTake(audio_rx_done_sema, portMAX_DELAY);

        pcm = (int16_t *)last_rx_buf;
#if ENABLE_SPEEX_AEC
        speex_echo_cancellation(st, (void *)(last_rx_buf), (void *)(last_tx_buf), (void *)e_buf);
        speex_preprocess_run(den, (void *)e_buf);
        pcm = (int16_t *)e_buf;
#endif

        if (frame_source_type == FMT_A_PCMU)
        {
            audio_data_t audio_data = { .type = FMT_A_PCMU, .len = G711_FSIZE, .data = NULL, .timestamp = xTaskGetTickCount() };
            audio_data.data = (uint8_t *) malloc(G711_FSIZE);
            for (int i = 0; i < G711_FSIZE; i++)
                audio_data.data[i] = encodeU(pcm[i]);
            xQueueSend(audio_rx_encoded_queue, &audio_data, portMAX_DELAY);
        }
        else if (frame_source_type == FMT_A_PCMA)
        {
            audio_data_t audio_data = { .type = FMT_A_PCMA, .len = G711_FSIZE, .data = NULL, .timestamp = xTaskGetTickCount() };
            audio_data.data = (uint8_t *) malloc(G711_FSIZE);
            for (int i = 0; i < G711_FSIZE; i++)
                audio_data.data[i] = encodeA(pcm[i]);
            xQueueSend(audio_rx_encoded_queue, &audio_data, portMAX_DELAY);
        }
#if ENABLE_AAC_ENC
        else if (frame_source_type == FMT_A_MP4A_LATM)
        {
            if (aac_pcm_cache != NULL) {
                memcpy(aac_pcm_cache + aac_pcm_cache_idx, pcm, AUDIO_DMA_PAGE_SIZE);
                aac_pcm_cache_idx += AUDIO_DMA_PAGE_SIZE;
                if (aac_pcm_cache_idx >= samplesInput*2) {
                    audio_data_t audio_data = { .type = FMT_A_MP4A_LATM, .len = 0, .data = NULL, .timestamp = xTaskGetTickCount() };
                    audio_data.data = (uint8_t *) malloc( maxBytesOutput );

                    audio_data.len = aac_encode_run(hEncoder, aac_pcm_cache, samplesInput, audio_data.data, maxBytesOutput);
                    aac_pcm_cache_idx -= samplesInput*2;
                    if (audio_data.len > 0) {
                        memmove(aac_pcm_cache, aac_pcm_cache + samplesInput*2, aac_pcm_cache_idx);
                        xQueueSend(audio_rx_encoded_queue, &audio_data, portMAX_DELAY);
                    } else {
                        free(audio_data.data);
                    }
                }
            }
        }
#endif
        else
        {
        }

        audio_set_rx_page(&audio_obj);
    }
}

static void audio_tx_handle_thread(void *param)
{
    int16_t *pcm;
    audio_data_t audio_data;

    while(1) {
        xQueueReceive(audio_tx_encoded_queue, &audio_data, portMAX_DELAY);

        if (audio_data.type == FMT_A_PCMU)
        {
            pcm = (int16_t *)pcm_tx_cache;
            for (int i=0; i<audio_data.len; i++) {
                pcm[pcm_tx_cache_idx/2] = decodeU( audio_data.data[i] );
                pcm_tx_cache_idx += 2;
                if (pcm_tx_cache_idx == AUDIO_DMA_PAGE_SIZE) {
                    if (xQueueSend(audio_tx_pcm_queue, pcm_tx_cache, 1000) != pdTRUE) {
                        printf("fail to send tx queue\r\n");
                    }
                    pcm_tx_cache_idx = 0;
                }
            }
            free(audio_data.data);
        }
        else if (audio_data.type == FMT_A_PCMA)
        {
            pcm = (int16_t *)pcm_tx_cache;
            for (int i=0; i<audio_data.len; i++) {
                pcm[pcm_tx_cache_idx/2] = decodeA( audio_data.data[i] );
                pcm_tx_cache_idx += 2;
                if (pcm_tx_cache_idx == AUDIO_DMA_PAGE_SIZE) {
                    if (xQueueSend(audio_tx_pcm_queue, pcm_tx_cache, 1000) != pdTRUE) {
                        printf("fail to send tx queue\r\n");
                    }
                    pcm_tx_cache_idx = 0;
                }
            }
            free(audio_data.data);
        }
#if ENABLE_AAC_DEC
        else if (audio_data.type == FMT_A_AAC_RAW)
        {
            int ret = 0;
            uint8_t *inbuf, *inbuf_backup;
            int bytesLeft, bytesLeft_backup;
            AACFrameInfo frameInfo;

            if (aac_data_cache_len + audio_data.len >= sizeof(aac_data_cache)) {
                // This should never happened
                printf("aac data cache overflow %d %d\r\n", aac_data_cache_len, audio_data.len);
                while(1);
            }

            // there is 4 byte timestamp in front of aac raw data, ignore it
            memcpy( aac_data_cache + aac_data_cache_len , audio_data.data + 4, audio_data.len - 4 );
            aac_data_cache_len += audio_data.len - 4;
            free(audio_data.data);

            inbuf = aac_data_cache;
            bytesLeft = aac_data_cache_len;

            // TODO: aac decode sometimes require 2nd frame to decode 1st frame. 
            // we should make sure decoder requires enough frame instead of feed enough bytes (>= 1024)
            while (ret == 0 && bytesLeft > 1024) {
                inbuf_backup = inbuf;
                bytesLeft_backup = bytesLeft;

                ret = AACDecode(hAACDecoder, &inbuf, &bytesLeft, (void *)aac_decode_buf);
                if (ret == 0) {
                    AACGetLastFrameInfo(hAACDecoder, &frameInfo);

                    // TODO: it might need resample if use different sample rate & channel number
                    for (int i=0; i<frameInfo.outputSamps * 2; i++) {
                        pcm_tx_cache[pcm_tx_cache_idx++] = aac_decode_buf[i];
                        if (pcm_tx_cache_idx == AUDIO_DMA_PAGE_SIZE) {
                            if (xQueueSend(audio_tx_pcm_queue, pcm_tx_cache, 1000) != pdTRUE) {
                                printf("fail to send tx queue\r\n");
                            }
                            pcm_tx_cache_idx = 0;
                        }
                    }
                } else {
                    if (ret == ERR_AAC_INDATA_UNDERFLOW) {
                        inbuf = inbuf_backup;
                        bytesLeft = bytesLeft_backup;
                    } else {
                        printf("decode err:%d\r\n", ret);
                    }
                    break;
                }
            }

            if (bytesLeft > 0) {
                memmove(aac_data_cache, aac_data_cache + aac_data_cache_len - bytesLeft, bytesLeft);
                aac_data_cache_len = bytesLeft;
            } else {
                aac_data_cache_len = 0;
            }
        }
#endif
        else
        {
        }
    }
}

void *audio_wrapper_open()
{
    // There might be 2 tasks doing module open at the same time, add critical section to avoid this
    taskENTER_CRITICAL();

    if (reference_count == 0) {
        if (first_init) {
            audio_tx_done_sema = xSemaphoreCreateBinary();
            audio_rx_done_sema = xSemaphoreCreateBinary();
            audio_tx_pcm_queue = xQueueCreate(AUDIO_TX_PCM_QUEUE_LENGTH, AUDIO_DMA_PAGE_SIZE);
            audio_tx_encoded_queue = xQueueCreate(AUDIO_TX_ENCODED_QUEUE_LENGTH, sizeof(audio_data_t));
            audio_rx_encoded_queue = xQueueCreate(AUDIO_RX_ENCODED_QUEUE_LENGTH, sizeof(audio_data_t));

        	if(xTaskCreate(audio_rx_handle_thread, ((const char*)"audio_rx_handle_thread"), 256, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        		printf("\r\n audio_rx_handle_thread: Create Task Error\n");
        	}

        	if(xTaskCreate(audio_tx_handle_thread, ((const char*)"audio_tx_handle_thread"), 256, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        		printf("\r\n audio_tx_handle_thread: Create Task Error\n");
        	}

            audio_init(&audio_obj, OUTPUT_SINGLE_EDNED, MIC_DIFFERENTIAL, AUDIO_CODEC_2p8V);
            audio_set_param(&audio_obj, ASR_8KHZ, WL_16BIT);
            audio_set_rx_dma_buffer(&audio_obj, dma_rxdata, AUDIO_DMA_PAGE_SIZE);    
            audio_set_tx_dma_buffer(&audio_obj, dma_txdata, AUDIO_DMA_PAGE_SIZE);   
            audio_mic_analog_gain(&audio_obj, 1, MIC_40DB);

            audio_tx_irq_handler(&audio_obj, (audio_irq_handler)audio_tx_complete, (uint32_t)&audio_obj);
            audio_rx_irq_handler(&audio_obj, (audio_irq_handler)audio_rx_complete, (uint32_t)&audio_obj);

            first_init = 0;
        } else {

        }
    }
    reference_count++;

    taskEXIT_CRITICAL();

    return &audio_obj;
}

void audio_wrapper_close()
{
    reference_count--;
    if (reference_count == 0) {
        audio_trx_stop(&audio_obj);
#if ENABLE_AAC_ENC
        SAFE_FREE(aac_pcm_cache);
#endif
    }
}

int audio_wrapper_set_param(void *ctx, int cmd, int arg)
{
    int ret = 0;
    switch(cmd) {
		case CMD_AUDIO_TX_ENABLE:
			printf("audio_tx_start\n\r");
			audio_tx_start(&audio_obj);
			break;
		case CMD_AUDIO_RX_ENABLE:
			printf("audio_rx_start\n\r");
			audio_rx_start(&audio_obj);
			break;
        case CMD_AUDIO_SET_SOURCE_FRAMETYPE:
            if(arg == FMT_A_PCMU) 
                frame_source_type = FMT_A_PCMU;
            else if(arg == FMT_A_PCMA)
                frame_source_type = FMT_A_PCMA;
#if ENABLE_AAC_ENC
            else if(arg == FMT_A_MP4A_LATM)
                frame_source_type = FMT_A_MP4A_LATM;
#endif
            else
                frame_source_type = FMT_A_PCMA; //default
            break;
        case CMD_AUDIO_SET_SINK_FRAMETYPE:
            if (arg == FMT_A_PCMU)
                frame_sink_type = FMT_A_PCMU;
            else if (arg == FMT_A_PCMA)
                frame_sink_type = FMT_A_PCMA;
#if ENABLE_AAC_DEC
            else if (arg == FMT_A_AAC_RAW)
            {
                frame_sink_type = FMT_A_AAC_RAW;
                hAACDecoder = AACInitDecoder();
            }
#endif
            else
                frame_sink_type = FMT_A_PCMA;
            break;
#if ENABLE_AAC_ENC
        case CMD_AUDIO_AACEN_SET_SAMPLERATE:
            samplerate = arg;
            break;
        case CMD_AUDIO_AACEN_SET_CHANNEL:
            channel = arg;
            break;
        case CMD_AUDIO_AACEN_SET_APPLY:
            aac_encode_init(&hEncoder, 16, samplerate, channel, &samplesInput, &maxBytesOutput);
            aac_pcm_cache = (uint8_t *) malloc (samplesInput * 2 + AUDIO_DMA_PAGE_SIZE);
            break;
#endif
#if ENABLE_AAC_DEC
        case CMD_AUDIO_AACDE_SET_SAMPLERATE:
            aac_dec_samplerate = arg;
            break;
        case CMD_AUDIO_AACDE_SET_CHANNEL:
            aac_dec_channel = arg;
            break;
        case CMD_AUDIO_AACDE_SET_APPLY:
        {
            AACFrameInfo frameInfo;
            frameInfo.nChans = aac_dec_channel;
            frameInfo.sampRateCore = aac_dec_samplerate;
            frameInfo.profile = 1; // 1 for LC
            AACSetRawBlockParams(hAACDecoder, 0, &frameInfo);
            break;
        }
#endif		
        default:
            ret = -EINVAL;
            break;
    }
    return ret;
}

int audio_wrapper_sink_mod_handle(void *ctx, void *b)
{
    int i;
    int16_t *pcm;
    audio_data_t audio_data;

    exch_buf_t *exbuf = (exch_buf_t*)b;

	if(exbuf->state != STAT_READY) {
		return -EAGAIN;
    }

    audio_data.type = frame_sink_type;
    audio_data.len = exbuf->len;
    audio_data.data = (uint8_t *) malloc( audio_data.len );
    memcpy( audio_data.data, exbuf->data, audio_data.len);
    xQueueSend(audio_tx_encoded_queue, &audio_data, portMAX_DELAY);

    exbuf->state = STAT_USED;

    return 0;
}

int audio_wrapper_source_mod_handle(void *ctx, void *b)
{
    audio_data_t audio_data;
    exch_buf_t *exbuf = (exch_buf_t*)b;

    if(exbuf->state == STAT_USED) {
        SAFE_FREE(exbuf->data);
    }

	if(exbuf->state != STAT_READY) {
        xQueueReceive(audio_rx_encoded_queue, &audio_data, portMAX_DELAY);

        exbuf->index = 0;
        exbuf->data = audio_data.data;
        exbuf->len = audio_data.len;
        exbuf->timestamp = audio_data.timestamp;
		exbuf->codec_fmt = audio_data.type;
        exbuf->state = STAT_READY;

        mmf_source_add_exbuf_sending_list_all(exbuf);
    }
    return 0;
}
void audio_send_to_tx_encoded_queue(int type, uint8_t *data, int len) {
	audio_data_t audio_data;
	audio_data.type = type;
	audio_data.len = len;
	audio_data.data = (uint8_t *) malloc( audio_data.len );
	memcpy( audio_data.data, data, audio_data.len);
	xQueueSend(audio_tx_encoded_queue, &audio_data, portMAX_DELAY);
}