# Isolated SDK experiment: do not replace the pinned official SDK or build/dev.
param([string]$Destination = '.tools/LibRaw-openmp')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$archive = '.tools/downloads/LibRaw-0.22.2-Win64.zip'
$expectedHash = 'AC64FA12BB00A7581332D4C6AB918C0533FB3F119D6B668D47A6875410DCA948'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Unrecognized source archive' }
Expand-Archive -LiteralPath $archive -DestinationPath $Destination -Force
$sdk = (Resolve-Path -LiteralPath "$Destination/LibRaw-0.22.2").Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'VS2022 C++ tools missing' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$env:VSLANG = '1033'
$environment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars failed' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
Push-Location -LiteralPath $sdk
try {
    # /A avoids accidentally reusing object files from the binary distribution.
    & nmake.exe /nologo /A /f Makefile.msvc 'COPT_OPT=/openmp' 'bin\libraw.dll'
    if ($LASTEXITCODE -ne 0) { throw 'LibRaw OpenMP compilation failed' }
    & dumpbin.exe /dependents 'bin/libraw.dll'
    Get-FileHash -LiteralPath 'bin/libraw.dll' -Algorithm SHA256 | Format-List Hash
} finally { Pop-Location }
