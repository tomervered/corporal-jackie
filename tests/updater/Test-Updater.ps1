# Tests for packaging/windows/updater/apply-update.ps1, on real folders:
# a valid update, a damaged file, a forged signature, path traversal (in the
# manifest and in the zip), a link that leads out of the install, a file in
# use, an interrupted update, and an older build.
#
#   powershell -File tests\updater\Test-Updater.ps1      (Windows PowerShell 5.1)
#   pwsh -File tests\updater\Test-Updater.ps1            (PowerShell 7)
#
# Each case builds an install of build 10 and a package for build 11, signed
# with a key made here, runs the updater in its own process, and checks the
# exit code and every file afterwards. Exits 1 if any case fails.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$updater = Join-Path $repo 'packaging\windows\updater\apply-update.ps1'
$signer = Join-Path $repo 'packaging\windows\updater\sign-manifest.ps1'
$shell = (Get-Process -Id $PID).Path   # run the updater in the same PowerShell
$work = Join-Path ([IO.Path]::GetTempPath()) ('cj-updater-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem

function New-Key {
    # RSA.Create() gives a fixed-size key on .NET Framework (KeySize is
    # read-only there), so ask for the size up front, as the docs do.
    $rsa = New-Object Security.Cryptography.RSACryptoServiceProvider 2048
    $pair = @{ Private = $rsa.ToXmlString($true); Public = $rsa.ToXmlString($false) }
    $rsa.Dispose()
    return $pair
}
$key = New-Key
$otherKey = New-Key

function Get-Sha256([string]$file) { (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant() }

# Writes files (relative path -> text) and the build.txt / manifest.txt that
# describe them, the way CI stamps a build.
function Write-Build([string]$dir, [int]$build, [hashtable]$files, [string]$signWith) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $all = @{} + $files
    $all['build.txt'] = "build $build (abc$build)`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    $lines = foreach ($relative in ($all.Keys | Sort-Object)) {
        # A hostile path (a test case) goes in the manifest but is never written.
        if (-not $relative.StartsWith('..')) {
            $path = Join-Path $dir $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
            [IO.File]::WriteAllText($path, $all[$relative])
        }
        $hash = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($all[$relative])) | ForEach-Object { $_.ToString('x2') })
        "$hash`t$($relative.Replace('\', '/'))"
    }
    $sha.Dispose()
    [IO.File]::WriteAllText((Join-Path $dir 'manifest.txt'), (($lines -join "`n") + "`n"))
    if ($signWith) { & $signer -Folder $dir -KeyXml $signWith }
}

$v10 = @{
    'app\CorporalJackie.exe' = 'exe 10'
    'app\Qt6Core.dll'        = 'qt same'
    'app\old-plugin.dll'     = 'going away'
    'LICENSE.txt'            = 'license'
}
$v11 = @{
    'app\CorporalJackie.exe' = 'exe 11'
    'app\Qt6Core.dll'        = 'qt same'
    'app\qml\New\new.qml'    = 'new file'
    'LICENSE.txt'            = 'license'
}

# An installed copy of build 10, with the updater and the test key in app\updater.
function New-Install([string]$name) {
    $root = Join-Path $work $name
    Write-Build $root 10 $v10 $key.Private
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'app\updater') | Out-Null
    Copy-Item $updater (Join-Path $root 'app\updater\apply-update.ps1')
    [IO.File]::WriteAllText((Join-Path $root 'app\updater\update-key.xml'), $key.Public)
    return $root
}

# A package from build 10 to `build` (11 unless said): changed files only.
function New-Package([string]$name, [hashtable]$files = $v11, [int]$build = 11, [string]$signWith = $key.Private) {
    $full = Join-Path $work "$name-full"
    Write-Build $full $build $files $null
    $package = Join-Path $work $name
    New-Item -ItemType Directory -Force -Path (Join-Path $package 'files') | Out-Null
    foreach ($f in 'build.txt', 'manifest.txt') { Copy-Item (Join-Path $full $f) (Join-Path $package $f) }
    foreach ($relative in @($files.Keys) + 'build.txt') {
        if ($relative -ne 'build.txt' -and $v10.ContainsKey($relative) -and $v10[$relative] -eq $files[$relative]) { continue }
        if ($relative.StartsWith('..')) { continue }
        $target = Join-Path $package "files\$relative"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item (Join-Path $full $relative) $target
    }
    if ($signWith) { & $signer -Folder $package -KeyXml $signWith }
    return $package
}

