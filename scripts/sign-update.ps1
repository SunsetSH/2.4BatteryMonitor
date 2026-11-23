param(
    [switch]$InitializeKey,
    [string]$Version
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$repo = (Get-Content -Raw -LiteralPath (Join-Path $root 'installer\update-repository.txt')).Trim()
$keyName = '2.4BatteryMonitor Release Signing'
$publicPath = Join-Path $root 'installer\update-public-key.txt'
if ($InitializeKey) {
    if (Test-Path -LiteralPath $publicPath) { throw 'A public release key is already configured.' }
    if (-not [Security.Cryptography.CngKey]::Exists($keyName)) {
        $created = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, $keyName)
        $created.Dispose()
    }
} elseif ($Version -cnotmatch '^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$') {
    throw 'Pass -InitializeKey or -Version MAJOR.MINOR.PATCH.'
}

$key = [Security.Cryptography.CngKey]::Open($keyName)
try {
    $public = [BitConverter]::ToString($key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)).Replace('-', '')
    if ($InitializeKey) {
        [IO.File]::WriteAllText($publicPath, $public + "`n", [Text.Encoding]::ASCII)
        Write-Output "Public key written to $publicPath"
        return
    }
    if ([IO.File]::ReadAllText($publicPath).Trim() -cne $public) { throw 'The local signing key differs from the configured public key.' }
    # The installer updates installed copies, the bare executable portable ones (src/update_core.h names both).
    $sha = [Security.Cryptography.SHA256]::Create()
    $ecdsa = [Security.Cryptography.ECDsaCng]::new($key)
    try {
        foreach ($name in "2.4BatteryMonitor-Setup-$Version.exe", "2.4BatteryMonitor-$Version.exe") {
            $path = Join-Path $root "dist\$name"
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            $data = [Text.Encoding]::ASCII.GetBytes("2.4BatteryMonitor release v1`n$repo`n$name`n$hash`n")
            $signature = $ecdsa.SignHash($sha.ComputeHash($data))
            if ($signature.Length -ne 64) { throw 'Expected a 64-byte ECDSA P-256 signature.' }
            [IO.File]::WriteAllBytes("$path.sig", $signature)
            Write-Output "Signature written to $path.sig"
        }
    } finally {
        $ecdsa.Dispose()
        $sha.Dispose()
    }
} finally {
    $key.Dispose()
}
