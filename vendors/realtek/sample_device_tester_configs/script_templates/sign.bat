::@echo off

set ProjectRoot="C:/workspace/amazon-freertos/vendors/realtek/boards/amebaD/aws_demos"
set RootDir=%CD%

cd %ProjectRoot%

wsl openssl dgst -sha256 -sign ecdsa-sha256-signer.key.pem -out ./Debug/Exe/km4_image/km0_km4_image2_sig.bin ./Debug/Exe/km4_image/km0_km4_image2.bin
wsl openssl base64 -A -in ./Debug/Exe/km4_image/km0_km4_image2_sig.bin -out ./Debug/Exe/km4_image/IDT-OTA-Signature

ping 127.0.0.1 -n 3 -w 1000

cd %RootDir%

echo "sign done"