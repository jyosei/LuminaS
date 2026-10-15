param([Parameter(Position=0)][string]$Request = '--mcp', [switch]$DryRun)
$ErrorActionPreference = 'Stop'
# Existing desktop processes may predate the system environment variable.
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) {
    $env:DEEPSEEK_API_KEY = [Environment]::GetEnvironmentVariable('DEEPSEEK_API_KEY', 'User')
}
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) {
    $env:DEEPSEEK_API_KEY = [Environment]::GetEnvironmentVariable('DEEPSEEK_API_KEY', 'Machine')
}
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) { throw 'DEEPSEEK_API_KEY is missing' }
$taskNodeCommand = Get-Command node -ErrorAction SilentlyContinue
if ($taskNodeCommand) { $taskNodePath = $taskNodeCommand.Source }
else {
    $taskNodePath = 'C:\Users\CascadeAI\AppData\Local\OpenAI\Codex\runtimes\cua_node\cfb32733c877621e\bin\node.exe'
}
if (-not (Test-Path -LiteralPath $taskNodePath)) { throw 'Node.js runtime missing; install Node.js 22+ or update the fallback path in launch.ps1' }
if ($DryRun) { & $taskNodePath (Join-Path $PSScriptRoot 'worker.mjs') '--dry-run' $Request }
else { & $taskNodePath (Join-Path $PSScriptRoot 'worker.mjs') $Request }
exit $LASTEXITCODE
