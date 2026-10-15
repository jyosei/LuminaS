<#
.SYNOPSIS
Runs the GPU experiment probe (build/dev/gpu_probe.exe) and records hardware and process evidence.

.DESCRIPTION
New, self-contained experiment wrapper. It does not build, install or package anything, and it
never writes outside the report directory. Evidence produced per run:
  hardware.json  - GPU adapters, OS bitness, CPU, RAM and a timestamp
  report.json    - written by gpu_probe itself (its --report target)
  stdout.txt     - probe standard output
  stderr.txt     - probe standard error
  process.json   - exit code, timeout state and wall-clock duration

The report directory must resolve under <workspace>/build/, must not exist yet (or must be
empty) and its four evidence file names must be free, so previous reports are never overwritten.

.EXAMPLE
pwsh -File tools/run-gpu-experiment.ps1 -CapabilitiesOnly
pwsh -File tools/run-gpu-experiment.ps1 -Size 24mp -Iterations 100
#>
[CmdletBinding()]
param(
    [string]$ReportDirectory = 'build/private-qa/gpu',
    [ValidateRange(100, 1000)][int]$Iterations = 100,
    [ValidateSet('small', '4k', '24mp', '45mp')][string]$Size = '4k',
    [switch]$CapabilitiesOnly,
    [switch]$Present
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$script:ProbeTimeoutSeconds = 900
$script:EvidenceNames = @('hardware.json', 'report.json', 'stdout.txt', 'stderr.txt', 'process.json')

function Get-WorkspaceRoot {
    $root = [System.IO.Directory]::GetParent($PSScriptRoot).FullName
    if (-not $root) { throw 'Cannot resolve the workspace root from PSScriptRoot.' }
    $resolved = [System.IO.Path]::GetFullPath($root)
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "Workspace root does not exist: $resolved"
    }
    return $resolved.TrimEnd([System.IO.Path]::DirectorySeparatorChar)
}

function Assert-NoReparsePoint {
    param([string]$Path, [string]$Description)
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        throw "$Description is a reparse point (symlink or junction), which is not accepted: $Path"
    }
}

function Resolve-ReportDirectory {
    param([string]$WorkspaceRoot, [string]$Requested)
    if ([string]::IsNullOrWhiteSpace($Requested)) { throw 'ReportDirectory must not be empty.' }
    $candidate = $Requested
    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $WorkspaceRoot $candidate
    }
    $full = [System.IO.Path]::GetFullPath($candidate)
    $buildRoot = Join-Path $WorkspaceRoot 'build'

    $comparison = [System.StringComparison]::OrdinalIgnoreCase
    $prefix = $buildRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $isUnderBuild = $full.StartsWith($prefix, $comparison)
    if (-not $isUnderBuild) {
        throw "ReportDirectory must resolve under $buildRoot (received: $full)."
    }

    $relative = $full.Substring($prefix.Length)
    if ([string]::IsNullOrWhiteSpace($relative)) { throw 'ReportDirectory must not be the build directory itself.' }
    foreach ($segment in $relative.Split([System.IO.Path]::DirectorySeparatorChar)) {
        if ($segment -eq '.' -or $segment -eq '..' -or [string]::IsNullOrWhiteSpace($segment)) {
            throw "ReportDirectory contains an invalid path segment: $Requested"
        }
    }

    # Reject symlink/junction escape: build itself, plus every existing ancestor of the target.
    if (Test-Path -LiteralPath $buildRoot -PathType Container) {
        Assert-NoReparsePoint -Path $buildRoot -Description 'build directory'
    }
    $cursor = $buildRoot
    foreach ($segment in $relative.Split([System.IO.Path]::DirectorySeparatorChar)) {
        $cursor = Join-Path $cursor $segment
        if (Test-Path -LiteralPath $cursor) {
            Assert-NoReparsePoint -Path $cursor -Description 'report directory ancestor'
        }
    }
    return $full
}

