::@echo off

set sourcePath_ori=C:/workspace/amazon-freertos
set sourcePath=%sourcePath_ori:/=\%

SET project_type=%1

set FlashToolDir=%sourcePath%\vendors\realtek\tools
set ImagePath=%sourcePath%\projects\realtek\amebaZ2\IAR\%project_type%\Debug\Exe

set COMPORT=COM19

:repeat
ping 127.0.0.1 -n 1 -w 300 1> null
(%FlashToolDir%\uartfwburn.exe -p %COMPORT% -b 3000000 -f %ImagePath%\flash_is.bin -v -r -m 0x1000 0x3000 -d z2 | find "success") || goto :repeat
echo Success
