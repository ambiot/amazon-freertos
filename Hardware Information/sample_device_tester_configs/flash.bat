set IARDir="E:\Program Files (x86)\IAR Systems\Embedded Workbench 8.1"
set IARProjectDir=%1%\tests\realtek\amebaz2\iar
set PorjectName=application_is


set RootDir=%CD%
cd %IARProjectDir%
%IARDir%\common\bin\cspybat --leave_target_running --timeout 500 %IARDir%\arm\bin\armproc.dll %IARDir%\arm\bin\armjlink2.dll %IARProjectDir%\Debug\Exe\application_is.out --plugin %IARDir%\arm\bin\armbat.dll --macro %IARProjectDir%\debug.mac --flash_loader %IARProjectDir%\..\..\..\..\lib\third_party\mcu_vendor\realtek\component\soc\realtek\8710c\misc\iar_utility\flashloader\FlashRTL8710c.board --backend --endian=little --cpu=Cortex-M33 --cmse --fpu=none -p %IARProjectDir%\..\..\..\..\lib\third_party\mcu_vendor\realtek\component\soc\realtek\8710c\misc\iar_utility\8710c.ddf --semihosting=none --drv_communication=USB0 --drv_interface_speed=500 --jlink_reset_strategy=0,0 --drv_interface=SWD --drv_catch_exceptions=0x000 --drv_swo_clock_setup=25000000,1,2000000
cd %RootDir%