function Invoke-Updater([string]$root, [string]$package, [hashtable]$environment = @{}) {
    foreach ($k in $environment.Keys) { Set-Item "env:$k" $environment[$k] }
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'app\updater\apply-update.ps1'), '-Root', $root, '-NoPause')
    if ($package) { $argList += @('-Package', $package) }
    $output = & $shell @argList 2>&1 | Out-String
    $code = $LASTEXITCODE
    foreach ($k in $environment.Keys) { Remove-Item "env:$k" }
    return @{ Code = $code; Output = $output }
}

# Every file under a folder with its hash, to show nothing changed.
function Get-State([string]$root) {
    $state = @{}
    foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File -Force) { $state[$f.FullName.Substring($root.Length + 1)] = Get-Sha256 $f.FullName }
    return $state
}
function Test-SameState([hashtable]$a, [hashtable]$b) {
    if ($a.Count -ne $b.Count) { return $false }
    foreach ($k in $a.Keys) { if (-not $b.ContainsKey($k) -or $a[$k] -ne $b[$k]) { return $false } }
    return $true
}

$failures = @()
function Check([string]$case, [bool]$condition, [string]$what, $result) {
    if ($condition) { return }
    $script:failures += "$case`: $what"
    Write-Host "FAIL $case`: $what"
    if ($result) { Write-Host $result.Output }
}
function Test-Unchanged([string]$case, [string]$root, [hashtable]$before, $result) {
    Check $case ($result.Code -ne 0) "expected a refusal, got exit 0" $result
    Check $case (Test-SameState $before (Get-State $root)) 'the install changed' $result
    Check $case (-not (Test-Path (Join-Path $root '.update-journal'))) 'a journal was left' $result
}

# --- Cases ----------------------------------------------------------------------

$case = 'valid update'
$root = New-Install 'valid'
$package = New-Package 'valid-pkg'
$result = Invoke-Updater $root $package
Check $case ($result.Code -eq 0) "exit $($result.Code)" $result
Check $case ([IO.File]::ReadAllText((Join-Path $root 'app\CorporalJackie.exe')) -eq 'exe 11') 'exe not replaced' $result
Check $case (Test-Path (Join-Path $root 'app\qml\New\new.qml')) 'new file missing' $result
Check $case (-not (Test-Path (Join-Path $root 'app\old-plugin.dll'))) 'removed file still there' $result
Check $case (([IO.File]::ReadAllText((Join-Path $root 'build.txt'))).Trim() -eq 'build 11 (abc11)') 'build marker not advanced' $result
Check $case (-not (Test-Path (Join-Path $root '.update-backup'))) 'backup left behind' $result
# The same package again: not newer, refused.
$before = Get-State $root
Test-Unchanged 'replayed update' $root $before (Invoke-Updater $root $package)

$case = 'valid update from a zip'
$root = New-Install 'zip'
$package = New-Package 'zip-pkg'
$zip = Join-Path $work 'CorporalJackie-windows-x64-update.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($package, $zip)
$result = Invoke-Updater $root $zip
Check $case ($result.Code -eq 0) "exit $($result.Code)" $result
Check $case ([IO.File]::ReadAllText((Join-Path $root 'app\CorporalJackie.exe')) -eq 'exe 11') 'exe not replaced' $result

$case = 'bad hash'
$root = New-Install 'badhash'
$package = New-Package 'badhash-pkg'
[IO.File]::WriteAllText((Join-Path $package 'files\app\CorporalJackie.exe'), 'tampered')
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)

$case = 'forged manifest'
$root = New-Install 'forged'
$package = New-Package 'forged-pkg' -signWith $otherKey.Private
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)

$case = 'manifest edited after signing'
$root = New-Install 'edited'
$package = New-Package 'edited-pkg'
[IO.File]::AppendAllText((Join-Path $package 'manifest.txt'), ("0" * 64) + "`tapp/extra.dll`n")
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)

$case = 'unsigned package'
$root = New-Install 'unsigned'
$package = New-Package 'unsigned-pkg' -signWith $null
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)

$case = 'traversal in a signed manifest'
$root = New-Install 'traversal'
$evil = @{} + $v11
$evil['..\outside.txt'] = 'escaped'
$package = New-Package 'traversal-pkg' -files $evil
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)
Check $case (-not (Test-Path (Join-Path $work 'outside.txt'))) 'a file was written outside the install' $null

