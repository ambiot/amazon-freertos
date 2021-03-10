::@echo off

set sourcePath="C:\workspace\amazon-freertos"
set iarbuild="C:\Program Files (x86)\IAR Systems\Embedded Workbench 8.1\common\bin\IarBuild.exe"

set opensslpath="C:\Program Files\OpenSSL-Win64\bin\openssl.exe"
set keypath="%sourcePath%\projects\realtek\amebaD\IAR\aws_demos\ecdsa-sha256-signer.key.pem"
set outsha256="%sourcePath%\projects\realtek\amebaD\IAR\aws_demos\Debug\Exe\km4_image\km0_km4_image2_sig.bin"
set image2="%sourcePath%\projects\realtek\amebaD\IAR\aws_demos\Debug\Exe\km4_image\km0_km4_image2.bin"
set outsignature="%sourcePath%\projects\realtek\amebaD\IAR\aws_demos\Debug\Exe\km4_image\IDT-OTA-Signature"

IF %1==1 (
    SET project_type=aws_tests
) ELSE (
    SET project_type=aws_demos
)

set currentDir=%CD%

%iarbuild% %sourcePath%\projects\realtek\amebaD\IAR\%project_type%\km0_bootloader.ewp -make Debug -log errors -parallel 4

ping 127.0.0.1 -n 8 -w 1000

%iarbuild% %sourcePath%\projects\realtek\amebaD\IAR\%project_type%\km0_application.ewp -make Debug -log errors -parallel 4

ping 127.0.0.1 -n 8 -w 1000

%iarbuild% %sourcePath%\projects\realtek\amebaD\IAR\%project_type%\km4_bootloader.ewp -make is -log errors -parallel 4

ping 127.0.0.1 -n 8 -w 1000
%iarbuild% %sourcePath%\projects\realtek\amebaD\IAR\%project_type%\km4_application.ewp -clean is -log errors 

ping 127.0.0.1 -n 8 -w 1000
%iarbuild% %sourcePath%\projects\realtek\amebaD\IAR\%project_type%\km4_application.ewp -make is -log errors -parallel 4

ping 127.0.0.1 -n 15 -w 1000

IF %1==0 (
    cd %sourcePath%\projects\realtek\amebaD\IAR\aws_demos
    wsl python3 python_custom_ecdsa_D.py
    ping 127.0.0.1 -n 5 -w 1000

    wsl openssl dgst -sha256 -sign ecdsa-sha256-signer.key.pem -out ./Debug/Exe/km4_image/km0_km4_image2_sig.bin ./Debug/Exe/km4_image/km0_km4_image2.bin
    wsl openssl base64 -A -in ./Debug/Exe/km4_image/km0_km4_image2_sig.bin -out ./Debug/Exe/km4_image/IDT-OTA-Signature

    ping 127.0.0.1 -n 3 -w 1000

    cd %currentDir%
)

ping 127.0.0.1 -n 1 -w 1000