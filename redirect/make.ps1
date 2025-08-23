param (
  [string]$executable
)

Add-Type -AssemblyName System.Windows.Forms

function Get-ImportsFromPE {
  param(
    [string]$VsDevCmd,
    [string]$exePath
  )

  Write-Host "Scanning dll imports ..."
  $dumpbinOutput = cmd.exe /c "`"$VsDevCmd`" -no_logo && dumpbin /IMPORTS `"$exePath`"" | Out-String
  $imports = @()
  foreach ($line in $dumpbinOutput -split "`n") {
    if ($line -match "^\s{4}(.*)\.dll\r?$") {
      $imports += $Matches[1].ToLower()
    }
  }

  return $imports
}

function Get-ExecArch {
  param(
    [string]$VsDevCmd,
    [string]$ExePath
  )

  $dumpbinOutput = cmd.exe /c "`"$VsDevCmd`" -no_logo && dumpbin.exe /HEADERS `"$ExePath`"" | Out-String
  foreach ($line in $dumpbinOutput -split "`n") {
    if ($line -match "machine\s*\((.+)\)") {
      return $Matches[1].Trim().toLower()
    }
  }
  
  return "unknown"
}

function Get-KnownDlls {
    $knownDlls = @()
    $regPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs"
    if (Test-Path $regPath) {
        $knownDlls = Get-ItemProperty -Path $regPath | Get-Member -MemberType NoteProperty | ForEach-Object { $_.Name.ToLower() }
    }

    return $knownDlls
}

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

function Create-Exports {
  param (
    [string]$VsDevCmd,
    [string]$DllPath
  )

  Write-Host "> Creating exports.h ..."
  $dumpbinOutput = cmd.exe /c "`"$VsDevCmd`" -no_logo && dumpbin.exe /EXPORTS `"$DllPath`"" | Out-String
  $exports = @()
  $privateExports = @("DllCanUnloadNow", "DllGetClassObject", "DllInstall", "DllRegisterServer", "DllUnregisterServer")
  foreach ($line in $dumpbinOutput -split "`n") {
    if ($line -match "^\s*(\d+)\s+[0-9A-F]+\s+[0-9A-F]+\s+(.+?)\r?$" -or $line -match "^\s*(\d+)\s+[0-9A-F]+\s+([^\s]+)\s+\(forwarded to ([^)]+)\)") {
      $ordinal = $Matches[1]
      $symbol = $Matches[2]
      $export = "#pragma comment(linker, `"/EXPORT:$symbol=`" DLL_FORWARD_PATH `".$symbol"
      if ($privateExports -contains $symbol) {
        $export += ",PRIVATE`")"
      } else {
        $export += ",@$ordinal`")"
      }

      $exports += $export
    }
  }

  $exports = $exports -join "`r`n"
  Set-Content -Path "exports.h" -Value $exports
  Write-Host "> Created exports.h"
}

function Get-GameSpyExecutable {
  $ofd = New-Object System.Windows.Forms.OpenFileDialog
  $ofd.Filter = "Executables (*.exe)|*.exe|All files (*.*)|*.*"
  $ofd.Title = "Select an executable file"

  Write-Host "Please select a Game for which you want a redirect dll"
  if ($ofd.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
      Write-Host "No file selected, exiting."
      return ""
  }

  return $ofd.FileName
}

function Build-Dll {
  param (
    [string]$VsDevCmd,
    [string]$VsInstallDir,
    [string]$Arch,
    [string]$TargetName,
    [string]$AdditionalTargets,
    [string]$DllForwardPath,
    [string]$GameName
  )

  if (-not $GameName) {
    $GameName = "gmtest"
  }

  if ($DllForwardPath) {
    # replace single backslashes with double backslashes (c/c++ escaping now applies!)
    $DllForwardPath = $DllForwardPath -replace "\\", "\\"
  }

  $libs = "Ws2_32.lib User32.lib Shell32.lib detours.lib"
  $libDir = "..\dependencies\detours\lib.$Arch"
  $includeDir = "..\dependencies\detours\include"
  $buildCmd = "cl /LD /Ox /EHsc /nologo /std:c++latest /I`"$includeDir`" /D GAMESPY_GAMENAME=\`"$GameName\`" /D DLL_FORWARD_PATH=\`"$DllForwardPath\`" /Fe$TargetName.dll main.cpp $AdditionalTargets $libs /link /LIBPATH:`"$libDir`""

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

  Remove-Item *.obj, *.lib, *.exp, exports.def, exports.h -Force -ErrorAction SilentlyContinue
}

