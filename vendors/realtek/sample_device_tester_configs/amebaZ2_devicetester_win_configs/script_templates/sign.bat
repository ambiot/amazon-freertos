::@echo off

set ProjectRoot="C:/workspace/amazon-freertos/projects/realtek/amebaZ2/IAR/aws_demos"
set RootDir=%CD%

cd %ProjectRoot%

wsl openssl dgst -sha256 -sign ecdsa-sha256-signer.key.pem -out ./Debug/Exe/firmware_is_temp.bin ./Debug/Exe/firmware_is_pad.bin
wsl openssl base64 -A -in ./Debug/Exe/firmware_is_temp.bin -out ./Debug/Exe/IDT-OTA-Signature

ping 127.0.0.1 -n 3 -w 1000 1> null

cd %RootDir%

echo "sign done"