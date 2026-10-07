[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Build','Serve','Client','Chat')][string]$Action='Client',
    [string]$BinaryDirectory,
    [string]$Session,
    [string]$Database,
    [int]$Port=8765,
    [string]$RuntimeDirectory="$PSScriptRoot\..\..\xlang3\build\Release",
    [string]$PythonLibSource='C:\Python\Python314\Lib',
    [string]$Model,
    [string]$SelectableModels,
    [ValidateSet('unknown','unsupported','supported')][string]$StreamUsage='unknown',
    [string]$ModelEndpoint,
    [ValidateSet('chat-completions','responses')][string]$ModelWire='chat-completions',
    [string]$Workspace,
    [string]$InspectionWorkspace,
    [switch]$ApprovedEdits,
    [string]$CredentialId,
    [string]$ProcessConfig,
    [string]$McpConfig,
    [string]$InstructionsConfig,
    [string]$GraphsConfig,
    [ValidateSet('unknown','unsupported','supported')][string]$ModelTools='unknown',
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$ClientArguments
)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot\..").Path
if($Action -eq 'Build') {
    & "$PSScriptRoot\native-milestone.ps1" -Action Build -RuntimeDirectory $RuntimeDirectory -PythonLibSource $PythonLibSource
    exit $LASTEXITCODE
}
if($Port -lt 0 -or $Port -gt 65535 -or ($Action -in @('Client','Chat') -and $Port -eq 0)) {throw 'Invalid port.'}
if($Action -eq 'Serve' -and $ModelWire -eq 'responses' -and (-not $Model -or -not $ModelEndpoint)){throw 'Responses startup requires -Model and -ModelEndpoint.'}
if(-not $BinaryDirectory){$BinaryDirectory=Join-Path $projectRoot 'build\native\Release'}
$binary=Join-Path ([System.IO.Path]::GetFullPath($BinaryDirectory)) $(if($Action -eq 'Serve') {'xmind_server.exe'} else {'xmind_cli.exe'})
if(-not (Test-Path -LiteralPath $binary)) {throw 'Build the native xMind targets first with -Action Build.'}
if($Action -eq 'Serve') {
    if(-not $Database) {$Database=Join-Path $projectRoot '.agentflow\native\state.sqlite'}
    $Database=[System.IO.Path]::GetFullPath($Database)
    New-Item -ItemType Directory -Force -Path (Split-Path $Database -Parent) | Out-Null
    $serverArguments=@('--db',$Database,'--modules',(Join-Path $RuntimeDirectory 'modules'),'--stdlib',$PythonLibSource,'--port',"$Port")
    if($Model) {$serverArguments+=@('--model',$Model)}
    if($Model -or $Workspace) {$serverArguments+=@('--model-tools',$ModelTools)}
    if($SelectableModels) {$serverArguments+=@('--models',$SelectableModels)}
    if($Model) {$serverArguments+=@('--model-stream-usage',$StreamUsage)}
    if($ModelEndpoint) {$serverArguments+=@('--model-endpoint',$ModelEndpoint)}
    if($Model) {$serverArguments+=@('--model-wire',$ModelWire)}
    if($Workspace) {$serverArguments+=@('--workspace',$Workspace)}
    if($InspectionWorkspace) {$serverArguments+=@('--inspection-workspace',$InspectionWorkspace)}
    if($ApprovedEdits) {$serverArguments+=@('--workspace-edits','approved')}
    if($CredentialId) {$serverArguments+=@('--credential-id',$CredentialId)}
    if($ProcessConfig) {$serverArguments+=@('--process-config',$ProcessConfig)}
    if($McpConfig) {$serverArguments+=@('--mcp-config',$McpConfig)}
    if($InstructionsConfig) {$serverArguments+=@('--instructions-config',$InstructionsConfig)}
    if($GraphsConfig) {$serverArguments+=@('--graphs-config',$GraphsConfig)}
    & $binary @serverArguments
} elseif($Action -eq 'Chat') {
    if($ClientArguments.Count -gt 0){throw 'Use -Session and -Model for Chat, or -Action Client for raw commands.'}
    if($Model -and -not $Session){throw 'An initial -Model requires -Session. For a new chat, choose /model after entering chat.'}
    $chatArguments=@('chat')
    if($Session){$chatArguments+=$Session}
    if($Model){$chatArguments+=$Model}
    & $binary $Port @chatArguments
} else {
    & $binary $Port @ClientArguments
}
exit $LASTEXITCODE
