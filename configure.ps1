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

function Build-Detours {
  param (
    [string]$VsDevCmd,
    [string]$VsInstallDir,
    [string]$Arch
  )

  if ($Arch -eq "x86") {
    $vcEnv = Join-Path $VsInstallDir "VC\Auxiliary\Build\vcvars32.bat"
    & cmd.exe /c "`"$VsDevCmd`" -no_logo && `"$vcEnv`" && cd dependencies/detours/src && nmake"
  }
  elseif ($Arch -eq "x64") {
    $vcEnv = Join-Path $vsInstallDir "VC\Auxiliary\Build\vcvars64.bat"
    & cmd.exe /c "`"$VsDevCmd`" -no_logo && `"$vcEnv`" && cd dependencies/detours/src && nmake"
  }
  else {
    Write-Error "Unknowwn Arch: $Arch"
  }
}

function main {
  $ErrorActionPreference = "Stop"

  Write-Host "initializing/updating dependencies"
  & git submodule update --init --recursive

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

  Write-Host "building detours.lib"
  Build-Detours -VsDevCmd $vsDevCmd -VsInstallDir $vsInstallDir -Arch "x86"
  Build-Detours -VsDevCmd $vsDevCmd -VsInstallDir $vsInstallDir -Arch "x64"
}

main
