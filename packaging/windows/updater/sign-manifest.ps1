# Signs a build for the updater: an RSA (SHA-256, PKCS #1 v1.5) signature
# over the bytes of build.txt followed by manifest.txt, written base64 to
# manifest.sig in the same folder.
#
#   sign-manifest.ps1 -Folder <dist> -KeyXml <private key, as RSA XML>
#
# CI passes the key from the UPDATE_SIGNING_KEY secret; tests pass a key
# they made. See docs/RELEASE_SIGNING.md.
param(
    [Parameter(Mandatory = $true)][string]$Folder,
    [Parameter(Mandatory = $true)][string]$KeyXml
)
$ErrorActionPreference = 'Stop'
$data = [byte[]]([IO.File]::ReadAllBytes((Join-Path $Folder 'build.txt')) + [IO.File]::ReadAllBytes((Join-Path $Folder 'manifest.txt')))
$rsa = [Security.Cryptography.RSA]::Create()
try {
    $rsa.FromXmlString($KeyXml)
    $signature = $rsa.SignData($data, [Security.Cryptography.HashAlgorithmName]::SHA256,
                               [Security.Cryptography.RSASignaturePadding]::Pkcs1)
} finally {
    $rsa.Dispose()
}
[IO.File]::WriteAllText((Join-Path $Folder 'manifest.sig'), [Convert]::ToBase64String($signature))
