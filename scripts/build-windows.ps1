# Fatboard – build everything on your own Windows PC and install the VST3s.
# Needs: Visual Studio 2022 (workload "Desktop development with C++"), CMake, Git.
# Run in PowerShell from the repo folder:   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
# First run takes 15-30 minutes (it downloads and compiles JUCE); later runs only rebuild what changed.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
git pull --ff-only
cmake -B build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build build --config Release --target Scrub_All ScrubPro_All Harun_All Smash_All Sog_All Samt_All Funke_All Dig_All Tap_All --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$dest = "$env:CommonProgramFiles\VST3"
$plugins = "Scrub","ScrubPro","Harun","Smash","Sog","Samt","Funke","Dig","Tap"
try {
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  foreach ($t in $plugins) {
    Get-ChildItem "build\${t}_artefacts\Release\VST3" -Filter *.vst3 | ForEach-Object {
      $target = Join-Path $dest $_.Name
      if (Test-Path $target) { Remove-Item -Recurse -Force $target }
      Copy-Item -Recurse $_.FullName $target
      Write-Host "installed $($_.Name)"
    }
  }
  Write-Host "`nDone. Close FL Studio, open it again and rescan plugins (Dig is an instrument: Channel Rack)."
} catch {
  Write-Host "`nBuilt, but could not copy into $dest (needs admin, or FL Studio has a plugin open)."
  Write-Host "Close FL Studio, open PowerShell as administrator and run this script again."
  Write-Host "The built plugins are in build\<Name>_artefacts\Release\VST3"
}
