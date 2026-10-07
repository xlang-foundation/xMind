param(
    [string]$CodeExecutable,[string]$RuntimeDirectory,[string]$BundleDirectory,
    [string]$StdlibSource='C:\Python\Python314\Lib',
    [string]$Model,[string]$ModelEndpoint,[string]$SelectableModels,
    [string]$ProviderKeyEnvironment='OPENAI_API_KEY',
    [string]$CredentialId,
    [ValidateSet('unknown','unsupported','supported')][string]$ModelTools='unknown',
    [ValidateSet('unknown','unsupported','supported')][string]$StreamUsage='unknown',
    [string]$Workspace,[switch]$ApprovedEdits
)
$ErrorActionPreference='Stop'
if([bool]$Model -ne [bool]$ModelEndpoint){throw 'Provide both -Model and -ModelEndpoint.'}
if(-not $Model -and ($CredentialId -or $SelectableModels)){throw 'Startup credential references/model lists require -Model and -ModelEndpoint.'}
if($ApprovedEdits -and -not $Workspace){throw 'Approved file edits require a workspace.'}
if($Workspace -and $ModelTools -ne 'supported'){throw 'Workspace execution requires -ModelTools supported.'}
if($ProviderKeyEnvironment -notmatch '^[A-Za-z_][A-Za-z0-9_]*$'){throw 'Invalid provider key environment name.'}
if($ProviderKeyEnvironment -eq 'XMIND_AUTH_TOKEN' -or $ProviderKeyEnvironment -like 'XMIND_UI_*'){throw 'Select a provider credential variable, not a preview authentication variable.'}
foreach($uiArgument in @($CodeExecutable,$RuntimeDirectory,$BundleDirectory,$StdlibSource,$Model,$ModelEndpoint,$SelectableModels,$CredentialId,$Workspace)){
    if($uiArgument -and $uiArgument.IndexOfAny([char[]]@([char]0,[char]10,[char]13,[char]34)) -ge 0){throw 'Preview arguments cannot contain quotes or control characters.'}
}
$uiProject=Split-Path $PSScriptRoot -Parent
$uiState=Join-Path $uiProject '.agentflow\ui-host'
New-Item -ItemType Directory -Force -Path $uiState | Out-Null
if(-not $CodeExecutable) {$CodeExecutable=Join-Path $uiState 'vscode\Code.exe'}
if(-not (Test-Path -LiteralPath $CodeExecutable)) {throw 'Select an installed VS Code executable or unpack the official portable ZIP under .agentflow/ui-host/vscode.'}
$uiServer=Join-Path $uiProject 'build\native\Release\xmind_server.exe'
$uiBuildProvenance=$null
if($BundleDirectory){
    if($RuntimeDirectory){throw 'Select a complete native bundle or a runtime directory, not both.'}
    $uiBundle=(Resolve-Path -LiteralPath $BundleDirectory).Path
    $uiServer=Join-Path $uiBundle 'xmind_server.exe'
    $uiModules=Join-Path $uiBundle 'modules'
    foreach($uiRequired in @('xmind_server.exe','xmind_cli.exe','xlang3_runtime.dll','modules/xlang_json.x3pkg.dll','modules/xlang_sqlite3.x3pkg.dll','provenance.json')){
        if(-not(Test-Path -LiteralPath (Join-Path $uiBundle $uiRequired))){throw ('Incomplete native development bundle: '+$uiRequired)}
    }
    $uiBuildProvenance=Get-Content -LiteralPath (Join-Path $uiBundle 'provenance.json') -Raw|ConvertFrom-Json
}else{
    if(-not $RuntimeDirectory){$RuntimeDirectory=Join-Path (Split-Path $uiProject -Parent) 'xlang3/build/Release'}
    $uiModules=Join-Path $RuntimeDirectory 'modules'
}
if(-not (Test-Path -LiteralPath $uiServer)) {throw 'A verified native server build is required.'}
$uiAccess=[Convert]::ToHexString([System.Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$uiOriginalAuth=$env:XMIND_AUTH_TOKEN
$uiOriginalBootstrap=$env:XMIND_UI_BOOTSTRAP_TOKEN
$uiOriginalOrigin=$env:XMIND_UI_BACKEND_ORIGIN
$uiOriginalReady=$env:XMIND_UI_READY_FILE
$uiOriginalProviderKey=$env:XMIND_API_KEY
$uiEditorProviderEnvironment=[Environment]::GetEnvironmentVariable($ProviderKeyEnvironment,'Process')
$uiProcess=$null
try {
    $env:XMIND_AUTH_TOKEN=$uiAccess
    $uiDatabase=Join-Path $uiState 'sessions.sqlite'
    $uiLog=Join-Path $uiState 'backend.log'
    $uiErrorLog=Join-Path $uiState 'backend-error.log'
    $uiReady=Join-Path $uiState ('opened-'+[Guid]::NewGuid().ToString('N')+'.json')
    $uiArgs=@('--db',('"'+$uiDatabase+'"'),'--modules',('"'+$uiModules+'"'),'--stdlib',('"'+$StdlibSource+'"'),'--port','0')
    if($Workspace){$uiArgs+=@('--workspace',('"'+[System.IO.Path]::GetFullPath($Workspace)+'"'))}
    if($ApprovedEdits){$uiArgs+=@('--workspace-edits','approved')}
    if(-not $Model -and $Workspace){$uiArgs+=@('--model-tools',$ModelTools)}
    if($Model){
        $uiArgs+=@('--model',('"'+$Model+'"'),'--model-endpoint',('"'+$ModelEndpoint+'"'),'--model-tools',$ModelTools,'--model-stream-usage',$StreamUsage)
        if($SelectableModels){$uiArgs+=@('--models',('"'+$SelectableModels+'"'))}
        if($CredentialId){$uiArgs+=@('--credential-id',('"'+$CredentialId+'"'))}
        $uiProviderKey=[Environment]::GetEnvironmentVariable($ProviderKeyEnvironment,'Process')
        if(-not $uiProviderKey){$uiProviderKey=[Environment]::GetEnvironmentVariable($ProviderKeyEnvironment,'User')}
        if(-not $uiProviderKey){$uiProviderKey=[Environment]::GetEnvironmentVariable($ProviderKeyEnvironment,'Machine')}
        if($uiProviderKey){$env:XMIND_API_KEY=$uiProviderKey}
        elseif(-not $CredentialId){throw 'No provider key found. Set the selected provider environment variable privately, or provide an existing -CredentialId.'}
    }else{$env:XMIND_API_KEY=$null}
    if($ProviderKeyEnvironment -ne 'XMIND_API_KEY'){[Environment]::SetEnvironmentVariable($ProviderKeyEnvironment,$null,'Process')}
    $uiProcess=Start-Process -FilePath $uiServer -ArgumentList $uiArgs -WorkingDirectory $uiProject -WindowStyle Hidden -RedirectStandardOutput $uiLog -RedirectStandardError $uiErrorLog -PassThru
    # Only the native backend receives the provider key. It encrypts the key
    # through its credential repository; the editor must not inherit it.
    $env:XMIND_API_KEY=$null
    $uiProviderKey=$null
    [Environment]::SetEnvironmentVariable($ProviderKeyEnvironment,$null,'Process')
    $uiDeadline=[DateTime]::UtcNow.AddSeconds(20)
    $uiPort=$null
    while([DateTime]::UtcNow -lt $uiDeadline) {
        $uiProcess.Refresh()
        if($uiProcess.HasExited) {throw ('Native UI server exited: '+(Get-Content -LiteralPath $uiErrorLog -Raw))}
        $uiText=Get-Content -LiteralPath $uiLog -Raw -ErrorAction SilentlyContinue
        if($uiText -match 'listening on http://127\.0\.0\.1:(\d+)') {$uiPort=[int]$Matches[1];break}
        Start-Sleep -Milliseconds 100
    }
    if(-not $uiPort) {throw 'Native server readiness timed out.'}
    $uiOrigin='http://127.0.0.1:'+$uiPort
    $uiHealth=Invoke-RestMethod -Uri ($uiOrigin+'/v1/health') -Headers @{Authorization=('Bearer '+$uiAccess)}
    $env:XMIND_UI_BOOTSTRAP_TOKEN=$uiAccess
    $env:XMIND_UI_BACKEND_ORIGIN=$uiOrigin
    $env:XMIND_UI_READY_FILE=$uiReady
    # User explicitly requested a visible UI. This is an isolated development
    # host for this known repository, with its own settings/extensions directory.
    $uiCodeArgs=@('--new-window','--disable-workspace-trust','--skip-welcome','--remote-debugging-port=57217','--user-data-dir',('"'+(Join-Path $uiState 'profile')+'"'),'--extensions-dir',('"'+(Join-Path $uiState 'extensions')+'"'),('--extensionDevelopmentPath="'+(Join-Path $uiProject 'extensions\vscode')+'"'),('"'+$uiProject+'"'))
    $uiHost=Start-Process -FilePath $CodeExecutable -ArgumentList $uiCodeArgs -WorkingDirectory $uiProject -WindowStyle Normal -PassThru
    $uiMetadata=@{origin=$uiOrigin;backend_pid=$uiProcess.Id;host_launcher_pid=$uiHost.Id;ready_file=$uiReady;agent_execution=$uiHealth.agent_execution;model_configured=[bool]$uiHealth.agent_execution;server_executable=$uiServer;modules=$uiModules;source_revision=$uiBuildProvenance.xmind} | ConvertTo-Json
    [System.IO.File]::WriteAllText((Join-Path $uiState 'active.json'),$uiMetadata)
    $uiMetadata
} catch {
    if($uiProcess -and -not $uiProcess.HasExited) {$uiProcess.Kill()}
    throw
} finally {
    $env:XMIND_AUTH_TOKEN=$uiOriginalAuth
    $env:XMIND_UI_BOOTSTRAP_TOKEN=$uiOriginalBootstrap
    $env:XMIND_UI_BACKEND_ORIGIN=$uiOriginalOrigin
    $env:XMIND_UI_READY_FILE=$uiOriginalReady
    $env:XMIND_API_KEY=$uiOriginalProviderKey
    if($null -ne $uiEditorProviderEnvironment){[Environment]::SetEnvironmentVariable($ProviderKeyEnvironment,$uiEditorProviderEnvironment,'Process')}
}
