param([string]$QtRoot = $env:QT_ROOT)
$ErrorActionPreference = 'Stop'
if (-not $QtRoot) { throw 'Set QT_ROOT first.' }
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$QtRoot/plugins"
$env:QT_QPA_PLATFORM = 'windows'
$env:LUMINA_SCREENSHOT = Join-Path $taskRoot 'docs/engineering/evidence/s0-window.png'
$env:LUMINA_FIXTURES = Join-Path $taskRoot 'build/dev/fixtures'
& './build/dev/s0_tests.exe' -o 'docs/engineering/evidence/native-tests.txt,txt'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Copy-Item -LiteralPath 'build/dev/fixtures/manifest.json' -Destination 'docs/engineering/evidence/fixture-manifest.json'
& './build/dev/LuminaS.exe' --smoke-test "$env:LUMINA_FIXTURES/方向六.jpg" *> 'docs/engineering/evidence/native-startup.log'
$nativeExit = $LASTEXITCODE
"Native QPA=windows; lumina --smoke-test; exit=$nativeExit" | Add-Content -LiteralPath 'docs/engineering/evidence/native-startup.log' -Encoding utf8
exit $nativeExit
