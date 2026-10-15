param([string]$Destination = '.tools/LibRaw-0.22.2')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
New-Item -ItemType Directory -Path '.tools/downloads' -Force | Out-Null
$archive = '.tools/downloads/LibRaw-0.22.2-Win64.zip'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://www.libraw.org/data/LibRaw-0.22.2-Win64.zip' -OutFile $archive
}
$expectedHash = 'AC64FA12BB00A7581332D4C6AB918C0533FB3F119D6B668D47A6875410DCA948'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'LibRaw archive differs from the pinned S1 archive.' }
Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
Write-Output (Resolve-Path -LiteralPath "$Destination/LibRaw-0.22.2").Path
