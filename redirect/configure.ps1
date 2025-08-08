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
  $dumpbinOutput = cmd.exe /c "`"$VsDevCmd`" -no_logo && dumpbin.exe /IMPORTS `"$exePath`"" | Out-String
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

  Write-Host "Detecting executable architecture ..."
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

function Get-VSCommandCmd {
  if (Get-Command "VsDevCmd.bat" -ErrorAction SilentlyContinue) {
    # VsDevCmd.bat is already in the path
    return "VsDevCmd.bat"
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
  $vsDevCmdPath = Join-Path $visualStudioPath "Common7\Tools\VsDevCmd.bat"
  if (!(Test-Path $vsDevCmdPath)) {
    Write-Error "Invalid or no Visual Studio installation found"
    Write-Error "Please execute this script using the Visual Studio Command Line: Tools > Command Line > Developer PowerShell"
    return
  }

  Write-Host "Using Visual Studio Installation in: $visualStudioPath"
  return $vsDevCmdPath
}

function Create-DllTargetProps {
  param(
    [string]$TargetName,
    [string]$LibName
  )

    $linkSection = ""
    if ($LibName) {
        $linkSection = @"
  
    <Link>
      <AdditionalDependencies>$LibName.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
"@
    }

    $xml = @"
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ImportGroup Label="PropertySheets" />
  <PropertyGroup>
    <TARGET_DLL_NAME>$TargetName</TARGET_DLL_NAME>
  </PropertyGroup>
  <ItemDefinitionGroup>$linkSection
    <ClCompile>
      <PreprocessorDefinitions>GAMESPY_GAMENAME="battlefield2";`$(PreprocessorDefinitions);</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
"@

  Set-Content -Path "dlltarget.props" -Value $xml -Encoding utf8
}

function Create-DefAndLib {
  param (
    [string]$VsDevCmd,
    [string]$DllPath,
    [string]$LibName,
    [string]$DefName
  )

  Write-Host "> Creating $DefName ..."
  $dumpbinOutput = cmd.exe /c "`"$VsDevCmd`" -no_logo && dumpbin.exe /EXPORTS `"$DllPath`"" | Out-String
  $defContent = @()
  $defContent += "LIBRARY $LibName"
  $defContent += "EXPORTS"
  foreach ($line in $dumpbinOutput -split "`n") {
    if ($line -match "^\s*(\d+)\s+[0-9A-F]+\s+[0-9A-F]+\s+(.+?)\r?$" -or $line -match "^\s*(\d+)\s+[0-9A-F]+\s+([^\s]+)\s+\(forwarded to ([^)]+)\)") {
      $ordinal = $Matches[1]
      $symbol = $Matches[2]
      $defContent += "    $($symbol) @$($ordinal)"
    }
  }

  $defContent = $defContent -join "`r`n"
  Set-Content -Path "$DefName" -Value $defContent
  Write-Host "> Created $DefName"

  cmd.exe /c "`"$VsDevCmd`" -no_logo && lib /NOLOGO /DEF:$DefName /OUT:$LibName.lib /MACHINE:x86"
  Write-Host "> Created $LibName.lib"
}

function Get-GameSpyExecutable {
  $ofd = New-Object System.Windows.Forms.OpenFileDialog
  $ofd.Filter = "Executables (*.exe)|*.exe|All files (*.*)|*.*"
  $ofd.Title = "Select an executable file"

  if ($ofd.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
      Write-Host "No file selected, exiting."
      return ""
  }

  return $ofd.FileName
}

function main {
  param (
    [string]$exePath
  )

  $ErrorActionPreference = "Stop"
  $vsDevCmd = Get-VSCommandCmd
  if (!$vsDevCmd) {
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

  Write-Host "Building dll ..."
  $arch = ""
  if ($selection -eq 0) {
    $redirectLibDef = @"
LIBRARY redirect
EXPORTS
    DetourFinishHelperProcess @1
"@

    Create-DllTargetProps -TargetName "redirect"
    Set-Content -Path "exports32.def" -Value $redirectLibDef
    Set-Content -Path "exports64.def" -Value $redirectLibDef
  }
  else {
    $arch = Get-ExecArch -VsDevCmd $vsDevCmd -ExePath $exePath
    $targetName = $dllOptions[$selection - 1]

    $libName = $targetName
    $targetPath = Join-Path $exeDir "$targetName.dll"
    if (Test-Path -Path $targetPath) {
      $libName += "_original"
    }

    Create-DllTargetProps -TargetName $targetName -LibName $libName
    foreach ($dir in $env:PATH -split ';') {
      if (-not [string]::IsNullOrWhiteSpace($dir)) {
        $candidate = Join-Path $dir "$targetName.dll"
        if (Test-Path $candidate) {
          $sourceDll = $candidate
          break
        }
      }
    }

    if (-not $sourceDll) {
      Write-Host "Cannot create $targetName.dll because it was not found on the system"
      return
    }

    if ($arch -eq "x86") {
      Create-DefAndLib -VsDevCmd $vsDevCmd -DllPath $sourceDll -LibName $libName -DefName "exports32.def"
    } else {
      Create-DefAndLib -VsDevCmd $vsDevCmd -DllPath $sourceDll -LibName $libName -DefName "exports64.def"
    }
  }

  Write-Host "Generating $targetName.dll ..."
  if (-not $arch -or $arch -eq "x86") {
    cmd.exe /c "cd .. && `"$vsDevCmd`" -no_logo && msbuild gamespy.sln /t:redirect /p:Configuration=Release /p:Platform=x86"
  }

  if (-not $arch -or $arch -eq "x64") {
    cmd.exe /c "cd .. && `"$vsDevCmd`" -no_logo && msbuild gamespy.sln /t:redirect /p:Configuration=Release /p:Platform=x64"
  }

  if ($targetPath) {
    Write-Host "Build complete. The $targetName.dll is now useable."
    if (Test-Path -Path $targetPath) {
      Write-Host "$targetName.dll needs to be renamed to $targetName`_original.dll before the new dll can be used."
      $rename = Read-Host "Should this be done automatically? (y/n)"
      if ($rename -match '^[Yy]') {
        Rename-Item -Path $targetPath -NewPath (Join-Path $exeDir "$targetName`_original.dll")
      } else {
        Write-Host "Please manually rename the file."
        return
      }
    }

    $copy = Read-Host "Do you want the $targetName.dll to be copied? (y/n)"
    if ($copy -match '^[Yy]') {
      if ($arch -eq "x86") {
        Copy-Item -Path "../Release/$targetName.dll" -Destination $targetPath
      }
      else {
        Copy-Item -Path "../x64/Release/$targetName.dll" -Destination $targetPath
      }
    }
  }
}

main -exePath $executable
