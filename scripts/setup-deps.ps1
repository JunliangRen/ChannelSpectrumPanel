param([string]$SevenZip = '7z')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vendor = Join-Path $root 'vendor'
New-Item -ItemType Directory -Force $vendor | Out-Null
$archive = Join-Path $vendor 'SDK-2026-10-01.7z'
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest 'https://www.foobar2000.org/downloads/SDK-2026-10-01.7z' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'D4C55077336FAE81BF8DF0259B5B2748FA45EA84132C656EAD93EB123CBCDC26') { throw 'SDK archive checksum mismatch.' }
if (!(Test-Path -LiteralPath (Join-Path $vendor 'foobar2000\SDK\foobar2000.h'))) {
    & $SevenZip x $archive "-o$vendor" -y
    if ($LASTEXITCODE) { throw 'SDK extraction failed.' }
}
$cui = Join-Path $vendor 'columns_ui-sdk'
$revision = 'e33a58aece664dc2f02342289f25211af5e7576c'
if (!(Test-Path -LiteralPath $cui)) {
    & git clone https://github.com/reupen/columns_ui-sdk.git $cui
    if ($LASTEXITCODE) { throw 'Columns UI SDK clone failed.' }
    & git -C $cui checkout --detach $revision
    if ($LASTEXITCODE) { throw 'Columns UI SDK checkout failed.' }
}
$current = & git -C $cui rev-parse HEAD
if ($LASTEXITCODE -or $current -ne $revision) { throw 'Existing Columns UI SDK revision differs; it was left unchanged.' }
Write-Host 'Pinned SDK dependencies are ready.'
