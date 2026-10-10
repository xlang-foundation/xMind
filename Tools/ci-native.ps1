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
if($LASTEXITCODE -ne 0 -or $ciRevision -ne '7b8b32ae3a0e6a99fac7babd97362736099448fb'){throw 'xlang3 source does not match the pinned runtime.'}
if((& git -C $ciRuntime status --porcelain)){throw 'Pinned runtime checkout must be clean.'}
$ciStdlibRevision=(& git -C (Split-Path $ciStdlib -Parent) rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $ciStdlibRevision -ne 'ebf955df7a89ed0c7968f79faec1de49f61ed7cb'){throw 'Standard-library source differs from the pinned CPython 3.14.0 source.'}
# This pinned runtime includes the reviewed SQLite transaction/text prerequisite
# and native Windows path support; applying the historical overlay again fails.
$ciVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$ciVisualStudio=(& $ciVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json|ConvertFrom-Json)|Select-Object -First 1
if($LASTEXITCODE -ne 0 -or -not $ciVisualStudio){throw 'No supported MSVC x64 toolchain is installed.'}
$ciVsMajor=[int]($ciVisualStudio.installationVersion.Split('.')[0])
$ciGenerator=switch($ciVsMajor){18 {'Visual Studio 18 2026'} 17 {'Visual Studio 17 2022'} default {throw 'Unsupported Visual Studio generator version.'}}
$ciCmake=Join-Path $ciVisualStudio.installationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if(-not(Test-Path -LiteralPath $ciCmake)){$ciCmake=(Get-Command cmake -ErrorAction Stop).Source}
$ciCtest=Join-Path (Split-Path $ciCmake -Parent) 'ctest.exe'
if(-not(Test-Path -LiteralPath $ciCtest)){throw 'Matching CTest executable is missing.'}
$ciProcessors=[Environment]::ProcessorCount
$ciParallelism=[Math]::Max(1,[Math]::Min(4,$ciProcessors))
$ciIdentity=[System.Security.Principal.WindowsIdentity]::GetCurrent()
try {$ciDefaultOwnerMatchesUser=($ciIdentity.Owner.Value -eq $ciIdentity.User.Value)} finally {$ciIdentity.Dispose()}
$ciProvenance=@{xmind=(& git -C $ciRoot rev-parse HEAD).Trim();xlang3=$ciRevision;stdlib_source=$ciStdlibRevision;sqlite_prerequisite='Included in pinned xlang3 source; no overlay applied';runtime='Native xlang3; no CPython execution or bridge';toolchain=$ciGenerator;toolchain_version=$ciVisualStudio.installationVersion;processors=$ciProcessors;build_parallelism=$ciParallelism;windows_default_owner_matches_user=$ciDefaultOwnerMatchesUser}
$ciProvenance|ConvertTo-Json|Set-Content (Join-Path $ciEvidence 'provenance.json')
$ciRuntimeBuild=Join-Path $ciRuntime 'build'
Invoke-CiCommand 'runtime-configure' $ciCmake @('-S',$ciRuntime,'-B',$ciRuntimeBuild,'-G',$ciGenerator,'-A','x64','-DXLANG3_BUILD_CPYTHON_BRIDGE=OFF','-DXLANG3_PYTHON314_EXECUTABLE:FILEPATH=OFF')
Invoke-CiCommand 'runtime-build' $ciCmake @('--build',$ciRuntimeBuild,'--config','Release','--target','xlang3','xlang_json_native_package','xlang_sqlite3_native_package','--parallel',$ciParallelism.ToString())
$ciRelease=Join-Path $ciRuntimeBuild 'Release'
$ciNative=Join-Path $ciRoot 'build/native'
$ciNode=(Get-Command node -ErrorAction Stop).Source
Invoke-CiCommand 'native-schema-source-check' $ciNode @((Join-Path $ciRoot 'Tools/verify-jsoncons.mjs'))
Invoke-CiCommand 'native-yaml-source-check' $ciNode @((Join-Path $ciRoot 'Tools/verify-yaml-cpp.mjs'))
Invoke-CiCommand 'native-regex-source-check' $ciNode @((Join-Path $ciRoot 'Tools/verify-regex-dependencies.mjs'))
$ciNpm=(Get-Command npm.cmd -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
Invoke-CiCommand 'native-sdk-peer-install' $ciNpm @('ci','--prefix',(Join-Path $ciRoot 'Native/tests/sdk'),'--ignore-scripts','--no-audit','--no-fund')
# The real local-view contract builds the shared browser assets before the later
# editor suite, so its locked host dependencies must already exist.
Invoke-CiCommand 'native-view-host-install' $ciNpm @('ci','--prefix',(Join-Path $ciRoot 'extensions/vscode'),'--ignore-scripts','--no-audit','--no-fund')
$ciOpenSsl=(Get-Command openssl -ErrorAction Stop).Source
Invoke-CiCommand 'native-configure' $ciCmake @('-S',(Join-Path $ciRoot 'Native'),'-B',$ciNative,'-G',$ciGenerator,'-A','x64',('-DAGENTFLOW_XLANG3_SOURCE='+$ciRuntime),('-DAGENTFLOW_XLANG3_RUNTIME_DIR='+$ciRelease),('-DAGENTFLOW_PYTHON_LIB_SOURCE='+$ciStdlib),('-DAGENTFLOW_NODE_EXECUTABLE='+$ciNode),('-DAGENTFLOW_OPENSSL_EXECUTABLE='+$ciOpenSsl))
# Build the actual process contract and its production dependencies first. This
# catches new executor/test compile errors before unrelated schema/agent targets;
# the full build and exact complete contract gate below remain unconditional.
Invoke-CiCommand 'native-process-contract-build' $ciCmake @('--build',$ciNative,'--config','Release','--target','agentflow_process_executor_contract','--parallel',$ciParallelism.ToString())
Invoke-CiCommand 'native-build' $ciCmake @('--build',$ciNative,'--config','Release','--parallel',$ciParallelism.ToString())
$ciTests=& $ciCtest --test-dir $ciNative -C Release --show-only=json-v1
if($LASTEXITCODE -ne 0){throw 'Could not inspect the configured native contracts.'}
$ciTests|Set-Content (Join-Path $ciEvidence 'contracts.json')
$ciExpected=@('model_stream_protocol_contract','model_request_contract','native_secret_protection_contract','native_agent_service_contract','native_agent_runner_contract','native_edit_executor_contract','native_approved_edit_http_contract','native_workspace_tools_contract','native_chat_provider_contract','native_http_stream_transport_contract','native_agent_http_contract','native_agent_edit_http_contract','native_http_cli_contract','embedded_xlang_sqlite_contract','native_permission_waiter_contract','native_operation_repository_contract','xlang_repository_contract','persistence_service_contract','encrypted_credential_repository_contract')
$ciExpected+='native_mcp_wire_contract'
$ciExpected+='native_file_patch_contract'
$ciExpected+='native_patch_file_executor_contract'
$ciExpected+='native_patch_executor_contract'
$ciExpected+='native_patch_tool_contract'
$ciExpected+='native_agent_patch_http_contract'
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
$ciExpected+='native_skill_context_contract'
$ciExpected+='native_skill_state_contract'
$ciExpected+='native_skill_catalogue_contract'
$ciExpected+='native_graph_contract'
$ciExpected+='native_graph_children_contract'
$ciExpected+='native_graph_checkpoint_contract'
$ciExpected+='native_graph_runner_contract'
$ciExpected+='native_graph_mcp_contract'
$ciExpected+='native_graph_mcp_http_contract'
$ciExpected+='native_delegation_contract'
$ciExpected+='native_delegation_http_contract'
$ciExpected+='native_agent_authority_contract'
$ciExpected+='native_dynamic_plan_contract'
$ciExpected+='native_dynamic_plan_repository_contract'
$ciExpected+='native_planning_http_contract'
$ciExpected+='native_child_executor_contract'
$ciExpected+='native_dynamic_plan_engine_contract'
$ciExpected+='native_dynamic_plan_mcp_contract'
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
$ciExpected+='native_deepseek_provider_contract'
$ciExpected+='native_deepseek_profile_runtime_contract'
$ciExpected+='native_provider_model_policy_runtime_contract'
$ciExpected+='native_gemini_history_contract'
$ciExpected+='native_gemini_agent_contract'
$ciExpected+='native_gemini_catalogue_contract'
$ciExpected+='native_gemini_profile_runtime_contract'
$ciExpected+='native_anthropic_stream_contract'
$ciExpected+='native_anthropic_history_contract'
$ciExpected+='native_anthropic_provider_contract'
$ciExpected+='native_anthropic_agent_contract'
$ciExpected+='native_provider_profiles_contract'
$ciExpected+='native_provider_profile_runtime_contract'
$ciExpected+='native_provider_profile_cli_contract'
$ciExpected+='native_context_selection_contract'
$ciExpected+='native_responses_context_contract'
$ciExpected+='native_context_repository_contract'
$ciExpected+='native_context_engine_contract'
$ciExpected+='native_context_graph_engine_contract'
$ciExpected+='native_provider_yaml_config_contract'
$ciExpected+='native_context_control_contract'
$ciExpected+='native_context_cli_contract'
$ciExpected+='native_backend_owner_contract'
$ciExpected+='native_runtime_generation_contract'
$ciExpected+='native_backend_owner_http_contract'
$ciExpected+='native_backend_handoff_contract'
$ciExpected+='native_owner_process_contract'
$ciExpected+='native_legacy_owner_contract'
$ciExpected+='native_unified_program_contract'
$ciExpected+='native_console_workspace_contract'
$ciExpected+='native_local_profile_contract'
$ciExpected+='native_local_view_contract'
$ciExpected+='native_unified_patch_http_contract'
$ciExpected+='native_unified_graph_http_contract'
$ciExpected+='native_unified_mcp_http_contract'
$ciActual=($ciTests|ConvertFrom-Json).tests.name
if(@($ciActual).Count -ne $ciExpected.Count -or (Compare-Object ($ciActual|Sort-Object) ($ciExpected|Sort-Object))){throw 'The complete expected native contract set was not registered; refusing a partial green build.'}
Invoke-CiCommand 'native-ctest' $ciCtest @('--test-dir',$ciNative,'-C','Release','--output-on-failure','--no-tests=error')
# Publish the complete successful gate only after CTest returns zero. Packaging
# binds this receipt to the exact registered set, test log and source revision.
@{schemaVersion=1;sourceRevision=$ciProvenance.xmind;expectedContracts=$ciExpected;contractsSha256=(Get-FileHash -LiteralPath (Join-Path $ciEvidence 'contracts.json') -Algorithm SHA256).Hash.ToLowerInvariant();ctestSha256=(Get-FileHash -LiteralPath (Join-Path $ciEvidence 'native-ctest.log') -Algorithm SHA256).Hash.ToLowerInvariant();exitCode=0}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $ciEvidence 'native-gate.json')
$ciBundle=Join-Path $ciRoot 'build/native-distribution'
if(Test-Path -LiteralPath $ciBundle){throw 'Fresh native distribution directory required; do not mix old and unified binaries.'}
New-Item -ItemType Directory -Force -Path (Join-Path $ciBundle 'modules'),(Join-Path $ciBundle 'licenses')|Out-Null
foreach($ciBinary in @('xmind.exe','xlang3_runtime.dll')){
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
  'Run xmind.exe serve --db FILE --modules modules --stdlib LIB_SOURCE --port PORT with private XMIND_AUTH_TOKEN.',
  'Provider credentials belong in private backend configuration. No live provider credentials or conversations are packaged.',
  'This bundle is contract-tested development output, not evidence of full coding/provider/protocol/team completion.')|Set-Content (Join-Path $ciBundle 'README.txt')
