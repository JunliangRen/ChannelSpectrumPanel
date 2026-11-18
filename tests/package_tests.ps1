param([string]$Package = (Join-Path $PSScriptRoot '../dist/foo_uie_channel_spectrum-1.4.fb2k-component'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Split-Path $PSScriptRoot -Parent
$expected=@{
    'foo_uie_channel_spectrum.dll'='dist/x86/foo_uie_channel_spectrum.dll'
    'x64/foo_uie_channel_spectrum.dll'='dist/x64/foo_uie_channel_spectrum.dll'
    'arm64ec/foo_uie_channel_spectrum.dll'='dist/arm64ec/foo_uie_channel_spectrum.dll'
    'README.md'='README.md'
    'THIRD-PARTY-NOTICES.txt'='THIRD-PARTY-NOTICES.txt'
}
if ((Get-Content -LiteralPath "$Package.sha256") -notlike "$((Get-FileHash -LiteralPath $Package).Hash) *") { throw 'Package checksum mismatch.' }
$zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Package))
try {
    if ($zip.Entries.Count -ne $expected.Count) { throw 'Unexpected package entry count.' }
    $seen=@{}
    foreach ($entry in $zip.Entries) {
        if (!$expected.ContainsKey($entry.FullName) -or $seen.ContainsKey($entry.FullName)) { throw "Unexpected or duplicate entry: $($entry.FullName)" }
        $seen[$entry.FullName]=$true
        $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
        try { $hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') } finally { $stream.Dispose();$sha.Dispose() }
        if ($hash -ne (Get-FileHash -LiteralPath (Join-Path $root $expected[$entry.FullName])).Hash) { throw "Entry differs from source: $($entry.FullName)" }
    }
} finally { $zip.Dispose() }
Write-Output 'PASS: unified package has exactly the three matching architecture DLLs, README and third-party notices; all SHA-256 hashes match.'
