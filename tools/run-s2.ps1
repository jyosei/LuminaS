param([string[]]$Photos=@())
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
if(-not $env:QT_ROOT){$env:QT_ROOT=(Resolve-Path -LiteralPath '.tools/Qt/6.8.3/msvc2022_64').Path}
$env:PATH="$env:QT_ROOT/bin;$env:PATH"
$env:QT_PLUGIN_PATH="$env:QT_ROOT/plugins"
if(-not $env:OMP_NUM_THREADS){$env:OMP_NUM_THREADS='4'}
if(-not $env:OMP_WAIT_POLICY){$env:OMP_WAIT_POLICY='PASSIVE'}
& './build/dev/LuminaS.exe' @Photos
exit $LASTEXITCODE
