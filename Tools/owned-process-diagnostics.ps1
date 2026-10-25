param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$OwnerProcessId,
    [Parameter(Mandatory)][string]$OwnerExecutable,
    [Parameter(Mandatory)][datetime]$OwnerLaunchedAfterUtc,
    [Parameter(Mandatory)][string]$OutputFile,
    [Parameter(Mandatory)][string]$StopFile,
    [ValidateRange(200,5000)][int]$IntervalMs=2000,
    [ValidateRange(1,70)][int]$DurationSeconds=65,
    [switch]$CaptureStacks,
    [ValidateRange(1000,25000)][int]$StackCaptureAgeMs=15000
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'process-module-observation.ps1')
$encoding=[Text.UTF8Encoding]::new($false)
$timer=[Diagnostics.Stopwatch]::StartNew()
$samples=0
$ownerBirth=$null
$seen=@{}
$dumped=@{}
if($CaptureStacks){
    # Separate-process MiniDumpNormal: thread contexts/stacks, no full-memory flag.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class XMindOwnedStackDump {
 [DllImport("dbghelp.dll", SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool MiniDumpWriteDump(IntPtr process, uint pid, IntPtr file, uint type, IntPtr exception, IntPtr streams, IntPtr callback);
}
'@
}
function Record($value){
    if((Get-Item -LiteralPath $OutputFile).Length -gt 2097152){throw 'Diagnostic output limit reached'}
    [IO.File]::AppendAllText($OutputFile,($value|ConvertTo-Json -Depth 8 -Compress)+[Environment]::NewLine,$encoding)
}
$stream=[IO.File]::Open($OutputFile,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$stream.Dispose()
Record @{kind='scope';ownerPid=$OwnerProcessId;ownerImage=$OwnerExecutable;readOnly=$true;commandLinesIncluded=$false;environmentIncluded=$false;elapsedMsScope='observer';moduleNameLimit=64;threadLimit=128}
try {
    while($timer.Elapsed.TotalSeconds -lt $DurationSeconds -and -not(Test-Path -LiteralPath $StopFile)){
        # Retrieve only identity/ancestry fields, never command lines or environment.
        $all=@(Get-CimInstance Win32_Process -Property ProcessId,ParentProcessId,CreationDate,Name,ExecutablePath -OperationTimeoutSec 2)
        $root=@($all|Where-Object ProcessId -eq $OwnerProcessId)
        if($root.Count -ne 1){Record @{kind='end';reason='owner_exited';samples=$samples};exit 0}
        $birth=$root[0].CreationDate.ToUniversalTime()
        if(-not [String]::Equals($root[0].ExecutablePath,$OwnerExecutable,[StringComparison]::OrdinalIgnoreCase) -or $birth -lt $OwnerLaunchedAfterUtc.ToUniversalTime()){
            Record @{kind='end';reason='owner_identity_mismatch';samples=$samples};exit 0
        }
        if($null -eq $ownerBirth){$ownerBirth=$birth}
        if($birth -ne $ownerBirth){Record @{kind='end';reason='owner_identity_changed';samples=$samples};exit 0}
        $family=@{};$family[$OwnerProcessId]=$birth
        for($depth=0;$depth -lt 8;$depth++){
            $added=0
            foreach($candidate in $all){
                $id=[int]$candidate.ProcessId;$parentId=[int]$candidate.ParentProcessId
                if(-not $family.ContainsKey($id) -and $family.ContainsKey($parentId) -and $candidate.CreationDate.ToUniversalTime() -ge $family[$parentId]){
                    if($family.Count -ge 256){throw 'Owned diagnostic family exceeds limit'}
                    $family[$id]=$candidate.CreationDate.ToUniversalTime();$added++
                }
            }
            if(-not $added){break}
        }
        foreach($candidate in $all){
            $id=[int]$candidate.ProcessId
            if($id -eq $OwnerProcessId -or -not $family.ContainsKey($id) -or $candidate.Name -notin @('powershell.exe','pwsh.exe')){continue}
            $key=[string]$id+':'+$candidate.CreationDate.ToUniversalTime().Ticks
            $needsDump=$CaptureStacks -and $dumped.Count -lt 2 -and -not $dumped.ContainsKey($key) -and ([DateTime]::UtcNow-$candidate.CreationDate.ToUniversalTime()).TotalMilliseconds -ge $StackCaptureAgeMs
            if(-not $needsDump -and $seen.ContainsKey($key) -and ($seen[$key].count -ge 3 -or $timer.ElapsedMilliseconds -lt $seen[$key].nextAt)){continue}
            if(-not $seen.ContainsKey($key) -and $seen.Count -ge 16){continue}
            $process=$null
            try {
                $process=[Diagnostics.Process]::GetProcessById($id)
                if([Math]::Abs(($process.StartTime.ToUniversalTime()-$candidate.CreationDate.ToUniversalTime()).TotalMilliseconds) -gt 1 -or -not [String]::Equals($process.MainModule.FileName,$candidate.ExecutablePath,[StringComparison]::OrdinalIgnoreCase)){continue}
                if($needsDump){
                    $dumped[$key]=$true
                    $dumpName='owned-'+$id+'-'+$candidate.CreationDate.ToUniversalTime().Ticks+'.dmp'
                    $dumpFile=[IO.File]::Open((Join-Path ([IO.Path]::GetDirectoryName($OutputFile)) $dumpName),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
                    try{$ok=[XMindOwnedStackDump]::MiniDumpWriteDump($process.Handle,[uint32]$id,$dumpFile.SafeFileHandle.DangerousGetHandle(),0,[IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero);$dumpError=[Runtime.InteropServices.Marshal]::GetLastWin32Error()}
                    finally{$dumpFile.Dispose()}
                    Record @{kind='stack_dump';pid=$id;createdUtc=$candidate.CreationDate.ToUniversalTime().ToString('o');file=$dumpName;success=$ok;windowsError=$(if($ok){0}else{$dumpError});dumpType='MiniDumpNormal';fullMemory=$false}
                }
                $threads=@($process.Threads|Select-Object -First 128|ForEach-Object {
                    $thread=$_
                    try {$state=[string]$thread.ThreadState;$reason=if($state -eq 'Wait'){[string]$thread.WaitReason}else{$null};@{id=$thread.Id;state=$state;waitReason=$reason;cpuMs=$thread.TotalProcessorTime.TotalMilliseconds}}catch{@{id=$thread.Id;unavailable=$true}}
                })
                $moduleNames=@($process.Modules|ForEach-Object ModuleName)
                $moduleObservation=Get-ProcessModuleObservation $moduleNames
                $sampledUtc=[DateTime]::UtcNow
                Record @{kind='process';pid=$id;parentPid=[int]$candidate.ParentProcessId;createdUtc=$candidate.CreationDate.ToUniversalTime().ToString('o');sampledUtc=$sampledUtc.ToString('o');processAgeMs=[int64]($sampledUtc-$candidate.CreationDate.ToUniversalTime()).TotalMilliseconds;image=$candidate.ExecutablePath;elapsedMs=[int64]$timer.ElapsedMilliseconds;cpuMs=$process.TotalProcessorTime.TotalMilliseconds;threads=$threads;modules=$moduleObservation.modules;moduleCount=$moduleObservation.moduleCount;modulesTruncated=$moduleObservation.modulesTruncated;modulePresence=$moduleObservation.modulePresence}
                $samples++;$priorCount=if($seen.ContainsKey($key)){$seen[$key].count}else{0}
                $seen[$key]=@{count=1+$priorCount;nextAt=$timer.ElapsedMilliseconds+10000}
            }catch{Record @{kind='unavailable';pid=$id;errorType=$_.Exception.GetType().Name}}finally{if($null -ne $process){$process.Dispose()}}
        }
        $until=$timer.ElapsedMilliseconds+$IntervalMs
        while($timer.ElapsedMilliseconds -lt $until -and -not(Test-Path -LiteralPath $StopFile)){Start-Sleep -Milliseconds 100}
    }
    Record @{kind='end';reason=$(if(Test-Path -LiteralPath $StopFile){'observer_stopped'}else{'observation_budget'});samples=$samples}
}catch{
    Record @{kind='diagnostic_error';errorType=$_.Exception.GetType().Name;samples=$samples}
    exit 1
}
