param([string]$Destination = '.tools/lcms')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
New-Item -ItemType Directory -Path '.tools/downloads' -Force | Out-Null
$archive = '.tools/downloads/lcms2.19.1.zip'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://github.com/mm2/Little-CMS/archive/refs/tags/lcms2.19.1.zip' -OutFile $archive
}
$expectedHash = '8B7FDC5A708D9F5F41BAEECAFD2C32B993F0614448F2533EEA5DEAF841EA1C78'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Little CMS archive differs from the pinned S2 source.' }
Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
Write-Output (Resolve-Path -LiteralPath "$Destination/Little-CMS-lcms2.19.1").Path
