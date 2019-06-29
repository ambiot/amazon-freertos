#include "sensor_service.h"
#include "sensor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "platform_stdlib.h"
#define GRAY_MODE_LUX_L_BOUND (5)
#define GRAY_MODE_LUX_H_BOUND (20)

#define GRAY_MODE_AL3042_LUX_L_BOUND (10)
#define GRAY_MODE_AL3042_LUX_H_BOUND (100)

#if LIGHT_SENSOR_USE==LIGHT_SENSOR_AP1522D

void sensor_thread(void *param)
{
    float ir_brightness = 0;
    int gray_mode = 0;
    int lux;

    // 1. initialize sensors

    ir_ctrl_init(NULL);

    ir_cut_init(NULL);

    ambient_light_sensor_init(NULL);

    ambient_light_sensor_power(1);

    while(1) {

        // 2. get lux
        lux = ambient_light_sensor_get_lux(50);

        if ( !gray_mode && (lux <= GRAY_MODE_LUX_L_BOUND) )
        {
            // 3. lux turns too low, turn on gray mode

            // 3.1 enable gray mode
            gray_mode = 1;
            sensor_external_set_gray_mode(gray_mode);

            // 3.2 Slowly turn on IR LED
            for (ir_brightness = 0.05f; ir_brightness <=1.0f; ir_brightness += 0.05f) {
                ir_ctrl_set_brightness(ir_brightness);
                vTaskDelay(50);
            }

            // 3.3 disable IR cut
            ir_cut_enable(0);

            printf("lux:%d gray_mode:%d\r\n", lux, gray_mode);
        }
        else if ( gray_mode && (lux > GRAY_MODE_LUX_H_BOUND))
        {
            // 4. lux turns normal, turn off gray mode

            // 4.1 enable IR cut
            ir_cut_enable(1);

            // 4.2 turn of IR LED
            ir_brightness = 0;
            ir_ctrl_set_brightness(ir_brightness);

            // 4.3 disable gray mode
            gray_mode = 0;
            sensor_external_set_gray_mode(gray_mode);

            printf("lux:%d gray_mode:%d\r\n", lux, gray_mode);
        }
        else if ( gray_mode )
        {

        }
    }
}

#elif LIGHT_SENSOR_USE==LIGHT_SENSOR_AL3042
void sensor_thread(void *param)
{
    printf("[Start]SENSOR_AL3042 sensor_thread\n\r");
    float ir_brightness = 0;
    int gray_mode = 0;
    int lux= 0;

    // 1. initialize sensors

    ir_ctrl_init(NULL);

    ir_cut_init(NULL);
  
    printf("[before]al3042_light_sensor_init\n\r");
    al3042_light_sensor_init(NULL);

    while(1) {

        // 2. get lux
        lux = al3042_get_ALS();

        printf("lux:%d\r\n", lux);
        vTaskDelay(1000);
        
        if ( !gray_mode && (lux <= GRAY_MODE_AL3042_LUX_L_BOUND) )
        {
            // 3. lux turns too low, turn on gray mode

            // 3.1 enable gray mode
            gray_mode = 1;
            sensor_external_set_gray_mode(gray_mode);

            // 3.2 Slowly turn on IR LED
            for (ir_brightness = 0.05f; ir_brightness <=1.0f; ir_brightness += 0.05f) {
                ir_ctrl_set_brightness(ir_brightness);
                vTaskDelay(50);
            }

            // 3.3 disable IR cut
            ir_cut_enable(0);

            printf("lux:%d gray_mode:%d\r\n", lux, gray_mode);
        }
        else if ( gray_mode && (lux > GRAY_MODE_AL3042_LUX_H_BOUND))
        {
            // 4. lux turns normal, turn off gray mode

            // 4.1 enable IR cut
            ir_cut_enable(1);

            // 4.2 turn of IR LED
            ir_brightness = 0;
            ir_ctrl_set_brightness(ir_brightness);

            // 4.3 disable gray mode
            gray_mode = 0;
            sensor_external_set_gray_mode(gray_mode);

            printf("lux:%d gray_mode:%d\r\n", lux, gray_mode);
        }
        else if ( gray_mode )
        {

        }
    }
}
#else
#define NO_LIGHT_SENSOR
#endif

/**
 * These weak/virtual function implementations are default behaviors if there is no sensor board exist
 * The actual implementation depends on different evaluation board.
 */

__weak int ambient_light_sensor_init(void *param) 
{
    return 0;
}

__weak int al3042_light_sensor_init(void *param)
{
    return 0;
}

__weak int ambient_light_sensor_power(int enable)
{
    return 0;
}

__weak int ambient_light_sensor_get_lux(int sensibility)
{
    return 100;
}

__weak int ir_cut_init(void *param)
{
    return 0;
}

__weak int ir_cut_enable(int enable)
{
    return 0;
}

__weak int ir_ctrl_init(void *param)
{
    return 0;
}

__weak int ir_ctrl_set_brightness(float brightness)
{
    return 0;
}

__weak int sensor_external_set_gray_mode(int enable)
{
    return 0;
}

__weak int sensor_external_loop()
{
    vTaskDelay(1000);
    return 0;
}

void init_sensor_service(void)
{
#if !defined(NO_LIGHT_SENSOR)
    if(xTaskCreate(sensor_thread, ((const char*)"sensor_thread"), 384, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        printf("\r\n sensor_thread: Create Task Error\n");
    }
#endif
}