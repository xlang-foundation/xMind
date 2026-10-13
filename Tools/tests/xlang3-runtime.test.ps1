$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'xlang3-runtime.ps1')
$parent=[System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$root=Join-Path $parent ('xmind-runtime-selection-'+[Guid]::NewGuid().ToString('N'))
$project=Join-Path $root 'xMind'
$sibling=Join-Path $root 'xlang3/build/Release'
$paired=Join-Path $root 'xlang3/build-pytest-pathaware/Release'
function require-path([string]$Actual,[string]$Expected,[string]$Label){
    if(-not [string]::Equals([System.IO.Path]::GetFullPath($Actual),[System.IO.Path]::GetFullPath($Expected),[System.StringComparison]::OrdinalIgnoreCase)){
        throw "${Label}: expected '$Expected', got '$Actual'."
    }
}
try{
    New-Item -ItemType Directory -Force -Path (Join-Path $project 'build/native'),$sibling,$paired|Out-Null
    Set-Content -LiteralPath (Join-Path $sibling 'xlang3.exe') -Value 'stale fixture'
    Set-Content -LiteralPath (Join-Path $paired 'xlang3.exe') -Value 'paired fixture'
    Set-Content -LiteralPath (Join-Path $project 'build/native/CMakeCache.txt') -Value @(
        'CMAKE_GENERATOR:INTERNAL=Visual Studio',
        "AGENTFLOW_XLANG3_RUNTIME_DIR:PATH=$paired",
        'AGENTFLOW_PYTHON_LIB_SOURCE:PATH=C:/Python/Python314/Lib'
    )
    require-path (Get-XMindXlang3RuntimeDirectory -ProjectRoot $project) $paired 'Configured build runtime takes precedence over stale sibling runtime'
    require-path (Get-XMindXlang3RuntimeDirectory -ProjectRoot $project -RequestedRuntimeDirectory $sibling) $sibling 'Explicit runtime override takes precedence'
    Remove-Item -LiteralPath (Join-Path $paired 'xlang3.exe')
    require-path (Get-XMindXlang3RuntimeDirectory -ProjectRoot $project) $sibling 'Missing paired executable uses the documented sibling fallback'
    Remove-Item -LiteralPath (Join-Path $project 'build/native/CMakeCache.txt')
    require-path (Get-XMindXlang3RuntimeDirectory -ProjectRoot $project) $sibling 'Fresh checkout uses the documented sibling fallback'
    'xMind xlang3 runtime selection passed configured-cache, explicit-override, missing-binary and fresh-checkout cases.'
}finally{
    $resolved=[System.IO.Path]::GetFullPath($root)
    if([System.IO.Path]::GetDirectoryName($resolved).TrimEnd('\') -ne $parent.TrimEnd('\') -or [System.IO.Path]::GetFileName($resolved) -notlike 'xmind-runtime-selection-*'){
        throw 'Refusing to clean an unverified runtime-selection fixture path.'
    }
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
