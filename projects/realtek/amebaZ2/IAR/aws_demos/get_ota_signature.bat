::#!/bin/bash

::Use the sign.sh script if you select custom code signing for OTA tests.

::openssl dgst -sha256 -sign C:/<absolute-path-to>/<privare-key-file> -out C:/<absolute-path-to>/<signature-destination> %1
::openssl base64 -A -in C:/<absolute-path-to>/<signature-destination> -out %2
set RootDir=%CD%

set exepath="C:\Program Files\OpenSSL-Win64\bin\openssl.exe"
set key="%RootDir%\ecdsa-sha256-signer.key.pem"
set sha256out="%RootDir%\Debug\Exe\firmware_is_sha256.bin"
set image2="%RootDir%\Debug\Exe\firmware_is_pad.bin"
set outSig="%RootDir%\OTA-Signature"

%exepath% dgst -sha256 -sign %key% -out %sha256out% %image2%
%exepath% base64 -A -in %sha256out% -out %outSig%

::pause