$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'dist\foo_uie_channel_spectrum_x64-0.4.1.fb2k-component'
$dll = Join-Path $root 'dist\foo_uie_channel_spectrum_x64.dll'
if (!(Test-Path -LiteralPath $dll)) { throw 'Build the DLL before packaging.' }
$zip = [IO.Compression.ZipArchive]::new([IO.File]::Open($out,[IO.FileMode]::Create),[IO.Compression.ZipArchiveMode]::Create)
try {
    $files = @{
        'x64/foo_uie_channel_spectrum_x64.dll' = $dll
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
