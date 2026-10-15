param([ValidateRange(1,6)][int]$Threads=4,[switch]$Smoke,[string]$Photo='pictures4test/4X1A9817.CR3')
$ErrorActionPreference='Stop'
if(-not $env:QT_ROOT){throw 'Set QT_ROOT first; see OPTIMIZATION-EXPERIMENTS.md'}
$taskRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$env:PATH="$env:QT_ROOT/bin;$env:PATH"
$env:QT_PLUGIN_PATH="$env:QT_ROOT/plugins"
$env:OMP_NUM_THREADS=[string]$Threads
$env:OMP_WAIT_POLICY='PASSIVE'
if($Smoke){ & './build/openmp/RelWithDebInfo/LuminaS.exe' --smoke-test $Photo }
else{ & './build/openmp/RelWithDebInfo/LuminaS.exe' $Photo }
exit $LASTEXITCODE