function Initialize-ReportDirectory {
    param([string]$ReportDirectory)
    if (Test-Path -LiteralPath $ReportDirectory) {
        if (-not (Test-Path -LiteralPath $ReportDirectory -PathType Container)) {
            throw "ReportDirectory exists and is not a directory: $ReportDirectory"
        }
        $existing = @(Get-ChildItem -LiteralPath $ReportDirectory -Force)
        if ($existing.Count -gt 0) {
            throw "ReportDirectory is not empty; refusing to overwrite previous reports: $ReportDirectory"
        }
    } else {
        $null = New-Item -ItemType Directory -Path $ReportDirectory -Force
    }
    foreach ($name in $script:EvidenceNames) {
        $target = Join-Path $ReportDirectory $name
        if (Test-Path -LiteralPath $target) {
            throw "Refusing to overwrite an existing report file: $target"
        }
    }
}

function Resolve-QtRoot {
    if ($env:QT_ROOT) {
        $fromEnv = [System.IO.Path]::GetFullPath($env:QT_ROOT)
        if (Test-Path -LiteralPath (Join-Path $fromEnv 'bin') -PathType Container) { return $fromEnv }
        Write-Warning "QT_ROOT is set but has no bin directory; falling back to the workspace Qt: $fromEnv"
    }
    $workspaceQt = Join-Path (Get-WorkspaceRoot) '.tools/Qt/6.8.3/msvc2022_64'
    if (Test-Path -LiteralPath (Join-Path $workspaceQt 'bin') -PathType Container) { return $workspaceQt }
    throw "Qt not found. Set QT_ROOT to a Qt 6 MSVC x64 installation, or install the workspace Qt at $workspaceQt"
}

function Invoke-GpuProbe {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int]$TimeoutSeconds,
        [string]$StdOutPath,
        [string]$StdErrPath
    )
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Executable
    $startInfo.Arguments = ($Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' '
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $stdoutTask = $null
    $stderrTask = $null
    $timedOut = $false
    $exitCode = $null

    if (-not $process.Start()) { throw "Failed to start the probe process: $Executable" }
    try {
        # Drain both streams asynchronously to avoid pipe-buffer deadlock.
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $finished = $process.WaitForExit($TimeoutSeconds * 1000)
        if (-not $finished) {
            $timedOut = $true
            # Kill only the process tree owned by this probe instance.
            $null = & taskkill.exe /PID $process.Id /T /F 2>&1
            $null = $process.WaitForExit(30000)
        }
        if ($stdoutTask -and $stdoutTask.Wait(5000)) { [System.IO.File]::WriteAllText($StdOutPath, $stdoutTask.GetAwaiter().GetResult()) }
        else { [System.IO.File]::WriteAllText($StdOutPath, 'Output drain did not complete within the bounded wait.') }
        if ($stderrTask -and $stderrTask.Wait(5000)) { [System.IO.File]::WriteAllText($StdErrPath, $stderrTask.GetAwaiter().GetResult()) }
        else { [System.IO.File]::WriteAllText($StdErrPath, 'Error output drain did not complete within the bounded wait.') }
        if (-not $timedOut) { $exitCode = $process.ExitCode }
        elseif ($process.HasExited) { $exitCode = $process.ExitCode }
    } finally {
        $stopwatch.Stop()
        $process.Dispose()
    }

    return [pscustomobject]@{
        ExitCode  = $exitCode
        TimedOut  = $timedOut
        WallMs    = [int]$stopwatch.ElapsedMilliseconds
        Command   = $Executable
    }
}

function Get-HardwareSnapshot {
    $adapters = @()
    try {
        $controllers = @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop)
        foreach ($controller in $controllers) {
            $adapters += [ordered]@{
                name          = $controller.Name
                driverVersion = $controller.DriverVersion
                driverDate    = if ($controller.DriverDate) { $controller.DriverDate.ToString('o') } else { $null }
                adapterRAM    = $controller.AdapterRAM
            }
        }
    } catch {
        Write-Warning "Could not read Win32_VideoController: $($_.Exception.Message)"
    }

    $cpuName = $null
    $logicalCores = $env:NUMBER_OF_PROCESSORS
    try {
        $cpu = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
        if ($cpu) {
            $cpuName = $cpu.Name
            if ($cpu.NumberOfLogicalProcessors) { $logicalCores = [int]$cpu.NumberOfLogicalProcessors }
        }
    } catch {
        Write-Warning "Could not read Win32_Processor: $($_.Exception.Message)"
    }

    $memory = $null
    try {
        $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
        if ($computer) { $memory = $computer.TotalPhysicalMemory }
    } catch {
        Write-Warning "Could not read Win32_ComputerSystem: $($_.Exception.Message)"
    }

    return [ordered]@{
        timestampUtc        = [System.DateTime]::UtcNow.ToString('o')
        is64BitOperatingSystem = [System.Environment]::Is64BitOperatingSystem
        is64BitProcess      = [System.Environment]::Is64BitProcess
        osVersion           = [System.Environment]::OSVersion.VersionString
        videoControllers    = $adapters
        cpu                 = [ordered]@{
            name                 = $cpuName
            logicalProcessors    = $logicalCores
        }
        totalPhysicalMemoryBytes = $memory
    }
}

