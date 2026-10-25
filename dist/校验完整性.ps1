param([string]$Root)
# HTG DLSS5-AMD package integrity checker.
# Reads SHA256SUMS.txt (GBK, "<64-hex>  <relative-path>" per line) and compares
# every listed file against its recorded SHA-256. ASCII-only messages: this file
# ships without a BOM, and PowerShell 5.1 would mis-decode Chinese in it.
$ErrorActionPreference = 'Stop'
# Defensive: a trailing backslash in a quoted -Root argument escapes the quote
# in Windows command-line parsing, so trim quotes/backslashes before joining.
$Root = $Root.Trim()
if ($Root.EndsWith('"')) { $Root = $Root.TrimEnd('"') }
$Root = $Root.TrimEnd('\')
if ($Root -match '^[A-Za-z]:$') { $Root = $Root + '\' }
$enc = [Text.Encoding]::GetEncoding(936)
$sums = Join-Path $Root 'SHA256SUMS.txt'
$bad = 0
$n = 0
foreach ($line in [IO.File]::ReadAllLines($sums, $enc)) {
    $t = $line.Trim()
    if ($t -eq '') { continue }
    $h = $t.Substring(0, 64)
    $rel = $t.Substring(66).Trim()
    $n++
    $p = Join-Path $Root $rel
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Output ("MISSING: " + $rel)
        $bad++
        continue
    }
    $g = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
    if ($g -ne $h) {
        Write-Output ("MISMATCH: " + $rel)
        Write-Output ("  expected " + $h)
        Write-Output ("  actual   " + $g)
        $bad++
    }
    else {
        Write-Output ("OK  " + $rel)
    }
}
Write-Output ("checked " + $n + " files, " + $bad + " problem(s)")
exit $bad
