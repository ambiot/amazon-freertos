::@echo off

set sourcePath_ori=C:/workspace/amazon-freertos
set sourcePath=%sourcePath_ori:/=\%

SET project_type=%1

set FlashToolDir=C:\workspace\amazon-freertos\vendors\realtek\tools\1-10_MP_Image_Tool_Release
set ImagePath_km0=%sourcePath%\projects\realtek\amebaD\IAR\%project_type%\Debug\Exe\km0_image
set ImagePath_km4=%sourcePath%\projects\realtek\amebaD\IAR\%project_type%\Debug\Exe\km4_image
set RootDir=%CD%
set COMPORT=COM4

::Delay 3 seconds
ping 127.0.0.1 -n 3 -w 1000

cd %FlashToolDir%

%FlashToolDir%\1-10_MP_Image_Tool.exe -scan device
%FlashToolDir%\1-10_MP_Image_Tool.exe -add device %COMPORT%
%FlashToolDir%\1-10_MP_Image_Tool.exe -set chip amebad
%FlashToolDir%\1-10_MP_Image_Tool.exe -set skipsys true
%FlashToolDir%\1-10_MP_Image_Tool.exe -set verify true
%FlashToolDir%\1-10_MP_Image_Tool.exe -set baudrate 1500000
%FlashToolDir%\1-10_MP_Image_Tool.exe -set boot false

:: km0 bootloader
%FlashToolDir%\1-10_MP_Image_Tool.exe -set image %ImagePath_km0%\km0_boot_all.bin
%FlashToolDir%\1-10_MP_Image_Tool.exe -erase 0x08000000 4 %COMPORT%
%FlashToolDir%\1-10_MP_Image_Tool.exe -set address 0x08000000
%FlashToolDir%\1-10_MP_Image_Tool.exe -set length 0x0
%FlashToolDir%\1-10_MP_Image_Tool.exe -download %COMPORT%

ping 127.0.0.1 -n 5 -w 1000

:: km4_bootloader
%FlashToolDir%\1-10_MP_Image_Tool.exe -set image %ImagePath_km4%\km4_boot_all.bin
%FlashToolDir%\1-10_MP_Image_Tool.exe -erase 0x08004000 8 %COMPORT%
%FlashToolDir%\1-10_MP_Image_Tool.exe -set address 0x08004000
%FlashToolDir%\1-10_MP_Image_Tool.exe -set length 0x0
%FlashToolDir%\1-10_MP_Image_Tool.exe -download %COMPORT%

ping 127.0.0.1 -n 5 -w 1000

if "%project_type%"=="aws_demos" (
    %FlashToolDir%\1-10_MP_Image_Tool.exe -erase 0x08101000 4 %COMPORT%
    ping 127.0.0.1 -n 3 -w 1000
)

:: FW1
::%FlashToolDir%\1-10_MP_Image_Tool.exe -set boot true
::%FlashToolDir%\1-10_MP_Image_Tool.exe -set image %ImagePath_km4%\km0_km4_image2.bin
::%FlashToolDir%\1-10_MP_Image_Tool.exe -erase 0x08006000 778 %COMPORT%
::%FlashToolDir%\1-10_MP_Image_Tool.exe -set address 0x08006000
::%FlashToolDir%\1-10_MP_Image_Tool.exe -set length 0x0
::%FlashToolDir%\1-10_MP_Image_Tool.exe -download %COMPORT%

:: FW2
%FlashToolDir%\1-10_MP_Image_Tool.exe -set boot true
%FlashToolDir%\1-10_MP_Image_Tool.exe -set image %ImagePath_km4%\km0_km4_image2.bin
%FlashToolDir%\1-10_MP_Image_Tool.exe -erase 0x08106000 778 %COMPORT%
%FlashToolDir%\1-10_MP_Image_Tool.exe -set address 0x08106000
%FlashToolDir%\1-10_MP_Image_Tool.exe -set length 0x0
%FlashToolDir%\1-10_MP_Image_Tool.exe -download %COMPORT%

cd %RootDir%

::Delay 1 seconds
ping 127.0.0.1 -n 1 -w 1000
