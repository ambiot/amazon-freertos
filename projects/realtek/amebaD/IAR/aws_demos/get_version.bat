
set RootDir="%CD%"
set tooldir="%RootDir%\..\..\..\..\..\vendors\realtek\sdk\amebaD\component\soc\realtek\amebad\misc\iar_utility\common\tools"

for /f "delims=" %%i in ('cmd /c "%tooldir%\grep MAJOR %RootDir%\..\..\..\..\..\demos\include\aws_application_version.h | %tooldir%\gawk -F " " '{print $NF}'"') do set MAJOR=%%i
for /f "delims=" %%i in ('cmd /c "%tooldir%\grep MINOR %RootDir%\..\..\..\..\..\demos\include\aws_application_version.h | %tooldir%\gawk -F " " '{print $NF}'"') do set MINOR=%%i
for /f "delims=" %%i in ('cmd /c "%tooldir%\grep BUILD %RootDir%\..\..\..\..\..\demos\include\aws_application_version.h | %tooldir%\gawk -F " " '{print $NF}'"') do set BUILD=%%i

echo %MAJOR% > tmp_MAJOR.txt
echo %MINOR% >> tmp_MINOR.txt
echo %BUILD% >> tmp_BUILD.txt

