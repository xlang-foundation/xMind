function Get-XMindXlang3RuntimeDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [string]$RequestedRuntimeDirectory
    )

    if($RequestedRuntimeDirectory){return [System.IO.Path]::GetFullPath($RequestedRuntimeDirectory)}
    $cache=Join-Path $ProjectRoot 'build/native/CMakeCache.txt'
    if(Test-Path -LiteralPath $cache -PathType Leaf){
        $entry=Select-String -LiteralPath $cache -Pattern '^AGENTFLOW_XLANG3_RUNTIME_DIR:[^=]*=(.+)$' | Select-Object -First 1
        if($entry){
            $paired=$entry.Matches[0].Groups[1].Value.Trim()
            if($paired -and (Test-Path -LiteralPath (Join-Path $paired 'xlang3.exe') -PathType Leaf)){
                return [System.IO.Path]::GetFullPath($paired)
            }
        }
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Split-Path $ProjectRoot -Parent) 'xlang3/build/Release'))
}
