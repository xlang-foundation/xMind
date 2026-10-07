[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Runtime = "$PSScriptRoot\..\..\xlang3\build\Release\xlang3.exe",
    [string]$PythonLib = 'C:\Python\Python314\Lib',
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$AgentArguments
)
$projectRoot = (Resolve-Path "$PSScriptRoot\..").Path
if (-not (Test-Path -LiteralPath $Runtime)) { throw "Build xlang3 first: $Runtime" }
$savedPythonPath = $env:PYTHONPATH
$savedPythonLib = $env:XLANG3_PYTHON_LIB
try {
    $env:XLANG3_PYTHON_LIB = $PythonLib
    $env:PYTHONPATH = "$projectRoot;$projectRoot\.agentflow\site-packages"
    & $Runtime "$projectRoot\agentflow\main.py" @AgentArguments
    $result = $LASTEXITCODE
} finally {
    $env:PYTHONPATH = $savedPythonPath
    $env:XLANG3_PYTHON_LIB = $savedPythonLib
}
exit $result
