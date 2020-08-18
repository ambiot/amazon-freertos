from OpenSSL import crypto
from socket import gethostname
import os
import array as arr
import subprocess

Major = 0
Minor = 0
Build = 0
with open('../../../../../demos/include/aws_application_version.h') as f:
    for line in f:
        if line.find('APP_VERSION_MAJOR') != -1:
            x = line.split()
            Major = int(x[2])
        if line.find('APP_VERSION_MINOR') != -1:
            x = line.split()
            Minor = int(x[2])
        if line.find('APP_VERSION_BUILD') != -1:
            x = line.split()
            Build = int(x[2])

print('Major:' + str(Major))
print('Minor:' + str(Minor))
print('Build:' + str(Build))

#version = 0xffffffff
version = Major*1000000 + Minor*1000 + Build
version_byte = version.to_bytes(4,'little')

headernum = 0x00000001
headernum_byte = headernum.to_bytes(4,'little')

signature = 0x3141544f
signature_byte = signature.to_bytes(4,'little')

headerlen = 0x00000018
headerlen_byte = headerlen.to_bytes(4,'little')

checksum = 0;
with open("./Debug/Exe/km4_image/km0_km4_image2.bin", "rb") as f:
    byte = f.read(1)
    num = int.from_bytes(byte, 'big')
    checksum += num
    while byte != b"":
        byte = f.read(1)
        num = int.from_bytes(byte, 'big')
        checksum += num
checksum_byte = checksum.to_bytes(4,'little')

imagelen = os.path.getsize("./Debug/Exe/km4_image/km0_km4_image2.bin")
imagelen_bytes = imagelen.to_bytes(4, 'little')

offset = 0x00000020
offset_bytes = offset.to_bytes(4, 'little')

rvsd = 0x0800b000
rvsd_bytes = rvsd.to_bytes(4, 'little')

img2_bin = open('./Debug/Exe/km4_image/km0_km4_image2.bin', 'br').read()

f = open("./Debug/Exe/km4_image/OTA_ALL.bin", 'wb')
f.write(version_byte)
f.write(headernum_byte)
f.write(signature_byte)
f.write(headerlen_byte)
f.write(checksum_byte)
f.write(imagelen_bytes)
f.write(offset_bytes)
f.write(rvsd_bytes)
f.write(img2_bin)
f.close()

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
ota1_bin = open('./Debug/Exe/km4_image/km0_km4_image2.bin', 'br').read()
# sign and verify PASS
ota1_sig = crypto.sign(priv_key, ota1_bin, 'sha256')
crypto.verify(ss_cert, ota1_sig, ota1_bin, 'sha256')
ota1_sig_size = len(ota1_sig)
#print(ota1_sig_size)

#opening the ota_all.bin and getting the number of padding bytes
f = open("./Debug/Exe/km4_image/OTA_All.bin", 'rb')
ota_all_buff = f.read()
f.close()
ota_all_size = os.path.getsize("./Debug/Exe/km4_image/OTA_All.bin")
#print(ota_all_size)
ota_padding = 1024-(ota_all_size%1024)
#print(int(ota_padding))

#padding 0's to make the last block of OTA equal to an integral multiple of block size
x = bytes([0] * ota_padding)

#append the 0 padding bytes followed by the 2 individual signatures of ota1 and ota2 along with their signature sizes
f = open("./Debug/Exe/km4_image/OTA_ALL_sig.bin", 'wb')
f.write(ota_all_buff)
f.write(x)
f.write(ota1_sig)
f.write(bytes([ota1_sig_size]))
f.close()

print('done')

#Debug info in case you want to check the actual signature binaries generated separately
'''
sf = open("ota1.sig", 'wb')
sf.write(ota1_sig)
sf.close()

sf = open("ota2.sig", 'wb')
sf.write(ota2_sig)
sf.close()
'''