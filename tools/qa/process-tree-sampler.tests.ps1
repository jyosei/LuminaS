# Opt-in synthetic QA. Kills only the two processes this test explicitly owns.
$ErrorActionPreference='Stop'
$sampler=Join-Path $PSScriptRoot 'process-tree-sampler.ps1'
$engine=Join-Path $PSHOME 'pwsh.exe';if(-not (Test-Path -LiteralPath $engine)){$engine=Join-Path $PSHOME 'powershell.exe'}
$root=Join-Path ([IO.Path]::GetTempPath()) ('luminas-sampler-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($root)
$holder=$null;$childIdentity=$null;$summary=$null
function Assert-True($condition,[string]$message){if(-not $condition){throw $message}}
function Assert-Rejected([scriptblock]$action,[string]$message){$rejected=$false;try{& $action}catch{$rejected=$true};Assert-True $rejected $message}
try{
    $childCode='$allocation=New-Object byte[] 8388608;for($i=0;$i -lt $allocation.Length;$i+=4096){$allocation[$i]=1};Start-Sleep -Seconds 25'
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($childCode))
    $holderCode=@'
param([string]$Engine,[string]$Encoded,[string]$Marker)
$ErrorActionPreference='Stop'
$allocation=New-Object byte[] 4194304
for($i=0;$i -lt $allocation.Length;$i+=4096){$allocation[$i]=1}
$child=Start-Process -FilePath $Engine -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$Encoded) -WindowStyle Hidden -PassThru
@{id=$child.Id;ticks=$child.StartTime.ToUniversalTime().Ticks}|ConvertTo-Json|Set-Content -LiteralPath $Marker
Start-Sleep -Seconds 25
'@
    $script=Join-Path $root 'holder.ps1';$marker=Join-Path $root 'child.json';$report=Join-Path $root 'report.json'
    [IO.File]::WriteAllText($script,$holderCode,[Text.UTF8Encoding]::new($false))
    # Quoted arguments are generated local paths; no command string/shell.
    $holder=Start-Process -FilePath $engine -ArgumentList @('-NoProfile','-NonInteractive','-File',('"'+$script+'"'),'-Engine',('"'+$engine+'"'),'-Encoded',$encoded,'-Marker',('"'+$marker+'"')) -WindowStyle Hidden -PassThru
    $holderTicks=$holder.StartTime.ToUniversalTime().Ticks
    $wait=[Diagnostics.Stopwatch]::StartNew();while(-not [IO.File]::Exists($marker) -and $wait.Elapsed.TotalSeconds -lt 10){Start-Sleep -Milliseconds 50}
    Assert-True ([IO.File]::Exists($marker)) 'Synthetic child did not start'
    $childIdentity=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json
    & $sampler -RootProcessId $holder.Id -ReportPath $report -Seconds 4 -IntervalMs 500 -OwnedDiskRoot $root
    $data=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    Assert-True ($data.schemaVersion -eq 1 -and $data.sampleCount -gt 0) 'No samples'
    $withChild=@($data.samples|Where-Object {@($_.processes|Where-Object {$_.identity -like "$($childIdentity.id)/*"}).Count -gt 0})
    Assert-True ($withChild.Count -gt 0) 'Owned child was not observed'
    $complete=@($data.samples|Where-Object {$_.memoryComplete})
    Assert-True ($complete.Count -gt 0) 'No complete memory sample'
    foreach($sample in $complete){
        [long]$sum=0;foreach($process in $sample.processes){Assert-True ($process.privateBytes -gt 0) 'Invalid process memory';$sum+=$process.privateBytes}
        Assert-True ($sum -eq $sample.observedPrivateBytes) 'Tree private-memory sum differs'
        Assert-True ($sample.disk.files -ge 2 -and $sample.disk.bytes -gt 0) 'Owned disk count missing'
    }
    $original=[IO.File]::ReadAllBytes($report)
    Assert-Rejected {& $sampler -RootProcessId $holder.Id -ReportPath $report -Seconds 1} 'Existing report was accepted'
    Assert-True ([Convert]::ToBase64String([IO.File]::ReadAllBytes($report)) -eq [Convert]::ToBase64String($original)) 'Existing report changed'
    Assert-Rejected {& $sampler -RootProcessId 0 -ReportPath (Join-Path $root 'bad.json')} 'Invalid PID was accepted'
    Assert-Rejected {& $sampler -RootProcessId 2147483647 -ReportPath (Join-Path $root 'missing.json') -Seconds 1} 'Missing process was accepted'
    $missing=Get-Content -LiteralPath (Join-Path $root 'missing.json') -Raw|ConvertFrom-Json
    Assert-True ($missing.stopReason -eq 'root-identity-unavailable' -and $missing.sampleCount -eq 0 -and $missing.errors.Count -gt 0) 'Missing process diagnostic was suppressed'
    $summary=[pscustomobject]@{passed=$true;sampleCount=$data.sampleCount;childSamples=$withChild.Count;peakObservedPrivateBytes=$data.sampledPeakObservedPrivateBytes;scope='synthetic sampler correctness; not application performance'}
}finally{
    if($null -ne $childIdentity){try{$owned=Get-Process -Id $childIdentity.id -ErrorAction Stop;if($owned.StartTime.ToUniversalTime().Ticks -eq $childIdentity.ticks){$owned.Kill();$owned.WaitForExit(5000)|Out-Null};$owned.Dispose()}catch{Write-Warning 'Owned child cleanup unavailable'}}
    if($null -ne $holder){if(-not $holder.HasExited -and $holder.StartTime.ToUniversalTime().Ticks -eq $holderTicks){$holder.Kill();$holder.WaitForExit(5000)|Out-Null};$holder.Dispose()}
    # Fixed files only; unknown entries keep the owned directory for inspection.
    foreach($name in @('holder.ps1','child.json','report.json','missing.json','bad.json')){$file=Join-Path $root $name;if([IO.File]::Exists($file)){[IO.File]::Delete($file)}}
    try{[IO.Directory]::Delete($root)}catch{Write-Warning ('Owned test directory retained: '+$root)}
}
$summary|ConvertTo-Json
