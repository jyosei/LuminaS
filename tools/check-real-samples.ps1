param([string]$SampleDirectory = 'pictures4test', [string]$QtRoot = $env:QT_ROOT)
$ErrorActionPreference = 'Stop'
if (-not $QtRoot) { throw 'Set QT_ROOT first.' }
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$samplePath = (Resolve-Path -LiteralPath $SampleDirectory).Path
$privateQa = Join-Path $taskRoot 'build/private-qa'
New-Item -ItemType Directory -Path $privateQa -Force | Out-Null
$env:PATH = "$QtRoot/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$QtRoot/plugins"
$env:QT_QPA_PLATFORM = 'windows'
$env:LUMINA_REAL_SAMPLES = $samplePath
$env:LUMINA_REAL_SCREENSHOT = Join-Path $privateQa 'real-jpeg-window.png'
$env:LUMINA_REAL_REPORT = Join-Path $privateQa 'real-samples.json'
$beforeHashes = Get-ChildItem -LiteralPath $samplePath -File | Get-FileHash -Algorithm SHA256
& './build/dev/s0_tests.exe' realLocalSamples -o "$privateQa/real-tests.txt,txt"
$testExit = $LASTEXITCODE
if ($testExit -ne 0) { exit $testExit }
& './build/dev/LuminaS.exe' --smoke-test "$samplePath/4X1A9817.JPG"
$jpegExit = $LASTEXITCODE
& './build/dev/LuminaS.exe' --smoke-test "$samplePath/4X1A9817.CR3"
$rawExit = $LASTEXITCODE
$afterHashes = Get-ChildItem -LiteralPath $samplePath -File | Get-FileHash -Algorithm SHA256
$unchanged = @(Compare-Object $beforeHashes $afterHashes -Property Path,Hash).Count -eq 0
[ordered]@{testExit=$testExit; jpegNativeExit=$jpegExit; rawNativeExit=$rawExit; hashesUnchanged=$unchanged; expectedRawExit=0} |
    ConvertTo-Json | Set-Content -LiteralPath "$privateQa/native-results.json" -Encoding utf8
if ($jpegExit -ne 0 -or $rawExit -ne 0 -or -not $unchanged) { exit 1 }
exit 0
