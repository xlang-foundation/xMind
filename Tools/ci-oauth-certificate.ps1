param(
    [Parameter(Mandatory)][ValidateSet('Install','Remove')][string]$Action,
    [Parameter(Mandatory)][string]$CertificateFile,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$Thumbprint
)
$ErrorActionPreference='Stop'
# Test-only trust provisioning. Never enable this helper on a development PC,
# a self-hosted runner or by impersonating GitHub's environment variables.
if($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or $env:RUNNER_OS -ne 'Windows' -or $env:GITHUB_RUN_ID -notmatch '^\d+$'){
    throw 'OAuth certificate provisioning requires an isolated GitHub-hosted Windows runner.'
}
# Machine-root provisioning avoids the user-root consent UI in noninteractive CI.
# It remains restricted to the disposable hosted runner and exact owned CA.
$oauthIdentity=[Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $oauthPrincipal=[Security.Principal.WindowsPrincipal]::new($oauthIdentity)
    if(-not $oauthPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
        throw 'OAuth test certificate provisioning requires the elevated isolated runner.'
    }
} finally {$oauthIdentity.Dispose()}
$oauthTemp=(Resolve-Path -LiteralPath $env:RUNNER_TEMP).Path
$oauthFile=(Resolve-Path -LiteralPath $CertificateFile).Path
$oauthRelative=[IO.Path]::GetRelativePath($oauthTemp,$oauthFile)
if([IO.Path]::IsPathRooted($oauthRelative) -or $oauthRelative -eq '..' -or $oauthRelative.StartsWith('..'+[IO.Path]::DirectorySeparatorChar) -or $oauthRelative -notmatch '^xmind-oauth-trust-[^\\/]+[\\/]ca\.cer$'){
    throw 'The test certificate must belong to its owned runner temporary directory.'
}
$oauthCertificate=[Security.Cryptography.X509Certificates.X509Certificate2]::new($oauthFile)
try {
    if($oauthCertificate.Thumbprint -ne $Thumbprint.ToUpperInvariant() -or $oauthCertificate.HasPrivateKey -or $oauthCertificate.Subject -notmatch '^CN=xMind isolated OAuth CI [a-f0-9-]{36}$'){
        throw 'The test root certificate identity is invalid.'
    }
    $oauthPath='Cert:\LocalMachine\Root\'+$oauthCertificate.Thumbprint
    if($Action -eq 'Install'){
        if(Test-Path -LiteralPath $oauthPath){throw 'Do not replace an existing trusted certificate.'}
        try {
            $oauthImported=Import-Certificate -FilePath $oauthFile -CertStoreLocation 'Cert:\LocalMachine\Root'
            if($oauthImported.Thumbprint -ne $oauthCertificate.Thumbprint -or -not(Test-Path -LiteralPath $oauthPath)){throw 'Test certificate installation was not confirmed.'}
        } catch {
            if(Test-Path -LiteralPath $oauthPath){Remove-Item -LiteralPath $oauthPath -Force}
            throw
        }
    } else {
        if(-not(Test-Path -LiteralPath $oauthPath)){throw 'The owned test certificate was not present for cleanup.'}
        Remove-Item -LiteralPath $oauthPath -Force
        if(Test-Path -LiteralPath $oauthPath){throw 'Test certificate removal was not confirmed.'}
    }
    @{action=$Action;thumbprint=$oauthCertificate.Thumbprint;store='LocalMachine/Root';isolated_runner=$true}|ConvertTo-Json -Compress
} finally {$oauthCertificate.Dispose()}
