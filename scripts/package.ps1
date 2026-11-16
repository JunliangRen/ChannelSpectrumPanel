param([ValidateSet('x86','x64','arm64ec')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root "dist\foo_uie_channel_spectrum_$Architecture-1.0.fb2k-component"
$dll = Join-Path $root "dist\$Architecture\foo_uie_channel_spectrum_$Architecture.dll"
if (!(Test-Path -LiteralPath $dll)) { throw 'Build the DLL before packaging.' }
# Reject stale or wrong-architecture DLLs before creating an installable package.
$bytes = [IO.File]::ReadAllBytes($dll)
$peOffset = [BitConverter]::ToInt32($bytes,0x3c)
if ([BitConverter]::ToUInt32($bytes,$peOffset) -ne 0x4550) { throw 'Invalid PE signature.' }
$expectedMachine = if ($Architecture -eq 'x86') { 0x14c } elseif ($Architecture -eq 'arm64ec') { 0x8664 } else { 0x8664 }
if ([BitConverter]::ToUInt16($bytes,$peOffset+4) -ne $expectedMachine) { throw "DLL architecture does not match $Architecture." }
if ($Architecture -eq 'arm64ec') {
    $optional = $peOffset + 24
    if ([BitConverter]::ToUInt16($bytes,$optional) -ne 0x20b) { throw 'ARM64EC requires PE32+.' }
    $loadRva = [BitConverter]::ToUInt32($bytes,$optional+112+10*8)
    $loadSize = [BitConverter]::ToUInt32($bytes,$optional+112+10*8+4)
    $sections = $optional + [BitConverter]::ToUInt16($bytes,$peOffset+20)
    $loadOffset = -1
    for ($i=0; $i -lt [BitConverter]::ToUInt16($bytes,$peOffset+6); $i++) {
        $section=$sections+40*$i
        $rva=[BitConverter]::ToUInt32($bytes,$section+12)
        $size=[BitConverter]::ToUInt32($bytes,$section+16)
        if ($loadRva -ge $rva -and $loadRva -lt $rva+$size) { $loadOffset=[BitConverter]::ToUInt32($bytes,$section+20)+$loadRva-$rva; break }
    }
    if ($loadOffset -lt 0 -or $loadSize -lt 208 -or [BitConverter]::ToUInt64($bytes,$loadOffset+200) -eq 0) { throw 'Missing ARM64EC CHPE metadata; refusing to package an x64 DLL as ARM.' }
}
$dllEntry = if ($Architecture -eq 'x64') { 'x64/foo_uie_channel_spectrum_x64.dll' } elseif ($Architecture -eq 'arm64ec') { 'arm64ec/foo_uie_channel_spectrum_arm64ec.dll' } else { 'foo_uie_channel_spectrum_x86.dll' }
$zip = [IO.Compression.ZipArchive]::new([IO.File]::Open($out,[IO.FileMode]::Create),[IO.Compression.ZipArchiveMode]::Create)
try {
    $files = @{
        $dllEntry = $dll
        'analysis/STABILITY-0.5.1.md' = (Join-Path $root 'analysis/STABILITY-0.5.1.md')
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
