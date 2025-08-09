@echo off
setlocal

PUSHD "C:\Battlefield 2"
:: +modPath "mods/bf2all64"
"%~dp0withdll32.exe" /d:"%~dp0redirect32.dll" bf2_w32ded.exe +config "@HOME@/ServerConfigs/_serverSettings.con" +mapList "@HOME@/ServerConfigs/_maplist.con"
endlocal
