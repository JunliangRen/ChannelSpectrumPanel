param([ValidateSet('all','x86','x64','arm64ec')][string]$Architecture = 'all')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$root = Split-Path $PSScriptRoot -Parent
$suffix = if ($Architecture -eq 'all') { '' } else { "_$Architecture" }
$out = Join-Path $root "dist\foo_uie_channel_spectrum$suffix-1.4.fb2k-component"
$files = @{
    'README.md' = (Join-Path $root 'README.md')
    'THIRD-PARTY-NOTICES.txt' = (Join-Path $root 'THIRD-PARTY-NOTICES.txt')
}
$architectures = if ($Architecture -eq 'all') { @('x86','x64','arm64ec') } else { @($Architecture) }
foreach ($arch in $architectures) {
$Architecture = $arch
$dll = Join-Path $root "dist\$arch\foo_uie_channel_spectrum.dll"
if (!(Test-Path -LiteralPath $dll)) { throw "Missing $arch DLL; build all required architectures first." }
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
$entry = if ($arch -eq 'x86') { 'foo_uie_channel_spectrum.dll' } else { "$arch/foo_uie_channel_spectrum.dll" }
$files[$entry] = $dll
}
# Validate all inputs before replacing a previous package.
foreach ($source in $files.Values) { if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing input: $source" } }
$zip = [IO.Compression.ZipArchive]::new([IO.File]::Open("$out.tmp",[IO.FileMode]::Create),[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in ($files.Keys | Sort-Object)) {
        $entry = $zip.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
        $stream = $entry.Open()
        try { $bytes=[IO.File]::ReadAllBytes($files[$name]); $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
    }
} finally { $zip.Dispose() }
Move-Item -LiteralPath "$out.tmp" -Destination $out -Force
$hash = (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$out.sha256", "$hash  $([IO.Path]::GetFileName($out))`r`n",[Text.UTF8Encoding]::new($false))
Write-Host "Package: $out"
