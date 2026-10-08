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
if($LASTEXITCODE -ne 0 -or $ciRevision -ne '4aea7d8fb24da9ba86f9d7eeb92820794f213d29'){throw 'xlang3 source does not match the pinned runtime.'}
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
Invoke-CiCommand 'native-schema-source-check' $ciNode @((Join-Path $ciRoot 'Tools/verify-jsoncons.mjs'))
$ciNpm=(Get-Command npm.cmd -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
Invoke-CiCommand 'native-sdk-peer-install' $ciNpm @('ci','--prefix',(Join-Path $ciRoot 'Native/tests/sdk'),'--ignore-scripts','--no-audit','--no-fund')
$ciOpenSsl=(Get-Command openssl -ErrorAction Stop).Source
Invoke-CiCommand 'native-configure' $ciCmake @('-S',(Join-Path $ciRoot 'Native'),'-B',$ciNative,'-G',$ciGenerator,'-A','x64',('-DAGENTFLOW_XLANG3_SOURCE='+$ciRuntime),('-DAGENTFLOW_XLANG3_RUNTIME_DIR='+$ciRelease),('-DAGENTFLOW_PYTHON_LIB_SOURCE='+$ciStdlib),('-DAGENTFLOW_NODE_EXECUTABLE='+$ciNode),('-DAGENTFLOW_OPENSSL_EXECUTABLE='+$ciOpenSsl))
# Build the actual process contract and its production dependencies first. This
# catches new executor/test compile errors before unrelated schema/agent targets;
# the full build and exact complete contract gate below remain unconditional.
Invoke-CiCommand 'native-process-contract-build' $ciCmake @('--build',$ciNative,'--config','Release','--target','agentflow_process_executor_contract','--parallel','2')
Invoke-CiCommand 'native-build' $ciCmake @('--build',$ciNative,'--config','Release','--parallel','2')
$ciTests=& $ciCtest --test-dir $ciNative -C Release --show-only=json-v1
if($LASTEXITCODE -ne 0){throw 'Could not inspect the configured native contracts.'}
$ciTests|Set-Content (Join-Path $ciEvidence 'contracts.json')
$ciExpected=@('model_stream_protocol_contract','model_request_contract','native_secret_protection_contract','native_agent_service_contract','native_agent_runner_contract','native_edit_executor_contract','native_approved_edit_http_contract','native_workspace_tools_contract','native_chat_provider_contract','native_http_stream_transport_contract','native_agent_http_contract','native_agent_edit_http_contract','native_http_cli_contract','embedded_xlang_sqlite_contract','native_permission_waiter_contract','native_operation_repository_contract','xlang_repository_contract','persistence_service_contract','encrypted_credential_repository_contract')
$ciExpected+='native_mcp_wire_contract'
$ciExpected+='native_mcp_stdio_contract'
$ciExpected+='native_mcp_handshake_contract'
$ciExpected+='native_mcp_client_contract'
$ciExpected+='native_json_schema_contract'
$ciExpected+='native_mcp_effect_contract'
$ciExpected+='native_schema_worker_contract'
$ciExpected+='native_mcp_configuration_contract'
$ciExpected+='native_agent_mcp_http_contract'
$ciExpected+='native_mcp_sdk_modern_contract'
$ciExpected+='native_mcp_sdk_legacy_contract'
$ciExpected+='native_create_executor_contract'
$ciExpected+='native_process_adapter_contract'
$ciExpected+='native_process_executor_contract'
$ciExpected+='native_process_configuration_contract'
$ciExpected+='native_agent_process_http_contract'
$ciExpected+='native_instruction_configuration_contract'
$ciExpected+='native_repository_instructions_contract'
$ciExpected+='native_graph_contract'
$ciExpected+='native_graph_children_contract'
$ciExpected+='native_graph_checkpoint_contract'
$ciExpected+='native_graph_runner_contract'
$ciExpected+='native_graph_service_http_contract'
$ciExpected+='native_a2a_task_control_http_contract'
$ciExpected+='native_incoming_repository_contract'
$ciExpected+='native_a2a_message_http_contract'
$ciExpected+='native_a2a_stream_http_contract'
$ciExpected+='native_a2a_v1_http_contract'
$ciExpected+='native_responses_protocol_contract'
$ciExpected+='native_responses_agent_http_contract'
$ciExpected+='native_task_list_repository_contract'
$ciExpected+='native_provider_setup_contract'
$ciExpected+='native_provider_cli_contract'
$ciExpected+='native_anthropic_request_contract'
$ciExpected+='native_gemini_request_contract'
$ciExpected+='native_gemini_stream_contract'
$ciExpected+='native_gemini_provider_contract'
$ciExpected+='native_gemini_history_contract'
$ciExpected+='native_gemini_agent_contract'
$ciExpected+='native_gemini_catalogue_contract'
$ciExpected+='native_gemini_profile_runtime_contract'
$ciExpected+='native_anthropic_stream_contract'
$ciExpected+='native_anthropic_provider_contract'
$ciExpected+='native_anthropic_agent_contract'
$ciExpected+='native_provider_profiles_contract'
$ciExpected+='native_provider_profile_runtime_contract'
$ciExpected+='native_provider_profile_cli_contract'
$ciActual=($ciTests|ConvertFrom-Json).tests.name
if(@($ciActual).Count -ne $ciExpected.Count -or (Compare-Object ($ciActual|Sort-Object) ($ciExpected|Sort-Object))){throw 'The complete expected native contract set was not registered; refusing a partial green build.'}
Invoke-CiCommand 'native-ctest' $ciCtest @('--test-dir',$ciNative,'-C','Release','--output-on-failure','--no-tests=error')
$ciBundle=Join-Path $ciRoot 'build/native-distribution'
New-Item -ItemType Directory -Force -Path (Join-Path $ciBundle 'modules'),(Join-Path $ciBundle 'licenses')|Out-Null
foreach($ciBinary in @('xmind_server.exe','xmind_cli.exe','xmind_admin.exe','xmind_schema_worker.exe','xlang3_runtime.dll')){
    Copy-Item -LiteralPath (Join-Path $ciNative ('Release/'+$ciBinary)) -Destination $ciBundle
}
foreach($ciModule in @('xlang_json.x3pkg.dll','xlang_sqlite3.x3pkg.dll')){
    Copy-Item -LiteralPath (Join-Path $ciRelease ('modules/'+$ciModule)) -Destination (Join-Path $ciBundle 'modules')
}
Copy-Item -LiteralPath (Join-Path $ciRelease 'xlang3.exe') -Destination $ciBundle
Copy-Item -LiteralPath (Join-Path $ciEvidence 'provenance.json') -Destination $ciBundle
foreach($ciLicenseRoot in @(@{path=$ciRoot;label='xmind'},@{path=$ciRuntime;label='xlang3'})){
    foreach($ciLicense in Get-ChildItem -LiteralPath $ciLicenseRoot.path -File|Where-Object {$_.Name -match '^(LICENSE|NOTICE|COPYING)'}){
        Copy-Item -LiteralPath $ciLicense.FullName -Destination (Join-Path $ciBundle ('licenses/'+$ciLicenseRoot.label+'-'+$ciLicense.Name))
    }
}
foreach($ciThirdParty in @(@{path=(Join-Path $ciRoot 'Native/third_party');label='xmind-native'},@{path=(Join-Path $ciRuntime 'third_party');label='xlang3-third-party'},@{path=(Join-Path $ciRuntime 'modules');label='xlang3-modules'})){
    foreach($ciLicense in Get-ChildItem -LiteralPath $ciThirdParty.path -File -Recurse|Where-Object {$_.Name -match '^(LICENSE|NOTICE|COPYING)'}){
        $ciLicenseRelative=[System.IO.Path]::GetRelativePath($ciThirdParty.path,$ciLicense.FullName)
        $ciLicenseTarget=Join-Path $ciBundle ('licenses/'+$ciThirdParty.label+'/'+$ciLicenseRelative)
        New-Item -ItemType Directory -Force -Path (Split-Path $ciLicenseTarget -Parent)|Out-Null
        Copy-Item -LiteralPath $ciLicense.FullName -Destination $ciLicenseTarget
    }
}
@('Native xMind Windows development bundle. See provenance.json for exact source/toolchain.',
  'Provide allowed Python 3.14 standard-library source to --stdlib; no CPython executable/native extension is required.',
  'Run xmind_server.exe --db FILE --modules modules --stdlib LIB_SOURCE --port PORT with private XMIND_AUTH_TOKEN.',
  'Provider credentials belong in private backend configuration. No live provider credentials or conversations are packaged.',
  'This bundle is contract-tested development output, not evidence of full coding/provider/protocol/team completion.')|Set-Content (Join-Path $ciBundle 'README.txt')
