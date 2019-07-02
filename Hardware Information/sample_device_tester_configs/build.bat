set IARDir="E:\Program Files (x86)\IAR Systems\Embedded Workbench 8.1"
set IARProjectDir=%1%\tests\realtek\amebaz2\iar
set PorjectName=application_is


#%IARDir%\common\bin\IarBuild.exe %IARProjectDir%\%PorjectName%.ewp -clean Debug
%IARDir%\common\bin\IarBuild.exe %IARProjectDir%\%PorjectName%.ewp -make Debug