function main {
  param (
    [string]$exePath
  )

  if ($exePath -eq "clean") {
    Remove-Item *.obj, *.lib, *.exp, *.dll, exports.def, exports.h -Force -ErrorAction SilentlyContinue
    return
  }

  Write-Host "Welcome to the GameSpy redirector builder"
  Write-Host
  Write-Host "Using this tool, you will be able to create dlls which redirect calls from the now dead GameSpy endpoint"
  Write-Host "to configurable or autodetected (if the emulator is in your local network) endpoint."
  Write-Host
  Write-Host "This works by placing a creating a custom .dll file which is loaded by the game and which"
  Write-Host "intercepts all lookups to *.gamespy.com, returning the new endpoint."
  Write-Host

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
  
  if (-Not $exePath) {
    $exePath = Get-GameSpyExecutable
    if (-Not $exePath) { return }
    else { Write-Host "Selected executable: $exePath" }
  }

  $knownDlls = Get-KnownDlls
  $dllOptions = $nonKnownImports = Get-ImportsFromPE -VsDevCmd $vsDevCmd -exePath $exePath | Where-Object { $knownDlls -notcontains $_ }
  
  Write-Host "[0] <create redirect.dll>"
  $exeDir = Split-Path $exePath
  for ($i=0; $i -lt $dllOptions.Count; $i++) {
    $dllPath = Join-Path $exeDir "$($dllOptions[$i]).dll"
    if (Test-Path $dllPath) {
      Write-Host "[$($i + 1)] $($dllOptions[$i]) [rename required]"
    } else { 
      Write-Host "[$($i + 1)] $($dllOptions[$i])"
    }
  }

  do {
    $selection = Read-Host "Select DLL by number"
    $valid = ($selection -as [int]) -ge 0 -and ($selection -as [int]) -lt ($dllOptions.Count + 1)
  } until ($valid)

  Write-Host "Preparing dll ..."
  if ($selection -eq 0) {
    $targetName = "redirect"
    Set-Content -Path "exports.h" -Value "" -NoNewline
    Set-Content -Path "exports.def" -Value @"
LIBRARY redirect32
EXPORTS
    DetourFinishHelperProcess @1
"@
    Build-Dll -VsDevCmd $vsDevCmd -VsInstallDir $vsInstallDir -Arch "x86" -TargetName "redirect32" -AdditionalTargets "exports.def"

    Set-Content -Path "exports.h" -Value "" -NoNewline
    Set-Content -Path "exports.def" -Value @"
LIBRARY redirect64
EXPORTS
    DetourFinishHelperProcess @1
"@
    Build-Dll -VsDevCmd $vsDevCmd -VsInstallDir $vsInstallDir -Arch "x64" -TargetName "redirect64" -AdditionalTargets "exports.def"
    Write-Host
    Write-Host "redirect dlls built - please use withdll.exe to inject the dll into games"
    Write-Host
    return
  }
  
  $targetName = $dllOptions[$selection - 1]
  $targetPath = Join-Path $exeDir "$targetName.dll"
  if (Test-Path -Path $targetPath) {
    # the selected dll is present in the executable direct, in which case it needs to be moved
    $forwardDllPath = "$($targetName)_original.dll"
    $exportDllPath = $targetPath

    Write-Host "Creating security backup ..."
    Copy-Item -Path $targetPath -Destination $forwardDllPath
  } else {
    $forwardDllPath = "C:\Windows\System32\$targetName.dll"
    if (-not (Test-Path $forwardDllPath)) {
      Write-Host "Warning: $targetName.dll does not exist in C:\Windows\System32 - trying %PATH%"
      Write-Host "The resulting dll might only work on this machine!"
      $exeArch = Get-ExecArch -VsDevCmd $vsDevCmd -ExePath $exePath

      foreach ($dir in $env:PATH -split ';') {
        if (-not [string]::IsNullOrWhiteSpace($dir)) {
          $candidate = Join-Path $dir "$targetName.dll"
          if (Test-Path $candidate) {
            $dllArch = Get-ExecArch -VsDevCmd $vsDevCmd -ExePath $candidate
            if ($dllArch -eq $exeArch) {
              Write-Host "Found $targetName.dll in $dir"
              $forwardDllPath = $candidate
              break
            }
          }
        }
      }
    }

    $exportDllPath = $forwardDllPath
  }

  if (-not $forwardDllPath) {
    Write-Host "Cannot create $targetName.dll because it was not found on the system"
    return
  }

  Create-Exports -VsDevCmd $vsDevCmd -DllPath $exportDllPath

  if (-not $exeArch) {
    $exeArch = Get-ExecArch -VsDevCmd $vsDevCmd -ExePath $exePath
  }

  Write-Host "Generating $targetName.dll for $exeArch ..."
  Build-Dll -VsDevCmd $vsDevCmd -VsInstallDir $vsInstallDir -Arch $exeArch -TargetName $targetName -DllForwardPath $forwardDllPath
  Write-Host "Build complete. The $targetName.dll is now useable."

  if (Test-Path -Path $targetPath) {
    Write-Host "$targetName.dll needs to be renamed to $targetName`_original.dll before the new dll can be used."
    $rename = Read-Host "Do you want this to happen now? (y/n)"
    if ($rename -match '^[Yy]') {
      # in this case the $forwardDllPath doesn't contains only the relative filename
      Rename-Item -Path $targetPath -NewName $forwardDllPath
    } else {
      Write-Host "Please manually rename the file."
      return
    }
  }

  $copy = Read-Host "Do you want the $targetName.dll to be copied to the game's directory? (y/n)"
  if ($copy -match '^[Yy]') {
    if ($arch -eq "x86") {
      Copy-Item -Path "$targetName.dll" -Destination $targetPath
    }
    else {
      Copy-Item -Path "$targetName.dll" -Destination $targetPath
    }
  }
}

main -exePath $executable