$case = 'absolute path in a signed manifest'
$root = New-Install 'absolute'
$package = New-Package 'absolute-pkg'
$outside = Join-Path $work 'absolute-target.txt'
[IO.File]::AppendAllText((Join-Path $package 'manifest.txt'), "$(Get-Sha256 (Join-Path $package 'build.txt'))`t$outside`n")
& $signer -Folder $package -KeyXml $key.Private
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)
Check $case (-not (Test-Path $outside)) 'a file was written to an absolute path' $null

$case = 'traversal in the zip'
$root = New-Install 'zipslip'
$package = New-Package 'zipslip-pkg'
$zip = Join-Path $work 'zipslip.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($package, $zip)
$archive = [IO.Compression.ZipFile]::Open($zip, 'Update')
$entry = $archive.CreateEntry('../zipslip-escaped.txt')
$writer = New-Object IO.StreamWriter($entry.Open())
$writer.Write('escaped')
$writer.Dispose()
$archive.Dispose()
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $zip)
Check $case (-not (Test-Path (Join-Path ([IO.Path]::GetTempPath()) 'zipslip-escaped.txt'))) 'the zip wrote outside its folder' $null

$case = 'link out of the install'
$root = New-Install 'junction'
$package = New-Package 'junction-pkg'
$elsewhere = Join-Path $work 'junction-elsewhere'
New-Item -ItemType Directory -Path $elsewhere | Out-Null
New-Item -ItemType Junction -Path (Join-Path $root 'app\qml') -Target $elsewhere | Out-Null
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)
Check $case ((Get-ChildItem -LiteralPath $elsewhere -Recurse -File).Count -eq 0) 'a file was written through the link' $null

$case = 'locked file'
$root = New-Install 'locked'
$package = New-Package 'locked-pkg'
$before = Get-State $root
$lock = [IO.File]::Open((Join-Path $root 'app\CorporalJackie.exe'), 'Open', 'Read', 'None')
try { $result = Invoke-Updater $root $package } finally { $lock.Dispose() }
Test-Unchanged $case $root $before $result
# Unlocked, the same package works.
$result = Invoke-Updater $root $package
Check "$case, then unlocked" ($result.Code -eq 0) "exit $($result.Code)" $result

$case = 'interrupted update'
$root = New-Install 'interrupted'
$package = New-Package 'interrupted-pkg'
$before = Get-State $root
$result = Invoke-Updater $root $package @{ CJ_UPDATE_CRASH_AFTER = '2' }
Check $case ($result.Code -eq 9) "expected the simulated crash (exit 9), got $($result.Code)" $result
Check $case (Test-Path (Join-Path $root '.update-journal')) 'no journal after the crash' $result
Check $case (([IO.File]::ReadAllText((Join-Path $root 'build.txt'))).Trim() -eq 'build 10 (abc10)') 'build marker advanced before the end' $result
# The next run puts the old version back first...
$result = Invoke-Updater $root $null
Check $case ($result.Code -eq 0) "recovery exit $($result.Code)" $result
Check $case (Test-SameState $before (Get-State $root)) 'recovery did not restore build 10 exactly' $result
# ...and then the update applies cleanly.
$result = Invoke-Updater $root $package
Check "$case, then again" ($result.Code -eq 0) "exit $($result.Code)" $result
Check "$case, then again" ([IO.File]::ReadAllText((Join-Path $root 'app\CorporalJackie.exe')) -eq 'exe 11') 'exe not replaced' $result

$case = 'older build'
$root = New-Install 'older'
$package = New-Package 'older-pkg' -build 9
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)

$case = 'wrong starting build'
$root = New-Install 'wrongbase'
[IO.File]::WriteAllText((Join-Path $root 'app\Qt6Core.dll'), 'not what build 10 had')
$package = New-Package 'wrongbase-pkg'
$before = Get-State $root
Test-Unchanged $case $root $before (Invoke-Updater $root $package)

Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
if ($failures.Count -gt 0) {
    Write-Host "`n$($failures.Count) updater checks failed."
    exit 1
}
Write-Host "All updater checks passed ($($PSVersionTable.PSEdition) $($PSVersionTable.PSVersion))."
exit 0
