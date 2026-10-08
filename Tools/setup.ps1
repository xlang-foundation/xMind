[CmdletBinding()]
param(
    [string]$Runtime = "$PSScriptRoot\..\..\xlang3\build\Release\xlang3.exe",
    [string]$PythonLib = 'C:\Python\Python314\Lib'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot\..").Path
if (-not (Test-Path -LiteralPath $Runtime)) { throw "Build xlang3 first: $Runtime" }
$pipWheel = Join-Path $PythonLib 'ensurepip\_bundled\pip-26.2.1-py3-none-any.whl'
if (-not (Test-Path -LiteralPath $pipWheel)) {
    throw "Provide pure-Python pip 26.2.1 wheel under $PythonLib\ensurepip\_bundled. No CPython interpreter will be started."
}
$bootstrap = Join-Path $projectRoot '.agentflow\pip-bootstrap'
$packages = Join-Path $projectRoot '.agentflow\site-packages'
if (-not (Test-Path -LiteralPath (Join-Path $bootstrap 'pip\__main__.py'))) {
    New-Item -ItemType Directory -Path $bootstrap -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($pipWheel, $bootstrap)
}
$savedPath = $env:PYTHONPATH
$savedLib = $env:XLANG3_PYTHON_LIB
try {
    $env:XLANG3_PYTHON_LIB = $PythonLib
    $env:PYTHONPATH = "$bootstrap;$packages"
    & $Runtime -m pip install --upgrade --only-binary=:all: --platform any --implementation py --abi none --no-deps --disable-pip-version-check --target $packages -r "$projectRoot\requirements-pure.lock"
    if ($LASTEXITCODE -ne 0) { throw "xlang3 pip failed. Discuss compatibility gaps before selecting a workaround." }
    # pydantic-core distributes Python wrappers and native binaries together.
    # Download through xlang3 pip, but extract only source wrappers. Its native
    # implementation is the existing xlang3 pydantic_core._pydantic_core binding.
    $wheelCache = Join-Path $projectRoot '.agentflow\source-wheels'
    & $Runtime -m pip download --only-binary=:all: --no-deps --disable-pip-version-check --platform win_amd64 --implementation cp --python-version 314 --abi cp314 --dest $wheelCache 'pydantic_core==2.46.5'
    if ($LASTEXITCODE -ne 0) { throw "xlang3 pip source-wrapper download failed." }
    $wrapperWheel = Join-Path $wheelCache 'pydantic_core-2.46.5-cp314-cp314-win_amd64.whl'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($wrapperWheel)
    try {
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName -notmatch '^pydantic_core/[^/]+\.(py|pyi)$' -and $entry.FullName -ne 'pydantic_core/py.typed') { continue }
            $destination = Join-Path $packages $entry.FullName
            New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $true)
        }
    } finally { $archive.Dispose() }
    $nativeFiles = @(Get-ChildItem -LiteralPath $packages -Recurse -File | Where-Object { $_.Extension -in '.pyd','.dll','.so','.dylib' })
    if ($nativeFiles.Count -gt 0) { throw "Native dependency binaries found in pure-Python package directory." }
    $env:PYTHONPATH = "$projectRoot;$packages"
    & $Runtime "$projectRoot\Tools\probes\xlang3_dependencies.py"
    if ($LASTEXITCODE -ne 0) { throw "xlang3 dependency probe failed. Discuss the gap before changing native code." }
} finally {
    $env:PYTHONPATH = $savedPath
    $env:XLANG3_PYTHON_LIB = $savedLib
}
