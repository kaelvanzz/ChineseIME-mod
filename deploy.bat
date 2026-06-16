@echo on
copy .\build\libs\chineseime-1.0.0.jar "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods"
copy .\natives\Release\chineseime_native.dll "C:\Users\user\AppData\Roaming\PrismLauncher\instances\1.21.4 fabric\minecraft\mods"
@echo off
echo ----------------------------------------------------
REM set "targetJAR=build\libs\chineseime-1.0.0.jar"
REM set "targetDLL=natives\Release\chineseime_native.dll"
REM or %%A in ("%targetJAR%") do set "modDate=%%~tA"
REM or %%A in ("%targetDLL%") do set "modDate=%%~tA"
REM cho The file was last modified on: %modDate%
forfiles /P ".\build\libs" /M "chineseime-1.0.0.jar" /C "cmd /c echo @file modified on @fdate @ftime"
forfiles /P ".\natives\Release" /M "*.dll" /C "cmd /c echo @file modified on @fdate @ftime"
pause