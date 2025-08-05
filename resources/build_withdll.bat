@echo off
setlocal enabledelayedexpansion
pushd ..

echo initializing/updating dependencies
git submodule update --init --recursive

echo building withdll.exe
if not defined DevEnvDir (
  echo setting up MSVC build environment ...

  if defined ProgramFiles(x86^) (
    set "VCWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    set ARCH=amd64
  ) else (
    set "VCWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    set ARCH=x86
  )

  if not exist "!VCWHERE!" (
    echo could not detect Visual Studio installation
    goto :error
  )

  for /f "usebackq tokens=*" %%i in (`"!VCWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
  )

  call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" !ARCH! >nul
)

:build
pushd dependencies\detours\src
nmake >nul 2>&1 || goto :error
popd

pushd dependencies\detours\samples\syelog
nmake >nul 2>&1 || goto :error
popd

pushd dependencies\detours\samples\withdll
nmake >nul 2>&1 || goto :error
popd

echo copying for %ARCH%
if %ARCH%==amd64 (
  copy "dependencies\detours\bin.X64\withdll.exe" "%~dp0withdll64.exe"
  set ARCH=x86
  call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" !ARCH! >nul
  goto :build
) else (
  copy "dependencies\detours\bin.X86\withdll.exe" "%~dp0withdll32.exe"
)

popd
goto :eof

:error
echo.
echo *** ERROR OCCURRED ***
exit /b 1
