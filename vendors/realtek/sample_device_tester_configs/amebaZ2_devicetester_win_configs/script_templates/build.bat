@echo off

set sourcePath=C:\workspace\amazon-freertos
set iarbuild="C:\Program Files (x86)\IAR Systems\Embedded Workbench 8.1\common\bin\IarBuild.exe"
set opensslpath="C:\Program Files\OpenSSL-Win64\bin\openssl.exe"
set keypath="%sourcePath%\projects\realtek\amebaZ2\IAR\aws_demos\ecdsa-sha256-signer.key.pem"
set outsha256="%sourcePath%\projects\realtek\amebaZ2\IAR\aws_demos\Debug\Exe\firmware_is_sig.bin"
set image2="%sourcePath%\projects\realtek\amebaZ2\IAR\aws_demos\Debug\Exe\firmware_is.bin"
set outsignature="%sourcePath%\projects\realtek\amebaZ2\IAR\aws_demos\Debug\Exe\IDT-OTA-Signature"
set tooldir="%sourcePath%\vendors\realtek\sdk\amebaD\component\soc\realtek\amebad\misc\iar_utility\common\tools"
set currentDir=%CD%

IF %1==1 (
    SET project_type=aws_tests
) ELSE (
    SET project_type=aws_demos
)

cd %sourcePath%\projects\realtek\amebaZ2\IAR\aws_demos
for /f "delims=" %%i in ('cmd /c "%tooldir%\grep BUILD %sourcePath%\demos\include\aws_application_version.h | %tooldir%\gawk -F " " '{print $NF}'"') do set BUILD=%%i

IF %1==0 (
	if %BUILD%==2 (wsl cp amebaz2_firmware_is_2.json amebaz2_firmware_is.json)
	if %BUILD%==3 (wsl cp amebaz2_firmware_is_3.json amebaz2_firmware_is.json)
	if %BUILD%==4 (wsl cp amebaz2_firmware_is_4.json amebaz2_firmware_is.json)
	if %BUILD%==5 (wsl cp amebaz2_firmware_is_5.json amebaz2_firmware_is.json)
)

%iarbuild% %sourcePath%\projects\realtek\amebaZ2\IAR\%project_type%\application_is.ewp -make amazon_freertos -log errors -parallel 4

ping 127.0.0.1 -n 3 -w 1000 1> null

IF %1==0 (
	cd %sourcePath%\projects\realtek\amebaZ2\IAR\aws_demos
	wsl python python_custom_ecdsa_Z2.py

    wsl openssl dgst -sha256 -sign ecdsa-sha256-signer.key.pem -out ./Debug/Exe/firmware_is_temp.bin ./Debug/Exe/firmware_is_pad.bin
    wsl openssl base64 -A -in ./Debug/Exe/firmware_is_temp.bin -out ./Debug/Exe/IDT-OTA-Signature

    cd %currentDir%
)

ping 127.0.0.1 -n 1 -w 1000 1> null
