[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Build','Serve','Client')][string]$Action='Client',
    [string]$Database,
    [int]$Port=8765,
    [string]$RuntimeDirectory="$PSScriptRoot\..\..\xlang3\build\Release",
    [string]$PythonLibSource='C:\Python\Python314\Lib',
    [string]$Model,
    [string]$SelectableModels,
    [ValidateSet('unknown','unsupported','supported')][string]$StreamUsage='unknown',
    [string]$ModelEndpoint,
    [string]$Workspace,
    [string]$InspectionWorkspace,
    [switch]$ApprovedEdits,
    [string]$CredentialId,
    [ValidateSet('unknown','unsupported','supported')][string]$ModelTools='unknown',
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$ClientArguments
)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot\..").Path
if($Action -eq 'Build') {
    & "$PSScriptRoot\native-milestone.ps1" -Action Build -RuntimeDirectory $RuntimeDirectory -PythonLibSource $PythonLibSource
    exit $LASTEXITCODE
}
if($Port -lt 0 -or $Port -gt 65535 -or ($Action -eq 'Client' -and $Port -eq 0)) {throw 'Invalid port.'}
$binary=Join-Path $projectRoot ('build\native\Release\'+$(if($Action -eq 'Serve') {'xmind_server.exe'} else {'xmind_cli.exe'}))
if(-not (Test-Path -LiteralPath $binary)) {throw 'Build the native xMind targets first with -Action Build.'}
if($Action -eq 'Serve') {
    if(-not $Database) {$Database=Join-Path $projectRoot '.agentflow\native\state.sqlite'}
    $Database=[System.IO.Path]::GetFullPath($Database)
    New-Item -ItemType Directory -Force -Path (Split-Path $Database -Parent) | Out-Null
    $serverArguments=@('--db',$Database,'--modules',(Join-Path $RuntimeDirectory 'modules'),'--stdlib',$PythonLibSource,'--port',"$Port")
    if($Model) {$serverArguments+=@('--model',$Model,'--model-tools',$ModelTools)}
    if($SelectableModels) {$serverArguments+=@('--models',$SelectableModels)}
    if($Model) {$serverArguments+=@('--model-stream-usage',$StreamUsage)}
    if($ModelEndpoint) {$serverArguments+=@('--model-endpoint',$ModelEndpoint)}
    if($Workspace) {$serverArguments+=@('--workspace',$Workspace)}
    if($InspectionWorkspace) {$serverArguments+=@('--inspection-workspace',$InspectionWorkspace)}
    if($ApprovedEdits) {$serverArguments+=@('--workspace-edits','approved')}
    if($CredentialId) {$serverArguments+=@('--credential-id',$CredentialId)}
    & $binary @serverArguments
} else {
    & $binary $Port @ClientArguments
}
exit $LASTEXITCODE
