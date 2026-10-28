param(
    [string]$Workspace=(Get-Location).Path,
    [string]$RuntimeDirectory,
    [string]$Assets,
    [string]$Packages,
    [string]$ProfileRoot,
    [string]$ProviderConfig,
    [string]$McpConfig,
    [switch]$UpgradeBackend
)
$ErrorActionPreference='Stop'
$browserProject=Split-Path $PSScriptRoot -Parent
if(-not $RuntimeDirectory){$RuntimeDirectory=Join-Path $browserProject 'extensions/vscode/native-runtime'}
if(-not $Assets){$Assets=Join-Path $browserProject 'extensions/vscode/browser-runtime/assets'}
$browserExecutable=Join-Path ([System.IO.Path]::GetFullPath($RuntimeDirectory)) 'xmind.exe'
if(-not(Test-Path -LiteralPath $browserExecutable -PathType Leaf)){throw 'Select a verified native xMind runtime with -RuntimeDirectory.'}
if(-not(Test-Path -LiteralPath (Join-Path $Assets 'index.html') -PathType Leaf)){throw 'Select prebuilt browser assets with -Assets.'}
$browserArguments=@('web','--workspace',[System.IO.Path]::GetFullPath($Workspace),'--assets',[System.IO.Path]::GetFullPath($Assets))
if($ProfileRoot){$browserArguments+=@('--profile-root',[System.IO.Path]::GetFullPath($ProfileRoot))}
if($Packages){$browserArguments+=@('--packages',[System.IO.Path]::GetFullPath($Packages))}
if($ProviderConfig){$browserArguments+=@('--config',[System.IO.Path]::GetFullPath($ProviderConfig))}
if($McpConfig){$browserArguments+=@('--mcp-config',[System.IO.Path]::GetFullPath($McpConfig))}
if($UpgradeBackend){$browserArguments+='--upgrade-backend'}
& $browserExecutable @browserArguments
if($LASTEXITCODE -ne 0){throw 'Native browser startup failed; its profile and diagnostics were retained.'}