function Write-JsonFile {
    param([string]$Path, $Data)
    $json = $Data | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($Path, $json + [System.Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
}

$workspaceRoot = Get-WorkspaceRoot
Set-Location -LiteralPath $workspaceRoot

$probePath = Join-Path $workspaceRoot 'build/dev/gpu_probe.exe'
if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
    throw "gpu_probe.exe not found at $probePath. Build the dev preset first; this script never builds or installs."
}

$reportDir = Resolve-ReportDirectory -WorkspaceRoot $workspaceRoot -Requested $ReportDirectory

$qtRoot = Resolve-QtRoot
$env:PATH = (Join-Path $qtRoot 'bin') + [System.IO.Path]::PathSeparator + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
$env:QT_QPA_PLATFORM = 'windows'

Initialize-ReportDirectory -ReportDirectory $reportDir

$sourceManifest = @('CMakeLists.txt', 'tools/run-gpu-experiment.ps1',
    'tools/gpu-probe/main.cpp', 'tools/gpu-probe/presentation.inc',
    'tools/gpu-probe/basic.comp', 'tools/gpu-probe/present.vert',
    'tools/gpu-probe/present.frag', 'build/dev/gpu_probe.exe') | ForEach-Object {
    $manifestPath = Join-Path $workspaceRoot $_
    [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash }
}
Write-JsonFile -Path (Join-Path $reportDir 'source-manifest.json') -Data $sourceManifest

$reportJsonPath = Join-Path $reportDir 'report.json'
$stdoutPath = Join-Path $reportDir 'stdout.txt'
$stderrPath = Join-Path $reportDir 'stderr.txt'
$processJsonPath = Join-Path $reportDir 'process.json'
$hardwareJsonPath = Join-Path $reportDir 'hardware.json'

Write-JsonFile -Path $hardwareJsonPath -Data (Get-HardwareSnapshot)

$probeArguments = @(
    '--report', $reportJsonPath,
    '--iterations', [string]$Iterations,
    '--size', $Size
)
if ($CapabilitiesOnly) { $probeArguments += '--capabilities-only' }
if ($Present) { $probeArguments += '--present' }

$startupUtc = [System.DateTime]::UtcNow.ToString('o')
$result = Invoke-GpuProbe -Executable $probePath -Arguments $probeArguments -WorkingDirectory $workspaceRoot `
    -TimeoutSeconds $script:ProbeTimeoutSeconds -StdOutPath $stdoutPath -StdErrPath $stderrPath

$processRecord = [ordered]@{
    command        = $result.Command
    arguments      = $probeArguments
    startupUtc     = $startupUtc
    exitCode       = $result.ExitCode
    timedOut       = $result.TimedOut
    wallMs         = $result.WallMs
    timeoutSeconds = $script:ProbeTimeoutSeconds
    reportJson     = $reportJsonPath
    stdout         = $stdoutPath
    stderr         = $stderrPath
    capabilitiesOnly = [bool]$CapabilitiesOnly
    present        = [bool]$Present
    iterations     = $Iterations
    size           = $Size
}
Write-JsonFile -Path $processJsonPath -Data $processRecord

"gpu_probe exit=$($result.ExitCode) timedOut=$($result.TimedOut) wallMs=$($result.WallMs) report=$reportDir"

if ($result.TimedOut) {
    Write-Error "gpu_probe exceeded $($script:ProbeTimeoutSeconds) seconds and was terminated (exit code: $($result.ExitCode))."
    exit 1
}
exit [int]$result.ExitCode
