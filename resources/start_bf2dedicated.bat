@echo off
setlocal

PUSHD "C:\Battlefield 2 Server Untouched"
"%~dp0withdll.exe" /d:"%~dp0server-launcher.dll" bf2_w32ded.exe +config stdbots.con
endlocal
pause
