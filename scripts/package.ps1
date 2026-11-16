param([ValidateSet('x86','x64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root "dist\foo_uie_channel_spectrum_$Architecture-0.5.0.fb2k-component"
$dll = Join-Path $root "dist\$Architecture\foo_uie_channel_spectrum_$Architecture.dll"
if (!(Test-Path -LiteralPath $dll)) { throw 'Build the DLL before packaging.' }
# Reject stale or wrong-architecture DLLs before creating an installable package.
$bytes = [IO.File]::ReadAllBytes($dll)
$peOffset = [BitConverter]::ToInt32($bytes,0x3c)
if ([BitConverter]::ToUInt32($bytes,$peOffset) -ne 0x4550) { throw 'Invalid PE signature.' }
$expectedMachine = if ($Architecture -eq 'x86') { 0x14c } else { 0x8664 }
if ([BitConverter]::ToUInt16($bytes,$peOffset+4) -ne $expectedMachine) { throw "DLL architecture does not match $Architecture." }
$dllEntry = if ($Architecture -eq 'x64') { 'x64/foo_uie_channel_spectrum_x64.dll' } else { 'foo_uie_channel_spectrum_x86.dll' }
$zip = [IO.Compression.ZipArchive]::new([IO.File]::Open($out,[IO.FileMode]::Create),[IO.Compression.ZipArchiveMode]::Create)
try {
    $files = @{
        $dllEntry = $dll
        'analysis/DUAL-ARCHITECTURE.md' = (Join-Path $root 'analysis/DUAL-ARCHITECTURE.md')
        'analysis/RESTORATION-0.4.md' = (Join-Path $root 'analysis/RESTORATION-0.4.md')
        'analysis/DIRECTION-0.4.1.md' = (Join-Path $root 'analysis/DIRECTION-0.4.1.md')
        'analysis/PARITY.md' = (Join-Path $root 'analysis/PARITY.md')
        'analysis/evidence/csp-field-order.tsv' = (Join-Path $root 'analysis/evidence/csp-field-order.tsv')
        'README.md' = (Join-Path $root 'README.md')
        'THIRD-PARTY-NOTICES.txt' = (Join-Path $root 'THIRD-PARTY-NOTICES.txt')
    }
    foreach ($name in $files.Keys) {
        $entry = $zip.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
        $stream = $entry.Open()
        try { $bytes=[IO.File]::ReadAllBytes($files[$name]); $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
    }
} finally { $zip.Dispose() }
$hash = (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$out.sha256", "$hash  $([IO.Path]::GetFileName($out))`r`n",[Text.UTF8Encoding]::new($false))
Write-Host "Package: $out"
