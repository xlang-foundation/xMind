param(
    [string]$CodeExecutable,[string]$RuntimeDirectory,[string]$BundleDirectory,[string]$ServerExecutable,
    [string]$StdlibSource='C:\Python\Python314\Lib',
    [string]$Model,[string]$ModelEndpoint,[string]$SelectableModels,
    [ValidateSet('chat-completions','responses')][string]$ModelWire='chat-completions',
    [string]$ProviderKeyEnvironment='OPENAI_API_KEY',
    [string]$CredentialId,
    [ValidateSet('unknown','unsupported','supported')][string]$ModelTools='unknown',
    [ValidateSet('unknown','unsupported','supported')][string]$StreamUsage='unknown',
    [string]$Workspace,[switch]$ApprovedEdits,
    [switch]$DisableWorkspaceTrust,
    [ValidatePattern('^[A-Za-z0-9_-]{1,64}$')][string]$PreviewName='ui-host',
    [ValidateRange(1024,65535)][int]$DebugPort=57217,
    [string]$GraphsConfig,
    [string]$ProviderConfig,
    [string]$LanguageServerConfig,
    [string]$ProcessConfig
)
$ErrorActionPreference='Stop'
if([bool]$Model -ne [bool]$ModelEndpoint){throw 'Provide both -Model and -ModelEndpoint.'}
if($ProviderConfig -and $Model){throw 'Provider YAML requires configurable provider profiles; omit -Model.'}
if(-not $Model -and $ModelWire -ne 'chat-completions'){throw 'A Responses startup wire requires -Model and -ModelEndpoint.'}
if(-not $Model -and ($CredentialId -or $SelectableModels)){throw 'Startup credential references/model lists require -Model and -ModelEndpoint.'}
if($ApprovedEdits -and -not $Workspace){throw 'Approved file edits require a workspace.'}
if($Workspace -and $ModelTools -ne 'supported'){throw 'Workspace execution requires -ModelTools supported.'}
if($ProviderKeyEnvironment -notmatch '^[A-Za-z_][A-Za-z0-9_]*$'){throw 'Invalid provider key environment name.'}
if($ProviderKeyEnvironment -eq 'XMIND_AUTH_TOKEN' -or $ProviderKeyEnvironment -like 'XMIND_UI_*'){throw 'Select a provider credential variable, not a preview authentication variable.'}
foreach($uiArgument in @($CodeExecutable,$RuntimeDirectory,$BundleDirectory,$ServerExecutable,$StdlibSource,$Model,$ModelEndpoint,$SelectableModels,$CredentialId,$Workspace,$GraphsConfig,$ProviderConfig,$LanguageServerConfig,$ProcessConfig)){
    if($uiArgument -and $uiArgument.IndexOfAny([char[]]@([char]0,[char]10,[char]13,[char]34)) -ge 0){throw 'Preview arguments cannot contain quotes or control characters.'}
}
$uiProject=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'xlang3-runtime.ps1')
$uiOpenWorkspace=$uiProject
if($Workspace){
    $uiOpenWorkspace=[System.IO.Path]::GetFullPath($Workspace)
    if(-not (Test-Path -LiteralPath $uiOpenWorkspace -PathType Container)){throw 'The selected UI workspace must be an existing folder.'}
}
$uiState=Join-Path (Join-Path $uiProject '.agentflow') $PreviewName
New-Item -ItemType Directory -Force -Path $uiState | Out-Null
if(-not $CodeExecutable) {$CodeExecutable=Join-Path $uiProject '.agentflow/ui-host/vscode/Code.exe'}
if(Test-Path -LiteralPath (Join-Path $uiState 'active.json')){
    $uiExisting=Get-Content -LiteralPath (Join-Path $uiState 'active.json') -Raw|ConvertFrom-Json
    if(Get-Process -Id $uiExisting.backend_pid -ErrorAction SilentlyContinue){throw 'This named preview already has a live backend. Reuse it or select another -PreviewName.'}
}
if(Get-NetTCPConnection -State Listen -LocalPort $DebugPort -ErrorAction SilentlyContinue){throw 'The selected development-host debug port is already in use.'}
if(-not (Test-Path -LiteralPath $CodeExecutable)) {throw 'Select an installed VS Code executable or unpack the official portable ZIP under .agentflow/ui-host/vscode.'}
$uiInstaller=@(Get-Process -ErrorAction SilentlyContinue|Where-Object {$_.ProcessName -like 'CodeSetup-*'})
if($uiInstaller.Count){$uiInstallerNames=($uiInstaller.ProcessName|Sort-Object -Unique)-join ', ';throw ('VS Code setup/update is still running ('+$uiInstallerNames+'). Finish the installer before starting an xMind development host.')}
$uiServer=if($ServerExecutable){[System.IO.Path]::GetFullPath($ServerExecutable)}else{Join-Path $uiProject 'build\native\contract-bin\Release\xmind.exe'}
$uiBuildProvenance=$null
if($BundleDirectory){
    if($RuntimeDirectory){throw 'Select a complete native bundle or a runtime directory, not both.'}
    $uiBundle=(Resolve-Path -LiteralPath $BundleDirectory).Path
    $uiServer=Join-Path $uiBundle 'xmind.exe'
    $uiModules=Join-Path $uiBundle 'modules'
    foreach($uiRequired in @('xmind.exe','xlang3_runtime.dll','modules/xlang_json.x3pkg.dll','modules/xlang_sqlite3.x3pkg.dll','provenance.json')){
        if(-not(Test-Path -LiteralPath (Join-Path $uiBundle $uiRequired))){throw ('Incomplete native development bundle: '+$uiRequired)}
    }
    $uiBuildProvenance=Get-Content -LiteralPath (Join-Path $uiBundle 'provenance.json') -Raw|ConvertFrom-Json
}else{
    $RuntimeDirectory=Get-XMindXlang3RuntimeDirectory -ProjectRoot $uiProject -RequestedRuntimeDirectory $RuntimeDirectory
    $uiModules=Join-Path $RuntimeDirectory 'modules'
}
if(-not (Test-Path -LiteralPath $uiServer)) {throw 'A verified native server build is required.'}
$uiSourceServer=$uiServer
if(-not $BundleDirectory){
    # Native development outputs may be rebuilt while this preview is open.
    # Load a private immutable snapshot instead of locking the build directory.
    $uiSnapshot=Join-Path $uiState ('runtime-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $uiSnapshot 'modules') -Force|Out-Null
    $uiRuntimeExecutable=Join-Path $RuntimeDirectory 'xlang3.exe'
    $uiServerRuntime=Join-Path (Split-Path $uiServer -Parent) 'xlang3_runtime.dll'
    $uiSdkRuntime=Join-Path $RuntimeDirectory 'xlang3_runtime.dll'
    foreach($uiRequired in @($uiRuntimeExecutable,$uiServerRuntime,$uiSdkRuntime,(Join-Path $uiModules 'xlang_json.x3pkg.dll'),(Join-Path $uiModules 'xlang_sqlite3.x3pkg.dll'))){
        if(-not(Test-Path -LiteralPath $uiRequired -PathType Leaf)){throw ('Incomplete paired xlang3 development runtime: '+$uiRequired)}
    }
    if((Get-FileHash -LiteralPath $uiServerRuntime -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $uiSdkRuntime -Algorithm SHA256).Hash){throw 'The native server and xlang3 executable use different runtime DLLs.'}
    foreach($uiBinary in @('xmind.exe','xlang3_runtime.dll')){
        Copy-Item -LiteralPath (Join-Path (Split-Path $uiServer -Parent) $uiBinary) -Destination (Join-Path $uiSnapshot $uiBinary)
    }
    Copy-Item -LiteralPath $uiRuntimeExecutable -Destination (Join-Path $uiSnapshot 'xlang3.exe')
    $uiRuntimeModuleNames=@('_argon2_cffi_bindings._ffi.x3pkg.dll','_brotli.x3pkg.dll','_cffi_backend.x3pkg.dll','_zstd.x3pkg.dll',
        'cryptography.hazmat.bindings._rust.x3pkg.dll','httptools.parser.parser.x3pkg.dll','httptools.parser.url_parser.x3pkg.dll','mmap.x3pkg.dll',
        'psutil._psutil_windows.x3pkg.dll','pydantic_core._pydantic_core.x3pkg.dll','rpds.rpds.x3pkg.dll','xlang__blake2.x3pkg.dll',
        'xlang__hashlib.x3pkg.dll','xlang__ssl.x3pkg.dll','xlang_device.x3pkg.dll','xlang_net.x3pkg.dll','xlang_yaml.x3pkg.dll','zstandard.backend_c.x3pkg.dll',
        'xlang_json.x3pkg.dll','xlang_sqlite3.x3pkg.dll')
    $uiSelectedModules=Get-ChildItem -LiteralPath $uiModules -Recurse -File|Where-Object {($_.Name -like '*.x3pkg.dll' -and $uiRuntimeModuleNames -contains $_.Name) -or ($_.Extension -eq '.py' -and $_.FullName -notmatch '[\\/]__pycache__[\\/]')}
    foreach($uiModule in $uiSelectedModules){
        $uiRelative=[System.IO.Path]::GetRelativePath($uiModules,$uiModule.FullName)
        $uiTarget=Join-Path (Join-Path $uiSnapshot 'modules') $uiRelative
        New-Item -ItemType Directory -Force -Path (Split-Path $uiTarget -Parent)|Out-Null
        Copy-Item -LiteralPath $uiModule.FullName -Destination $uiTarget
    }
    $uiServer=Join-Path $uiSnapshot 'xmind.exe';$uiModules=Join-Path $uiSnapshot 'modules'
}
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
    $uiArgs=@('serve','--db',('"'+$uiDatabase+'"'),'--modules',('"'+$uiModules+'"'),'--stdlib',('"'+$StdlibSource+'"'),'--port','0')
    if(-not $ProviderConfig -and -not $Model){
        $uiProviderTemplate=Join-Path $uiProject '.config/providers.yaml'
        if(Test-Path -LiteralPath $uiProviderTemplate -PathType Leaf){$ProviderConfig=$uiProviderTemplate}
    }
    if($ProviderConfig){$uiArgs+=@('--provider-config',('"'+[System.IO.Path]::GetFullPath($ProviderConfig)+'"'))}
    if(-not $LanguageServerConfig){
        $uiLanguageServerTemplate=Join-Path $uiProject '.config/language-servers.yaml'
        if(Test-Path -LiteralPath $uiLanguageServerTemplate -PathType Leaf){$LanguageServerConfig=$uiLanguageServerTemplate}
    }
    if($LanguageServerConfig){$uiArgs+=@('--language-server-config',('"'+[System.IO.Path]::GetFullPath($LanguageServerConfig)+'"'))}
    if(-not $ProcessConfig){
        $uiProcessTemplate=Join-Path $uiProject '.config/processes.yaml'
        if(Test-Path -LiteralPath $uiProcessTemplate -PathType Leaf){$ProcessConfig=$uiProcessTemplate}
    }
    if($ProcessConfig){$uiArgs+=@('--process-config',('"'+[System.IO.Path]::GetFullPath($ProcessConfig)+'"'))}
    if($Workspace){$uiArgs+=@('--workspace',('"'+[System.IO.Path]::GetFullPath($Workspace)+'"'))}
    if($ApprovedEdits){$uiArgs+=@('--workspace-edits','approved')}
    if($GraphsConfig){$uiArgs+=@('--graphs-config',('"'+[System.IO.Path]::GetFullPath($GraphsConfig)+'"'))}
    if(-not $Model -and $Workspace){$uiArgs+=@('--model-tools',$ModelTools)}
    if($Model){
        $uiArgs+=@('--model',('"'+$Model+'"'),'--model-endpoint',('"'+$ModelEndpoint+'"'),'--model-tools',$ModelTools,'--model-stream-usage',$StreamUsage)
        $uiArgs+=@('--model-wire',$ModelWire)
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
    $uiCodeArgs=@('--new-window','--skip-welcome','--skip-release-notes',('--remote-debugging-port='+$DebugPort),'--user-data-dir',('"'+(Join-Path $uiState 'profile')+'"'),'--extensions-dir',('"'+(Join-Path $uiState 'extensions')+'"'),('--extensionDevelopmentPath="'+(Join-Path $uiProject 'extensions\vscode')+'"'),('"'+$uiOpenWorkspace+'"'))
    if($DisableWorkspaceTrust){$uiCodeArgs+='--disable-workspace-trust'}
    $uiHost=Start-Process -FilePath $CodeExecutable -ArgumentList $uiCodeArgs -WorkingDirectory $uiProject -WindowStyle Normal -PassThru
    $uiSidebarDeadline=[DateTime]::UtcNow.AddSeconds(60)
    while([DateTime]::UtcNow -lt $uiSidebarDeadline -and -not(Test-Path -LiteralPath $uiReady)){
        Start-Sleep -Milliseconds 100
    }
    if(-not(Test-Path -LiteralPath $uiReady)){
        throw ('VS Code did not report a ready xMind secondary sidebar within 60 seconds. Inspect the Development Host for an activation error or workspace-trust prompt. Isolated smoke tests may use -DisableWorkspaceTrust. Ready marker: '+$uiReady)
    }
    $uiSidebar=Get-Content -LiteralPath $uiReady -Raw | ConvertFrom-Json
    if($uiSidebar.opened -ne $true -or $uiSidebar.location -ne 'secondarySidebar' -or $uiSidebar.origin -ne $uiOrigin){throw 'The VS Code readiness marker did not match the opened native xMind sidebar.'}
    $uiMetadata=@{origin=$uiOrigin;backend_pid=$uiProcess.Id;host_launcher_pid=$uiHost.Id;ready_file=$uiReady;agent_execution=$uiHealth.agent_execution;model_configured=[bool]$uiHealth.agent_execution;server_executable=$uiServer;source_server_executable=$uiSourceServer;server_sha256=(Get-FileHash -LiteralPath $uiServer -Algorithm SHA256).Hash;modules=$uiModules;source_revision=$uiBuildProvenance.xmind;preview_name=$PreviewName;debug_port=$DebugPort;graphs_config=$GraphsConfig;language_server_config=$LanguageServerConfig} | ConvertTo-Json
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
