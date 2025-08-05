@echo off
setlocal

PUSHD "C:\Battlefield 2"
"%~dp0withdll32.exe" /d:"%~dp0redirect32.dll" bf2_w32ded.exe
endlocal
pause
