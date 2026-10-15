param([string]$SampleDirectory = 'pictures4test', [string]$OutputDirectory = '', [switch]$SkipProbes)
$ErrorActionPreference = 'Stop'
if (-not $env:QT_ROOT -or -not $env:LIBRAW_ROOT) { throw 'Set QT_ROOT and LIBRAW_ROOT; see BUILD.md.' }
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$samples = (Resolve-Path -LiteralPath $SampleDirectory).Path
if (-not $OutputDirectory) { $OutputDirectory = 'build/private-qa/s1-' + (Get-Date -Format 'yyyyMMdd-HHmmss') }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$env:PATH = "$env:QT_ROOT/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$env:QT_ROOT/plugins"
$env:QT_QPA_PLATFORM = 'windows'
$before = Get-ChildItem -LiteralPath $samples -File | Get-FileHash -Algorithm SHA256
$rawFile = Join-Path $samples '4X1A9817.CR3'
$jpegFile = Join-Path $samples '4X1A9817.JPG'
if (-not $SkipProbes) {
    foreach ($algorithm in 'ahd','dcb') {
        & './build/dev/raw_probe.exe' $rawFile --algorithm $algorithm --linear "$output/$algorithm-linear16.ppm" --preview "$output/$algorithm-preview.png" --report "$output/$algorithm.json" --reference $jpegFile --comparison "$output/$algorithm-vs-camera.png"
        if ($LASTEXITCODE -ne 0) { throw "raw_probe $algorithm failed: $LASTEXITCODE" }
    }
}
$env:LUMINA_REAL_SAMPLES = $samples
$env:LUMINA_REAL_REPORT = "$output/jpeg-4k.json"
$env:LUMINA_REAL_SCREENSHOT = "$output/jpeg-window.png"
$env:LUMINA_RAW_SCREENSHOT = "$output/raw-window.png"
& './build/dev/s0_tests.exe' realLocalSamples -o "$output/jpeg-tests.txt,txt"
if ($LASTEXITCODE -ne 0) { throw 'JPEG sample test failed' }
& './build/dev/s1_tests.exe' -o "$output/raw-tests.txt,txt"
if ($LASTEXITCODE -ne 0) { throw 'S1 native test failed' }
foreach ($path in $jpegFile,$rawFile) {
    & './build/dev/LuminaS.exe' --smoke-test $path
    if ($LASTEXITCODE -ne 0) { throw "Native preview failed: $path" }
}
$after = Get-ChildItem -LiteralPath $samples -File | Get-FileHash -Algorithm SHA256
if (@(Compare-Object $before $after -Property Path,Hash).Count -ne 0) { throw 'Source sample hash changed' }
[ordered]@{nativePlatform='windows'; jpegNativeExit=0; rawNativeExit=0; hashesUnchanged=$true; sampleDirectory=$samples; outputDirectory=$output} |
    ConvertTo-Json | Set-Content -LiteralPath "$output/native-results.json" -Encoding utf8
Write-Output "PASS: $output"
