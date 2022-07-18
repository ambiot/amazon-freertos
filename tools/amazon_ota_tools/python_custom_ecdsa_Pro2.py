from OpenSSL import crypto
from socket import gethostname
import os
import base64

#Reading the Private key generated using openssl(should be generated using ECDSA P256 curve)
f = open("ecdsa-sha256-signer.key.pem")
pv_buf = f.read()
f.close()
priv_key = crypto.load_privatekey(crypto.FILETYPE_PEM, pv_buf)

#Reading the certificate generated using openssl(should be generated using ECDSA P256 curve)
f = open("ecdsa-sha256-signer.crt.pem")
ss_buf = f.read()
f.close()
ss_cert = crypto.load_certificate(crypto.FILETYPE_PEM, ss_buf)

#Reading OTA1 binary and individually signing it using the ECDSA P256 curve
firmware_path = '../../../../GCC-RELEASE/build/ota.bin'
f = open(firmware_path, 'br')
ota1_bin = f.read()
ota1_bin_size = os.path.getsize(firmware_path)
f.close()

#Read ota firmware content without checksum
f = open(firmware_path, 'br')
ota_fw = f.read()[:-4]
print("ota_fw:", len(ota_fw))
f.close()

# sign and verify the ota firmware
ota1_sig = crypto.sign(priv_key, ota_fw, 'sha256')
crypto.verify(ss_cert, ota1_sig, ota_fw, 'sha256')
ota1_sig_size = len(ota1_sig)
#print(ota1_sig_size)

#Output signature to AWS-IoT OTA-Signature
ota_sig1_base64 = base64.b64encode(ota1_sig)
print (ota_sig1_base64)
f = open("IDT-OTA-Signature", 'w', encoding='utf-8')
f.write(ota_sig1_base64.decode('utf-8'))
f.close()