param([ValidateSet('configure','build','test','run','smoke')][string]$Action = 'build')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$env:VSLANG = '1033'
if (-not $env:QT_ROOT) { throw 'Set QT_ROOT to a Qt 6 MSVC x64 installation first; see BUILD.md.' }
if ($Action -in @('configure','build')) {
    if (-not $env:LIBRAW_ROOT) { throw 'Set LIBRAW_ROOT to the LibRaw 0.22.2 SDK root; see BUILD.md.' }
    if (-not $env:LCMS_SOURCE_DIR) { throw 'Set LCMS_SOURCE_DIR to Little CMS 2.19.1 sources; see BUILD.md.' }
}
if ($Action -in @('configure','build') -and -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC x64 tools not found.' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    $environment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
    foreach ($line in $environment) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$localTools = Join-Path $taskRoot '.tools/bootstrap/Scripts'
if (Test-Path -LiteralPath $localTools) { $env:PATH = "$localTools;$env:PATH" }
$env:PATH = "$env:QT_ROOT/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$env:QT_ROOT/plugins"
switch ($Action) {
    configure { & cmake --fresh --preset dev }
    build { & cmake --build --preset dev }
    test { & ctest --preset dev }
    run { & './build/dev/LuminaS.exe' }
    smoke { & './build/dev/LuminaS.exe' --smoke-test }
}
exit $LASTEXITCODE
