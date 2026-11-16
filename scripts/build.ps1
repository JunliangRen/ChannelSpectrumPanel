param([string]$MSBuild, [ValidateSet('all','x86','x64')][string]$Architecture = 'all')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$MSBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $MSBuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuild -or !(Test-Path -LiteralPath $MSBuild)) { throw 'MSBuild not found. Install Visual Studio C++ tools with v143, or pass -MSBuild.' }
Push-Location $root
try {
    $architectures = if ($Architecture -eq 'all') { @('x86','x64') } else { @($Architecture) }
    foreach ($arch in $architectures) {
        $platform = if ($arch -eq 'x86') { 'Win32' } else { 'x64' }
        New-Item -ItemType Directory -Force "build/$arch","dist/$arch" | Out-Null
        & $MSBuild foo_uie_channel_spectrum_x64.vcxproj /m /p:Configuration=Release "/p:Platform=$platform" /p:PreferredToolArchitecture=x64 /v:quiet /nologo /fl "/flp:logfile=build/$arch/build.log;verbosity=normal"
        if ($LASTEXITCODE) { throw "$arch plugin build failed." }
        & $MSBuild tests/core_tests.vcxproj /m /p:Configuration=Release "/p:Platform=$platform" /p:PreferredToolArchitecture=x64 /v:quiet /nologo
        if ($LASTEXITCODE) { throw "$arch test build failed." }
        foreach ($style in 0,1,2) {
            & "./build/$arch/tests/core_tests.exe" "build/$arch/preview-$style.bmp" $style "build/$arch/oracle-differences.txt" | Tee-Object "build/$arch/tests-$style.log"
            if ($LASTEXITCODE) { throw "$arch style $style core tests failed." }
        }
    }
    if ($Architecture -eq 'all') {
        foreach ($style in 0,1,2) {
            $x86 = (Get-FileHash "build/x86/preview-$style.bmp" -Algorithm SHA256).Hash
            $x64 = (Get-FileHash "build/x64/preview-$style.bmp" -Algorithm SHA256).Hash
            if ($x86 -ne $x64) { throw "Cross-architecture preview mismatch for style $style." }
        }
        Write-Host 'Cross-architecture synthetic previews match byte for byte.'
    }
    foreach ($arch in $architectures) { & "$PSScriptRoot/package.ps1" -Architecture $arch }
} finally { Pop-Location }
