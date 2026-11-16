param([string]$MSBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$MSBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $MSBuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuild -or !(Test-Path -LiteralPath $MSBuild)) { throw 'MSBuild not found. Install Visual Studio C++ tools with v143, or pass -MSBuild.' }
Push-Location $root
try {
    New-Item -ItemType Directory -Force build,dist | Out-Null
    & $MSBuild foo_uie_channel_spectrum_x64.vcxproj /m /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:quiet /nologo /fl '/flp:logfile=build\build.log;verbosity=normal'
    if ($LASTEXITCODE) { throw 'Plugin build failed.' }
    & $MSBuild tests/core_tests.vcxproj /m /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:quiet /nologo
    if ($LASTEXITCODE) { throw 'Test build failed.' }
    & .\build\tests\core_tests.exe build\synthetic-preview.bmp
    if ($LASTEXITCODE) { throw 'Core tests failed.' }
    & .\build\tests\core_tests.exe build\simple-line-preview.bmp 1
    if ($LASTEXITCODE) { throw 'Line style tests failed.' }
    & .\build\tests\core_tests.exe build\separate-lines-preview.bmp 2
    if ($LASTEXITCODE) { throw 'Separate style tests failed.' }
    & "$PSScriptRoot\package.ps1"
} finally { Pop-Location }
