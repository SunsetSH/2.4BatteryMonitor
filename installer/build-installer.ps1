# Builds dist\2.4BatteryMonitor-Setup-<version>.exe, dist\2.4BatteryMonitor-<version>.exe (what portable copies
# update from) and dist\2.4BatteryMonitor-<version>-portable.zip.
# Needs: 2.4BatteryMonitor.exe already built (make / build.bat) and Inno Setup 6 or 7 installed.
param([string]$Version = "1.0.0")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
& (Join-Path $root 'scripts\verify-windows-version.ps1') -ExpectedVersion $Version

if (-not (Test-Path (Join-Path $root "2.4BatteryMonitor.exe"))) { throw "Build 2.4BatteryMonitor.exe first (make or build.bat)." }

$iscc = foreach ($v in 7, 6) {
    foreach ($base in ${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs") { "$base\Inno Setup $v\ISCC.exe" }
}
$iscc = $iscc | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
if (-not $iscc) { throw "Inno Setup not found." }
& $iscc "/DAppVersion=$Version" (Join-Path $PSScriptRoot "2.4BatteryMonitor.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed." }

# Package the app and its license in a temporary directory.
$stage = Join-Path $root ("build\portable-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Copy-Item 2.4BatteryMonitor.exe $stage
    Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
    Compress-Archive -Path "$stage\*" -DestinationPath "dist\2.4BatteryMonitor-$Version-portable.zip" -Force
} finally {
    $stagePath = (Resolve-Path -LiteralPath $stage).Path
    $buildPath = (Resolve-Path -LiteralPath (Join-Path $root 'build')).Path
    if ([IO.Path]::GetDirectoryName($stagePath) -eq $buildPath) {
        Remove-Item -LiteralPath $stagePath -Recurse -Force
    }
}

Copy-Item 2.4BatteryMonitor.exe "dist\2.4BatteryMonitor-$Version.exe" -Force

$sums = foreach ($name in "2.4BatteryMonitor-Setup-$Version.exe", "2.4BatteryMonitor-$Version.exe", "2.4BatteryMonitor-$Version-portable.zip") {
    "$((Get-FileHash (Join-Path 'dist' $name) -Algorithm SHA256).Hash.ToLower())  $name"
}
[IO.File]::WriteAllText((Join-Path $root 'dist\SHA256SUMS.txt'), (($sums -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
Write-Host "Done: dist\2.4BatteryMonitor-Setup-$Version.exe, dist\2.4BatteryMonitor-$Version.exe, dist\2.4BatteryMonitor-$Version-portable.zip and dist\SHA256SUMS.txt"
