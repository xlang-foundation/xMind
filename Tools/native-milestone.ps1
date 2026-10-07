param(
    [ValidateSet('Build','Seed','Read')][string]$Action='Build',
    [string]$Database,
    [string]$RuntimeDirectory='D:\CantorAI2026\xlang3\build\Release',
    [string]$PythonLibSource='C:\Python\Python314\Lib',
    [long]$After=0
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$nativeBuild=Join-Path $projectRoot 'build\native'
if($Action -eq 'Build') {
    $benchmarks=@(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -eq 'xlang3.exe' -and $_.CommandLine -match 'pyperformance|run_benchmark\.py|benchmarks[\\/]'
    })
    if($benchmarks.Count -gt 0) {
        Write-Host "Build deferred: live xlang3 benchmark processes $($benchmarks.ProcessId -join ', ')."
        exit 3
    }
    & node (Join-Path $projectRoot 'Tools/verify-jsoncons.mjs')
    if($LASTEXITCODE -ne 0) {exit $LASTEXITCODE}
    $npmExecutable=(Get-Command npm.cmd -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    & $npmExecutable ci --prefix (Join-Path $projectRoot 'Native/tests/sdk') --ignore-scripts --no-audit --no-fund
    if($LASTEXITCODE -ne 0) {exit $LASTEXITCODE}
    $cmakeExecutable='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if(-not (Test-Path -LiteralPath $cmakeExecutable)) { $cmakeExecutable=(Get-Command cmake -ErrorAction Stop).Source }
    & $cmakeExecutable -S (Join-Path $projectRoot 'Native') -B $nativeBuild -G 'Visual Studio 18 2026' -A x64 "-DAGENTFLOW_XLANG3_RUNTIME_DIR=$RuntimeDirectory" "-DAGENTFLOW_PYTHON_LIB_SOURCE=$PythonLibSource"
    if($LASTEXITCODE -ne 0) {exit $LASTEXITCODE}
    & $cmakeExecutable --build $nativeBuild --config Release --parallel 1
    if($LASTEXITCODE -ne 0) {exit $LASTEXITCODE}
    $ctestExecutable=Join-Path (Split-Path $cmakeExecutable -Parent) 'ctest.exe'
    & $ctestExecutable --test-dir $nativeBuild -C Release --output-on-failure
    exit $LASTEXITCODE
}
$demo=Join-Path $nativeBuild 'Release\xmind_persistence_demo.exe'
if(-not (Test-Path -LiteralPath $demo)) {throw 'Build the native milestone first; no executable is available yet.'}
if(-not $Database) {$Database=Join-Path $projectRoot '.agentflow\milestones\persistence\state.sqlite'}
$Database=[System.IO.Path]::GetFullPath($Database)
if($Action -eq 'Seed') {
    New-Item -ItemType Directory -Force -Path (Split-Path $Database -Parent) | Out-Null
}
& $demo $Action.ToLowerInvariant() $Database (Join-Path $RuntimeDirectory 'modules') $PythonLibSource $After
exit $LASTEXITCODE
