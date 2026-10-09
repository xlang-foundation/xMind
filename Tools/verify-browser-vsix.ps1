param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$browserArchive=[System.IO.Compression.ZipFile]::OpenRead([System.IO.Path]::GetFullPath($Path))
try {
    $browserRequired=@('extension/patch-review.js','extension/browser-runtime/assets/patch-review.js','extension/extension.js','extension/browser-view.js','extension/browser-runtime/server.mjs','extension/browser-runtime/assets/index.html','extension/browser-runtime/assets/browser.js','extension/browser-runtime/assets/browser.css','extension/browser-runtime/assets/chat.js','extension/browser-runtime/assets/chat.css','extension/browser-runtime/assets/client.js','extension/browser-runtime/assets/marked.js','extension/browser-runtime/assets/purify.js','extension/browser-runtime/assets/licenses/marked.txt','extension/browser-runtime/assets/licenses/dompurify.txt','extension/browser-runtime/assets/licenses/dompurify-MPL.txt','extension/node_modules/marked/lib/marked.umd.js','extension/node_modules/dompurify/dist/purify.min.js','extension/node_modules/marked/LICENSE','extension/node_modules/dompurify/LICENSE')
    foreach($browserEntry in $browserRequired){if(-not $browserArchive.GetEntry($browserEntry)){throw "Missing browser/shared renderer package asset: $browserEntry"}}
    $browserManifest=$browserArchive.GetEntry('extension/package.json')
    $browserReader=[System.IO.StreamReader]::new($browserManifest.Open())
    try{$browserPackage=$browserReader.ReadToEnd() | ConvertFrom-Json}finally{$browserReader.Dispose()}
    if(-not($browserPackage.contributes.commands | Where-Object command -eq 'agentflow.openBrowser')){throw 'Shared browser command missing from package'}
    if($browserArchive.Entries | Where-Object {$_.FullName -match '(?:^|/)(?:auth\.token|\.agentflow|sessions\.sqlite)(?:/|$)'}){throw 'Private state must not be packaged'}
    "Browser VSIX verified: $($browserRequired.Count) required access/renderer/license assets and shared-backend command; no private state."
}finally{$browserArchive.Dispose()}
