param([Parameter(Mandatory)][string]$RuntimeSource,[Parameter(Mandatory)][string]$StdlibSource)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'This build helper is for isolated GitHub runners. Use the guarded native-milestone launcher on the development machine.'}
$ciRoot=Split-Path $PSScriptRoot -Parent
$ciRuntime=(Resolve-Path -LiteralPath $RuntimeSource).Path
$ciStdlib=(Resolve-Path -LiteralPath $StdlibSource).Path
$ciEvidence=Join-Path $ciRoot 'build/ci-evidence'
New-Item -ItemType Directory -Force -Path $ciEvidence | Out-Null
function Invoke-CiCommand([string]$Name,[string]$Executable,[string[]]$Arguments){
    & $Executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $ciEvidence ($Name+'.log'))
    if($LASTEXITCODE -ne 0){throw ($Name+' failed with exit code '+$LASTEXITCODE)}
}
$ciRevision=(& git -C $ciRuntime rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $ciRevision -ne '914783909835116969aad7c66b210a5ac9a27661'){throw 'xlang3 source does not match the pinned runtime.'}
if((& git -C $ciRuntime status --porcelain)){throw 'Runtime checkout must be clean before applying the reviewed SQLite prerequisite.'}
$ciStdlibRevision=(& git -C (Split-Path $ciStdlib -Parent) rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $ciStdlibRevision -ne 'ebf955df7a89ed0c7968f79faec1de49f61ed7cb'){throw 'Standard-library source differs from the pinned CPython 3.14.0 source.'}
$ciPatch=Join-Path $ciRoot 'doc/runtime-prerequisites/sqlite-xlang3.patch'
Invoke-CiCommand 'sqlite-patch-check' 'git' @('-C',$ciRuntime,'apply','--check',$ciPatch)
Invoke-CiCommand 'runtime-isolated-branch' 'git' @('-C',$ciRuntime,'checkout','-b','xmind-ci-sqlite')
Invoke-CiCommand 'sqlite-patch-apply' 'git' @('-C',$ciRuntime,'apply',$ciPatch)
$ciVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$ciVisualStudio=(& $ciVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json|ConvertFrom-Json)|Select-Object -First 1
if($LASTEXITCODE -ne 0 -or -not $ciVisualStudio){throw 'No supported MSVC x64 toolchain is installed.'}
$ciVsMajor=[int]($ciVisualStudio.installationVersion.Split('.')[0])
$ciGenerator=switch($ciVsMajor){18 {'Visual Studio 18 2026'} 17 {'Visual Studio 17 2022'} default {throw 'Unsupported Visual Studio generator version.'}}
$ciCmake=Join-Path $ciVisualStudio.installationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if(-not(Test-Path -LiteralPath $ciCmake)){$ciCmake=(Get-Command cmake -ErrorAction Stop).Source}
$ciCtest=Join-Path (Split-Path $ciCmake -Parent) 'ctest.exe'
if(-not(Test-Path -LiteralPath $ciCtest)){throw 'Matching CTest executable is missing.'}
$ciProvenance=@{xmind=(& git -C $ciRoot rev-parse HEAD).Trim();xlang3=$ciRevision;stdlib_source=$ciStdlibRevision;sqlite_patch_sha256=(Get-FileHash -LiteralPath $ciPatch -Algorithm SHA256).Hash;runtime='Native xlang3; no CPython execution or bridge';toolchain=$ciGenerator;toolchain_version=$ciVisualStudio.installationVersion}
$ciProvenance|ConvertTo-Json|Set-Content (Join-Path $ciEvidence 'provenance.json')
$ciRuntimeBuild=Join-Path $ciRuntime 'build'
Invoke-CiCommand 'runtime-configure' $ciCmake @('-S',$ciRuntime,'-B',$ciRuntimeBuild,'-G',$ciGenerator,'-A','x64','-DXLANG3_BUILD_CPYTHON_BRIDGE=OFF')
Invoke-CiCommand 'runtime-build' $ciCmake @('--build',$ciRuntimeBuild,'--config','Release','--target','xlang3','xlang_json_native_package','xlang_sqlite3_native_package','--parallel','2')
$ciRelease=Join-Path $ciRuntimeBuild 'Release'
$ciNative=Join-Path $ciRoot 'build/native'
$ciNode=(Get-Command node -ErrorAction Stop).Source
$ciOpenSsl=(Get-Command openssl -ErrorAction Stop).Source
Invoke-CiCommand 'native-configure' $ciCmake @('-S',(Join-Path $ciRoot 'Native'),'-B',$ciNative,'-G',$ciGenerator,'-A','x64',('-DAGENTFLOW_XLANG3_SOURCE='+$ciRuntime),('-DAGENTFLOW_XLANG3_RUNTIME_DIR='+$ciRelease),('-DAGENTFLOW_PYTHON_LIB_SOURCE='+$ciStdlib),('-DAGENTFLOW_NODE_EXECUTABLE='+$ciNode),('-DAGENTFLOW_OPENSSL_EXECUTABLE='+$ciOpenSsl))
Invoke-CiCommand 'native-build' $ciCmake @('--build',$ciNative,'--config','Release','--parallel','2')
$ciTests=& $ciCtest --test-dir $ciNative -C Release --show-only=json-v1
if($LASTEXITCODE -ne 0){throw 'Could not inspect the configured native contracts.'}
$ciTests|Set-Content (Join-Path $ciEvidence 'contracts.json')
$ciExpected=@('model_stream_protocol_contract','model_request_contract','native_secret_protection_contract','native_agent_service_contract','native_agent_runner_contract','native_edit_executor_contract','native_approved_edit_http_contract','native_workspace_tools_contract','native_chat_provider_contract','native_http_stream_transport_contract','native_agent_http_contract','native_agent_edit_http_contract','native_http_cli_contract','embedded_xlang_sqlite_contract','native_permission_waiter_contract','native_operation_repository_contract','xlang_repository_contract','persistence_service_contract','encrypted_credential_repository_contract')
$ciActual=($ciTests|ConvertFrom-Json).tests.name
if(@($ciActual).Count -ne $ciExpected.Count -or (Compare-Object ($ciActual|Sort-Object) ($ciExpected|Sort-Object))){throw 'The complete expected native contract set was not registered; refusing a partial green build.'}
Invoke-CiCommand 'native-ctest' $ciCtest @('--test-dir',$ciNative,'-C','Release','--output-on-failure','--no-tests=error')
