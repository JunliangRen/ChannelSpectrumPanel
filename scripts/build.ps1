param([string]$MSBuild, [ValidateSet('all','x86','x64','arm64ec')][string]$Architecture = 'all')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$MSBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $MSBuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (!$MSBuild -or !(Test-Path -LiteralPath $MSBuild)) { throw 'MSBuild not found. Install Visual Studio C++ tools with v143 (x86/x64 and ARM64/ARM64EC), or pass -MSBuild.' }
Push-Location $root
try {
    $architectures = if ($Architecture -eq 'all') { @('x86','x64','arm64ec') } else { @($Architecture) }
    foreach ($arch in $architectures) {
        $platform = if ($arch -eq 'x86') { 'Win32' } elseif ($arch -eq 'arm64ec') { 'ARM64EC' } else { 'x64' }
        if ($arch -eq 'arm64ec') {
            $armTools = ((& $MSBuild foo_uie_channel_spectrum_x64.vcxproj /p:Configuration=Release /p:Platform=ARM64EC -getProperty:VCToolsInstallDir /nologo) -join '').Trim()
            if (!$armTools -or !(Test-Path -LiteralPath (Join-Path $armTools 'bin/Hostx64/arm64/cl.exe'))) {
                throw 'ARM64EC tools are missing. Install Visual Studio v143 ARM64/ARM64EC build tools, then rerun. No ARM package was produced.'
            }
        }
        New-Item -ItemType Directory -Force "build/$arch","dist/$arch" | Out-Null
        & $MSBuild foo_uie_channel_spectrum_x64.vcxproj /m /p:Configuration=Release "/p:Platform=$platform" /p:PreferredToolArchitecture=x64 /v:quiet /nologo /fl "/flp:logfile=build/$arch/build.log;verbosity=normal"
        if ($LASTEXITCODE) { throw "$arch plugin build failed." }
        & $MSBuild tests/core_tests.vcxproj /m /p:Configuration=Release "/p:Platform=$platform" /p:PreferredToolArchitecture=x64 /v:quiet /nologo
        if ($LASTEXITCODE) { throw "$arch test build failed." }
        $crossCompileArm = $arch -eq 'arm64ec' -and [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'Arm64'
        # Only executing the standalone harness needs the SDK shared runtime DLL.
        & $MSBuild tests/ui_adapter_tests.vcxproj /m "/p:BuildTestRuntime=$(!$crossCompileArm)" /p:Configuration=Release "/p:Platform=$platform" /p:PreferredToolArchitecture=x64 /v:quiet /nologo
        if ($LASTEXITCODE) { throw "$arch SDK adapter test build failed." }
        & $MSBuild tests/spectrum_window_tests.vcxproj /m "/p:BuildTestRuntime=$(!$crossCompileArm)" /p:Configuration=Release "/p:Platform=$platform" /p:PreferredToolArchitecture=x64 /v:quiet /nologo
        if ($LASTEXITCODE) { throw "$arch real window test build failed." }
        if ($crossCompileArm) {
            Write-Host 'ARM64EC plugin and tests compiled; runtime tests require Windows on ARM and were NOT run.'
            continue
        }
        & "./build/$arch/tests/ui_adapter_tests.exe" | Tee-Object "build/$arch/ui-adapter-tests.log"
        if ($LASTEXITCODE) { throw "$arch SDK adapter tests failed." }
        $windowLog = Join-Path $root "build/$arch/spectrum-window-tests.log"
        $windowErrors = Join-Path $root "build/$arch/spectrum-window-tests.err"
        $windowTest = Start-Process -FilePath (Join-Path $root "build/$arch/tests/spectrum_window_tests.exe") -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput $windowLog -RedirectStandardError $windowErrors
        try {
            if (!$windowTest.WaitForExit(60000)) { $windowTest.Kill(); $windowTest.WaitForExit(); throw "$arch real window tests timed out." }
            Get-Content $windowLog
            if ((Get-Item $windowErrors).Length) { Get-Content $windowErrors }
            # Some SDK fatal paths terminate without a nonzero process exit code.
            if ($windowTest.ExitCode -ne 0 -or !(Select-String -LiteralPath $windowLog -Pattern '^PASS: .* real SpectrumWindow lifecycle checks' -Quiet)) { throw "$arch real window tests failed or exited before completion." }
        } finally { $windowTest.Dispose() }
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
        Write-Host 'x86/x64 synthetic previews match byte for byte.'
    }
    & "$PSScriptRoot/package.ps1" -Architecture $Architecture
    if ($Architecture -eq 'all') { & "$PSScriptRoot/../tests/package_tests.ps1" }
} finally { Pop-Location }
