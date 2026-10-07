param([string]$CodeExecutable)
$ErrorActionPreference='Stop'
$reviewRoot=Split-Path $PSScriptRoot -Parent
$reviewState=Join-Path $reviewRoot '.agentflow\ui-host'
if(-not $CodeExecutable){$CodeExecutable=Join-Path $reviewState 'vscode\Code.exe'}
if(-not(Test-Path -LiteralPath $CodeExecutable)){throw 'Select an installed VS Code executable.'}
$reviewProfile=Join-Path $reviewState ('review-contract-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $reviewProfile -Force | Out-Null
$reviewResult=Join-Path $reviewProfile 'result.json'
$reviewOldResult=$env:XMIND_REVIEW_TEST_RESULT
try {
    $env:XMIND_REVIEW_TEST_RESULT=$reviewResult
    $reviewArgs=@('--new-window','--disable-workspace-trust','--skip-welcome','--skip-release-notes','--disable-extensions',('--user-data-dir="'+$reviewProfile+'"'),('--extensions-dir="'+(Join-Path $reviewProfile 'extensions')+'"'),('--extensionDevelopmentPath="'+(Join-Path $reviewRoot 'extensions\vscode')+'"'),('--extensionTestsPath="'+(Join-Path $PSScriptRoot 'vscode-review-contract.cjs')+'"'))
    $reviewProcess=Start-Process -FilePath $CodeExecutable -ArgumentList $reviewArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $reviewProfile 'stdout.log') -RedirectStandardError (Join-Path $reviewProfile 'stderr.log')
    $reviewDeadline=[DateTime]::UtcNow.AddSeconds(45)
    while([DateTime]::UtcNow -lt $reviewDeadline -and -not(Test-Path -LiteralPath $reviewResult)){
        $reviewProcess.Refresh()
        if($reviewProcess.HasExited){break}
        Start-Sleep -Milliseconds 100
    }
    if(-not(Test-Path -LiteralPath $reviewResult)){
        Get-Content -LiteralPath (Join-Path $reviewProfile 'stdout.log') -ErrorAction SilentlyContinue
        Get-Content -LiteralPath (Join-Path $reviewProfile 'stderr.log') -ErrorAction SilentlyContinue
        throw ('Actual VS Code review contract produced no success evidence. Logs: '+$reviewProfile)
    }
    $reviewEvidence=Get-Content -LiteralPath $reviewResult -Raw
    if(-not($reviewEvidence|ConvertFrom-Json).passed){throw 'Actual VS Code contract failed.'}
    [System.IO.File]::WriteAllText((Join-Path $reviewRoot 'doc\evidence\vscode-native-diff.json'),$reviewEvidence)
    $reviewEvidence
} finally {$env:XMIND_REVIEW_TEST_RESULT=$reviewOldResult}
