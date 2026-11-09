# Release signing

Three kinds of signature matter for Corporal Jackie. Only the first works today, and it still needs one secret from the repository owner.

| Signature | Protects | Status |
|---|---|---|
| Update signature (`manifest.sig`) | Windows update packages: the updater refuses a package that this CI did not sign | Code done. Needs the key pair below. |
| Authenticode (Windows) | The `.exe` and `.dll` files: SmartScreen and Chrome stop calling them dangerous | Needs a certificate or a signing service |
| Developer ID and notarization (macOS) | The `.app`: Gatekeeper opens it without a right-click | Needs an Apple Developer account |

## Update signature

The full Windows build ships `app\updater\apply-update.ps1` and `app\updater\update-key.xml`, an RSA public key. An update package is data only (changed files, `build.txt`, `manifest.txt`, `manifest.sig`). The installed updater checks the signature over `build.txt` followed by `manifest.txt`, then the hash of every file, before it changes anything. A package signed with another key, or not signed, is refused.

Until the key exists, CI builds carry no `update-key.xml` and sign nothing, so every update package is refused and the full build is the only way to update.

What the owner does, once, on any Windows PC:

1. Open PowerShell and run:

   ```powershell
   $r = New-Object System.Security.Cryptography.RSACryptoServiceProvider 3072
   $r.ToXmlString($true)  | Set-Content -NoNewline update-signing-private.xml
   $r.ToXmlString($false) | Set-Content -NoNewline update-key.xml
   ```

2. In GitHub, open the repository's Settings › Secrets and variables › Actions, add a secret named `UPDATE_SIGNING_KEY`, and paste the whole content of `update-signing-private.xml`.
3. Commit `update-key.xml` as `packaging/windows/update-key.xml` (it is public), or send it to Claude to commit.
4. Delete `update-signing-private.xml`, or keep it somewhere only you can open. Anyone with it can sign updates that copies of the app will install.

The first build with the key has to be installed as a full download: older copies have no updater that knows the key. After that, update packages from `main` apply as before.

To change the key later, put the new `update-key.xml` in a build signed with the old key. Copies that apply that update trust the new key from then on.

## Authenticode on Windows

Chrome and SmartScreen judge unsigned files by how many people downloaded them, so a fresh build is flagged. A signature from a trusted certificate fixes that. The options, cheapest first:

- Azure Trusted Signing (Microsoft's signing service, about $10 a month). Needs an Azure account and identity validation of a person or company. CI then signs with the `azure/trusted-signing-action` step, using three secrets for the service principal.
- An OV or EV code-signing certificate from a certificate authority (roughly $200 to $500 a year). Since 2023 these live on a hardware token or a cloud HSM, so CI signs through the vendor's cloud tool rather than a `.pfx` file.

Either way, CI would sign `CorporalJackie.exe`, `app\CorporalJackie.exe` and our own DLLs in the Package step, before `manifest.txt` is written, so the update hashes cover the signed files.

## macOS

A Developer ID certificate needs an Apple Developer Program membership ($99 a year). CI would then run `codesign --options runtime --timestamp` on the bundle, `xcrun notarytool submit --wait` with an App Store Connect API key, and `xcrun stapler staple`. The secrets would be the certificate as a `.p12` with its password, and the API key ID, issuer and key file.

None of this can be set up from the repository alone: each needs an account in the owner's name.
