param([Parameter(Position=0)][string]$Request = '--mcp')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) { $env:DEEPSEEK_API_KEY = [Environment]::GetEnvironmentVariable('DEEPSEEK_API_KEY','User') }
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) { $env:DEEPSEEK_API_KEY = [Environment]::GetEnvironmentVariable('DEEPSEEK_API_KEY','Machine') }
if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) { throw 'DEEPSEEK_API_KEY is missing' }
$taskNodeCommand = Get-Command node -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($taskNodeCommand) { $taskNode = $taskNodeCommand.Source }
else { $taskNode = 'C:\Users\CascadeAI\AppData\Local\OpenAI\Codex\runtimes\cua_node\a9ba6825a525118c\bin\node.exe' }
if (-not (Test-Path -LiteralPath $taskNode)) { throw 'Node.js runtime missing; install Node.js 22+ or update the fallback path in tools/dsh/launch.ps1' }
& $taskNode (Join-Path $PSScriptRoot 'bridge.mjs') $Request
exit $LASTEXITCODE
