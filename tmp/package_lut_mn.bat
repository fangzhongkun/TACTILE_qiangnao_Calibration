@echo off
chcp 65001 >nul
cd /d "%~dp0..\build-point-display"
set LOG=%~dp0package_lut_mn.log
echo [%date% %time%] package start > "%LOG%"
"D:\Program Files\CMake\bin\cpack.exe" -G ZIP -C Release -B "%~dp0package_lut_mn" >> "%LOG%" 2>&1
echo ZIP_EXIT=%ERRORLEVEL% >> "%LOG%"
"D:\Program Files\CMake\bin\cpack.exe" -G NSIS -C Release -B "%~dp0package_lut_mn" >> "%LOG%" 2>&1
echo NSIS_EXIT=%ERRORLEVEL% >> "%LOG%"
echo [%date% %time%] package end >> "%LOG%"
