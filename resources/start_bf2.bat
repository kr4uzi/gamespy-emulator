@echo off
setlocal
if defined ProgramFiles(x86) (
    set "REGKEY=HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Electronic Arts\EA GAMES\Battlefield 2"
) else (
    set "REGKEY=HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\EA Games\Battlefield 2"
)

for /f "tokens=2* skip=2" %%a in ('reg query "%REGKEY%" /v "InstallDir"') do set "BF2Dir=%%b"
if not defined BF2Dir (
    echo Battlefield 2 directory not found in registry.
    pause
    exit /b 1
)

PUSHD "%BF2Dir%"
"%~dp0withdll.exe" /d:"%~dp0redirector.dll" BF2.exe +menu 1 +fullscreen 0 +restart +szx 800 +szy 600 +gamespy 127.0.0.1
endlocal
pause
