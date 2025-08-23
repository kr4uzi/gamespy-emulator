param (
  [string]$target
)

function Get-VSInstallDir {
  if (${env:VSINSTALLDIR}) {
    return ${env:VSINSTALLDIR}
  }

  Write-Host "Detecting Visual Studio installation ..."
  $programFilesX86 = ${env:ProgramFiles(x86)}
  if (-not $programFilesX86) {
    $programFilesX86 = ${env:ProgramFiles}
  }

  $vsWherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-Not (Test-Path $vsWherePath)) {
    Write-Host "vswhere.exe not found at $vsWherePath"
    return
  }

  $visualStudioPath = & $vsWherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  return $visualStudioPath  
}
function Build-Dll {
  param (
    [string]$VsInstallDir,
    [string]$Arch,
    [string]$OutName,
    [string]$Target
  )

  Set-Content -Path "exports.def" -Value @"
LIBRARY $OutName
EXPORTS
    DetourFinishHelperProcess @1
"@

  $libs = "detours.lib"
  $libDir = "..\dependencies\detours\lib.$Arch"
  $includeDir = "..\dependencies\detours\include"
  $buildCmd = "cl /LD /EHsc /nologo /std:c++latest /I`"..\dependencies\detours\include`" /I`"..\dependencies\GameSpy\src`" /Fe$OutName.dll $Target $libs /link /LIBPATH:`"$libDir`""

  Write-Host "Compiling ..."
  if ($Arch -eq "x86") {
    $vcEnv = Join-Path $VsInstallDir "VC\Auxiliary\Build\vcvars32.bat"
    & cmd.exe /c "`"$vcEnv`" 1>nul && $buildCmd"
  }
  elseif ($Arch -eq "x64") {
    $vcEnv = Join-Path $vsInstallDir "VC\Auxiliary\Build\vcvars64.bat"
    & cmd.exe /c "`"$vcEnv`" 1>nul && $buildCmd"
  }
  else {
    Write-Error "Unknowwn Arch: $Arch"
    return
  }

  if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
  }

  Remove-Item *.obj, *.lib, *.exp, *.def -Force -ErrorAction SilentlyContinue
}

function main {
  param (
    [string]$Target
  )

  if ($Target -eq "clean") {
    Remove-Item *.obj, *.lib, *.exp, *.def, *.dll -Force -ErrorAction SilentlyContinue
    return
  }
  elseif ($Target) {
    $file = Get-Item $Target
    Build-Dll -VsInstallDir $vsInstallDir -Arch "x86" -OutName $file.BaseName -Target "$($file.Name) exports.def"
    return
  }

  $ErrorActionPreference = "Stop"
  $vsInstallDir = Get-VSInstallDir
  if (!$vsInstallDir) {
    return
  }

  $vsDevCmd = Join-Path $vsInstallDir "Common7\Tools\VsDevCmd.bat"
  if (-not (Test-Path $vsDevCmd)) {
    Write-Error "Invalid or no Visual Studio installation found"
    Write-Error "Please execute this script using the Visual Studio Command Line: Tools > Command Line > Developer PowerShell"
    return
  }
  
  $files = Get-ChildItem *.cpp
  foreach ($file in $files) {
    Build-Dll -VsInstallDir $vsInstallDir -Arch "x86" -OutName $file.BaseName -Target "$($file.Name) exports.def"
  }
}

main -Target $target
