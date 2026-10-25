# Generates PACKAGE_MANIFEST.txt and SHA256SUMS.txt for a release directory.
#
#   .\gen_hashes.ps1 -Root F:\HTG-DLSS5-AMD-v1.01
#
# Keep this file pure ASCII. Windows PowerShell 5.1 decodes a UTF-8 script
# without a BOM as GBK, and non-ASCII comment bytes can then eat the following
# statement (symptom: "Cannot index into a null array").

param(
    [string]$Root    = (Get-Location).Path,
    [string]$Version = '1.01',
    [string]$Author  = 'HTG'
)

$ErrorActionPreference = 'Stop'

# Defensive normalisation: a trailing backslash inside quotes escapes the
# closing quote when this value arrives from a .cmd wrapper.
$Root = $Root.Trim()
if ($Root.EndsWith('"')) { $Root = $Root.TrimEnd('"') }
$Root = $Root.TrimEnd('\')
if ($Root -match '^[A-Za-z]:$') { $Root = $Root + '\' }
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    throw "Root directory not found: $Root"
}

$sums = Join-Path $Root 'SHA256SUMS.txt'
$man  = Join-Path $Root 'PACKAGE_MANIFEST.txt'

# Runtime leftovers from a test conversion must never reach a release: they
# carry the operator's local paths and private video file names.
$stale = @(
    'engine\DLSSVideoPlayer.log',
    'engine\amd-nr-assets\mochizuki_nr.log',
    'engine\neural-runtime\NeuralWorker.log',
    'engine\neural-runtime\ReShade.log',
    'engine\neural-runtime\ngx_logs'
)
$found = @()
foreach ($rel in $stale) {
    $p = Join-Path $Root $rel
    if (Test-Path -LiteralPath $p) { $found += $rel }
}
if (Test-Path -LiteralPath (Join-Path $Root 'engine\cache')) { $found += 'engine\cache' }
if ($found.Count -gt 0) {
    Write-Output 'WARNING: runtime leftovers present, clean them before releasing:'
    foreach ($f in $found) { Write-Output ("  " + $f) }
    Write-Output ''
}

$files = Get-ChildItem -Path $Root -Recurse -File |
    Where-Object { $_.FullName -ne $sums -and $_.FullName -ne $man } |
    Sort-Object FullName

# Hash every file once; Authenticode probing is far slower than SHA-256.
$hashes = @{}
foreach ($f in $files) {
    $hashes[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
}
Write-Output ("hashed " + $files.Count + " files")

# Manifest, same column layout as the upstream project.
$ml = New-Object System.Collections.Generic.List[string]
$ml.Add("ProductVersion=$Version")
$ml.Add("Author=$Author")
$ml.Add("Packaged=" + (Get-Date).ToString('yyyy-MM-dd'))
$ml.Add('Path|Size|SHA256|Authenticode|Signer')
foreach ($f in $files) {
    $rel = $f.FullName.Substring($Root.Length + 1)
    $sig = Get-AuthenticodeSignature -LiteralPath $f.FullName
    # IsOSBinary is unreliable in some environments; read the MZ header ourselves.
    $fs = [IO.File]::OpenRead($f.FullName)
    $b1 = $fs.ReadByte(); $b2 = $fs.ReadByte(); $fs.Close()
    $isPE = ($b1 -eq 0x4D -and $b2 -eq 0x5A)
    if ($isPE) {
        $st = [string]$sig.Status
        $signer = ''
        if ($sig.SignerCertificate) { $signer = $sig.SignerCertificate.Subject }
    } else {
        $st = 'N/A'
        $signer = ''
    }
    $ml.Add("$rel|$($f.Length)|$($hashes[$f.FullName])|$st|$signer")
}
# Manifest stays UTF-8: it is for humans and tools, nothing parses it under chcp 936.
[IO.File]::WriteAllLines($man, $ml, [Text.Encoding]::UTF8)
Write-Output 'manifest written'

# The sums cover everything except themselves, including the manifest above.
# GBK on purpose: verify.ps1's .cmd wrapper runs under chcp 936 and reads this
# file, so Chinese-named entries must be stored in the code page cmd speaks.
$gbk = [Text.Encoding]::GetEncoding(936)
$sl = New-Object System.Collections.Generic.List[string]
foreach ($f in $files) {
    $rel = $f.FullName.Substring($Root.Length + 1)
    $sl.Add($hashes[$f.FullName] + '  ' + $rel)
}
[IO.File]::WriteAllLines($sums, $sl, $gbk)
Write-Output ("sums written: " + $sl.Count + " entries")
