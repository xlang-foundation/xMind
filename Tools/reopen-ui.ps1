$ErrorActionPreference='Stop'
$sidebarRoot=Split-Path $PSScriptRoot -Parent
$sidebarRuntime=Join-Path $sidebarRoot '.agentflow\ui-host'
$sidebarCode=Join-Path $sidebarRuntime 'vscode\Code.exe'
$sidebarMetadata=Get-Content -LiteralPath (Join-Path $sidebarRuntime 'active.json') -Raw | ConvertFrom-Json
$env:XMIND_UI_BACKEND_ORIGIN=$sidebarMetadata.origin
$env:XMIND_UI_READY_FILE=Join-Path $sidebarRuntime ('sidebar-opened-'+[Guid]::NewGuid().ToString('N')+'.json')
try {
    $sidebarArguments=@('--new-window','--disable-workspace-trust','--skip-welcome','--remote-debugging-port=57217',('--user-data-dir="'+(Join-Path $sidebarRuntime 'profile')+'"'),('--extensions-dir="'+(Join-Path $sidebarRuntime 'extensions')+'"'),('--extensionDevelopmentPath="'+(Join-Path $sidebarRoot 'extensions\vscode')+'"'),('"'+$sidebarRoot+'"'))
    $sidebarProcess=Start-Process -FilePath $sidebarCode -ArgumentList $sidebarArguments -WorkingDirectory $sidebarRoot -WindowStyle Normal -PassThru
    $sidebarMetadata.host_launcher_pid=$sidebarProcess.Id;$sidebarMetadata.ready_file=$env:XMIND_UI_READY_FILE
    [System.IO.File]::WriteAllText((Join-Path $sidebarRuntime 'active.json'),($sidebarMetadata|ConvertTo-Json))
    $sidebarProcess|Select-Object Id
} finally {$env:XMIND_UI_BACKEND_ORIGIN=$null;$env:XMIND_UI_READY_FILE=$null}
