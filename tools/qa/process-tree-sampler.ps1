# Read-only, sampled observation. No process termination or file-content reads.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$RootProcessId,
    [Parameter(Mandatory)][string]$ReportPath,
    [ValidateRange(1,3600)][int]$Seconds=120,
    [ValidateRange(250,5000)][int]$IntervalMs=1000,
    [string[]]$OwnedDiskRoot=@()
)
$ErrorActionPreference='Stop'
function Get-IdentityTicks([datetime]$time){ [long]$ticks=$time.ToUniversalTime().Ticks; return $ticks-($ticks%10) } # CIM retains microseconds.
$target=[IO.Path]::GetFullPath($ReportPath)
if(Test-Path -LiteralPath $target){throw 'Report already exists'}
if(-not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($target))){throw 'Report parent must exist'}
$diskRoots=[Collections.Generic.List[string]]::new()
foreach($requestedRoot in $OwnedDiskRoot){
    $directory=Get-Item -LiteralPath $requestedRoot -Force
    if(-not $directory.PSIsContainer -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Disk root must be a real directory'}
    $diskRoot=$directory.FullName
    if($diskRoot.TrimEnd('\','/') -eq [IO.Path]::GetPathRoot($diskRoot).TrimEnd('\','/')){throw 'Drive root is forbidden'}
    foreach($existingRoot in $diskRoots){
        if($diskRoot.Equals($existingRoot,[StringComparison]::OrdinalIgnoreCase) -or $diskRoot.StartsWith($existingRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or $existingRoot.StartsWith($diskRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Overlapping disk roots are forbidden'}
    }
    $diskRoots.Add($diskRoot)
}
function Get-DiskObservation {
    if($diskRoots.Count -eq 0){return $null}
    $pending=[Collections.Generic.Stack[string]]::new();foreach($diskRoot in $diskRoots){$pending.Push($diskRoot)}
    [long]$bytes=0;$entries=0;$files=0;$issues=[Collections.Generic.List[string]]::new();$truncated=$false
    while($pending.Count -gt 0 -and -not $truncated){
        $next=$pending.Pop()
        try{
            # Recheck queued directories before entering; never traverse links.
            $dir=Get-Item -LiteralPath $next -Force
            if($dir.Attributes -band [IO.FileAttributes]::ReparsePoint){$issues.Add('directory-reparse-skipped');continue}
            foreach($entry in [IO.DirectoryInfo]::new($next).EnumerateFileSystemInfos()){
                if(++$entries -gt 10000){$truncated=$true;break}
                if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){$issues.Add('entry-reparse-skipped');continue}
                if($entry.Attributes -band [IO.FileAttributes]::Directory){$pending.Push($entry.FullName)}
                else{$bytes+=$entry.Length;$files++}
            }
        }catch{$issues.Add('disk-query-failed: '+$_.Exception.GetType().Name)}
    }
    return [pscustomobject]@{roots=@($diskRoots.ToArray());bytes=$bytes;files=$files;entries=$entries;truncated=$truncated;errors=@($issues.ToArray())}
}
$startedUnixMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$watch=[Diagnostics.Stopwatch]::StartNew()
$known=@{};$samples=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[string]]::new()
$initialIdentity=$null
try{
    $root=Get-Process -Id $RootProcessId
    try{$initialIdentity=(Get-IdentityTicks $root.StartTime)}finally{$root.Dispose()}
}catch{$errors.Add('root-identity-unavailable: '+$_.Exception.GetType().Name)}
$peakWorking=[long]0;$peakPrivate=[long]0;$peakDisk=[long]0;$lastAlive=@();$stopReason='duration'
if($null -ne $initialIdentity){$known["$RootProcessId/$initialIdentity"]=[pscustomobject]@{id=$RootProcessId;ticks=$initialIdentity;parent=$null}}
while($null -ne $initialIdentity -and $watch.Elapsed.TotalSeconds -lt $Seconds -and $samples.Count -lt 15000){
    $sampleWatch=[Diagnostics.Stopwatch]::StartNew();$issues=[Collections.Generic.List[string]]::new();$rows=@();$queryOk=$true
    try{$rows=@(Get-CimInstance Win32_Process -Property ProcessId,ParentProcessId,CreationDate,Name)}catch{$queryOk=$false;$issues.Add('process-topology-query-failed: '+$_.Exception.GetType().Name)}
    $current=@{}
    foreach($row in $rows){
        try{$ticks=(Get-IdentityTicks $row.CreationDate);$key="$($row.ProcessId)/$ticks";$current[$key]=$row}catch{$issues.Add('process-identity-unavailable')}
    }
    # A parent identity must still be observed, and precede the child's birth.
    # This deliberately prefers omissions to attributing an unrelated PID reuse.
    do{
        $added=$false
        foreach($key in @($current.Keys)){
            if($known.ContainsKey($key)){continue};$row=$current[$key]
            foreach($parentKey in @($known.Keys)){
                $parent=$known[$parentKey]
                if($current.ContainsKey($parentKey) -and $parent.id -eq $row.ParentProcessId -and $parent.ticks -le (Get-IdentityTicks $row.CreationDate)){
                    $known[$key]=[pscustomobject]@{id=[int]$row.ProcessId;ticks=(Get-IdentityTicks $row.CreationDate);parent=$parentKey};$added=$true;break
                }
            }
        }
    }while($added)
    $alive=[Collections.Generic.List[string]]::new();$observed=[Collections.Generic.List[object]]::new();[long]$working=0;[long]$private=0;$complete=$queryOk
    foreach($key in @($known.Keys)){
        $identity=$known[$key];$process=$null
        try{
            $process=Get-Process -Id $identity.id -ErrorAction Stop
            if((Get-IdentityTicks $process.StartTime) -ne $identity.ticks){continue}
            $alive.Add($key);$ws=$process.WorkingSet64;$pm=$process.PrivateMemorySize64;$working+=$ws;$private+=$pm
            $observed.Add([pscustomobject]@{identity=$key;parent=$identity.parent;name=$process.ProcessName;workingSetBytes=$ws;privateBytes=$pm})
        }catch{
            # Topology says it existed, but memory cannot be confirmed: no zero.
            if($current.ContainsKey($key) -or -not $queryOk){$complete=$false;$issues.Add('process-memory-unavailable: '+$key);$alive.Add($key)}
        }finally{if($null -ne $process){$process.Dispose()}}
    }
    $disk=Get-DiskObservation
    if($null -ne $disk){$peakDisk=[Math]::Max($peakDisk,$disk.bytes)}
    $peakWorking=[Math]::Max($peakWorking,$working);$peakPrivate=[Math]::Max($peakPrivate,$private)
    $samples.Add([pscustomobject]@{elapsedMs=$watch.ElapsedMilliseconds;queryMs=$sampleWatch.ElapsedMilliseconds;memoryComplete=$complete;observedWorkingSetBytes=$working;observedPrivateBytes=$private;processes=@($observed.ToArray());disk=$disk;errors=@($issues.ToArray())})
    $lastAlive=@($alive.ToArray())
    if($queryOk -and $lastAlive.Count -eq 0){$stopReason='all-known-identities-ended';break}
    $remaining=$IntervalMs-$sampleWatch.ElapsedMilliseconds;if($remaining -gt 0){Start-Sleep -Milliseconds $remaining}
}
if($null -eq $initialIdentity){$stopReason='root-identity-unavailable'}elseif($samples.Count -ge 15000){$stopReason='sample-cap'}
$report=[ordered]@{schemaVersion=1;startedUnixMs=$startedUnixMs;rootProcessId=$RootProcessId;rootCreatedUtcTicks=$initialIdentity;intervalMs=$IntervalMs;elapsedMs=$watch.ElapsedMilliseconds;stopReason=$stopReason;sampleCount=$samples.Count;sampledPeakObservedWorkingSetBytes=$peakWorking;sampledPeakObservedPrivateBytes=$peakPrivate;sampledPeakDiskBytes=$peakDisk;remainingKnownAlive=$lastAlive;errors=@($errors.ToArray());limitations=@('Sampled observations, not continuous peaks.','Short-lived or already-orphaned descendants between samples can be missed.','Topology/identity failures remain explicit; partial totals are not complete tree totals.','Working set aggregation can count shared pages more than once.','Disk entry/link checks are observational, not protection against adversarial path races.');samples=@($samples.ToArray())}
$temporary=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
try{
    [IO.File]::WriteAllText($temporary,($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary,$target) # No replacement overload.
}finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
if($null -eq $initialIdentity){throw 'Root could not be identified; diagnostic report saved'}
