# Applies a Corporal Jackie update package to the installed copy this script
# belongs to (it ships in app\updater\ of the full build, with the public key
# that update packages must be signed with).
#
#   apply-update.ps1 [-Package <update .zip or unzipped folder>] [-Root <install folder>] [-NoPause]
#
# With no -Package it takes the newest CorporalJackie-windows-x64-update*.zip
# in the user's Downloads folder.
#
# Order of work, so a bad or interrupted update never leaves a broken copy:
#  1. Finish or undo an earlier update that was interrupted (.update-journal).
#  2. Unpack the package to a temporary folder, refusing any entry whose path
#     leaves it.
#  3. Check the signature on build.txt + manifest.txt with the installed key,
#     that the build is newer than the installed one, that every path is a
#     plain relative path inside the install, and that every file in the
#     package has the hash the signed manifest gives. Files the package does
#     not carry must already be installed with the right hash.
#  4. Check that no file to replace or remove is in use.
#  5. Apply. Each step is written to the journal before it happens; old files
#     are moved to .update-backup. build.txt and the manifest go last.
#  6. Delete the journal (the update is now done), then the backup.
# Any failure in 5 puts every old file back.
#
# Works in Windows PowerShell 5.1 and PowerShell 7.

param(
    [string]$Package,
    [string]$Root,
    [switch]$NoPause
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

if (-not $Root) { $Root = Join-Path $PSScriptRoot '..\..' }
$Root = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$sep = [IO.Path]::DirectorySeparatorChar
$keyFile = Join-Path $PSScriptRoot 'update-key.xml'
$journal = Join-Path $Root '.update-journal'
$backup = Join-Path $Root '.update-backup'
$staging = $null
# Tests set this to stop the process after that many applied steps, as a
# power cut would.
$crashAfter = if ($env:CJ_UPDATE_CRASH_AFTER) { [int]$env:CJ_UPDATE_CRASH_AFTER } else { -1 }

function Finish([string]$message, [int]$code) {
    if ($staging -and (Test-Path -LiteralPath $staging)) { Remove-Item -Recurse -Force -LiteralPath $staging -ErrorAction SilentlyContinue }
    Write-Host ''
    Write-Host $message
    if (-not $NoPause) { Read-Host 'Press Enter to close' | Out-Null }
    exit $code
}

# A path from a manifest or a zip: relative, no drive, no "..", no stream
# names, no names Windows would quietly change.
function Test-SafeRelative([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path) -or $path.Length -gt 400) { return $false }
    if ($path.Contains(':') -or $path.StartsWith('/') -or $path.StartsWith('\')) { return $false }
    foreach ($c in $path.ToCharArray()) { if ([int]$c -lt 32) { return $false } }
    foreach ($part in ($path -split '[\\/]')) {
        if ($part -eq '' -or $part -eq '.' -or $part -eq '..') { return $false }
        if ($part.EndsWith('.') -or $part.EndsWith(' ')) { return $false }
    }
    return $true
}

function Test-IsLink([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    return [bool]((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}

# The full path of `relative` under `base`, refusing anything that ends up
# outside it, directly or through a link (junction or symbolic link) on the way.
function Resolve-Inside([string]$base, [string]$relative) {
    if (-not (Test-SafeRelative $relative)) { throw "Not a plain relative path: $relative" }
    $full = [IO.Path]::GetFullPath((Join-Path $base $relative))
    if (-not $full.StartsWith($base + $sep, [StringComparison]::OrdinalIgnoreCase)) { throw "Path leaves the folder: $relative" }
    $at = $full
    while ($at.Length -gt $base.Length) {
        if (Test-IsLink $at) { throw "A link is in the way: $relative" }
        $at = Split-Path -Parent $at
    }
    return $full
}

function Get-Sha256([string]$file) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant()
}

# manifest.txt: one "<sha256>`t<relative path>" line per file.
function Read-Manifest([string]$file) {
    $map = [ordered]@{}
    foreach ($line in [IO.File]::ReadAllLines($file)) {
        if ($line -eq '') { continue }
        $parts = $line -split "`t", 2
        if ($parts.Count -ne 2 -or $parts[0] -notmatch '^[0-9a-f]{64}$' -or -not (Test-SafeRelative $parts[1])) {
            throw "Bad manifest line: $line"
        }
        $key = $parts[1].Replace('/', '\')
        if ($map.Contains($key)) { throw "Listed twice: $key" }
        $map[$key] = $parts[0]
    }
    return $map
}

function Get-BuildNumber([string]$text) {
    if ($text -match '^build (\d+)') { return [int]$Matches[1] }
    return -1
}

function Test-Signature([string]$folder) {
    if (-not (Test-Path -LiteralPath $keyFile)) { return 'this copy has no update key (app\updater\update-key.xml)' }
    $sigFile = Join-Path $folder 'manifest.sig'
    if (-not (Test-Path -LiteralPath $sigFile)) { return 'the update is not signed' }
    $data = [byte[]]([IO.File]::ReadAllBytes((Join-Path $folder 'build.txt')) + [IO.File]::ReadAllBytes((Join-Path $folder 'manifest.txt')))
    $rsa = [Security.Cryptography.RSA]::Create()
    try {
        $rsa.FromXmlString([IO.File]::ReadAllText($keyFile))
        $signature = [Convert]::FromBase64String(([IO.File]::ReadAllText($sigFile)).Trim())
        $ok = $rsa.VerifyData($data, $signature, [Security.Cryptography.HashAlgorithmName]::SHA256,
                              [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    } catch {
        return "the signature could not be read ($($_.Exception.Message))"
    } finally {
        $rsa.Dispose()
    }
    if (-not $ok) { return 'the signature does not match this app''s update key' }
    return $null
}

# Unpacks a zip, entry by entry, refusing any path that would leave the folder.
function Expand-Safely([string]$zip, [string]$into) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($entry in $archive.Entries) {
            $name = $entry.FullName
            if ($name.EndsWith('/') -or $name.EndsWith('\')) { continue }
            $target = Resolve-Inside $into $name
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    } finally {
        $archive.Dispose()
    }
}

# --- Journal: what the apply step did, so it can be undone -------------------

function Add-JournalLine([string]$kind, [string]$relative) {
    $line = "$kind`t$relative" + [Environment]::NewLine
    $stream = [IO.File]::Open($journal, 'Append', 'Write', 'None')
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($line)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
}

$script:steps = 0
function Step {
    $script:steps++
    if ($crashAfter -ge 0 -and $script:steps -gt $crashAfter) { [Environment]::Exit(9) }
}

# Puts back every file an unfinished update moved away, and removes every
# file it added. Safe to run again if it is itself interrupted.
function Undo-Journal {
    $lines = @([IO.File]::ReadAllLines($journal) | Where-Object { $_ })
    [array]::Reverse($lines)
    foreach ($line in $lines) {
        $kind, $relative = $line -split "`t", 2
        $target = Join-Path $Root $relative
        if ($kind -eq 'added') {
            if (Test-Path -LiteralPath $target) { Remove-Item -Force -LiteralPath $target }
        } elseif ($kind -eq 'moved') {
            $saved = Join-Path $backup $relative
            if (Test-Path -LiteralPath $saved) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Move-Item -Force -LiteralPath $saved -Destination $target
            }
        }
    }
    Remove-Item -Force -LiteralPath $journal
    if (Test-Path -LiteralPath $backup) { Remove-Item -Recurse -Force -LiteralPath $backup }
}

# --- 1. An earlier, interrupted update ----------------------------------------

if (Get-Process -Name 'CorporalJackie' -ErrorAction SilentlyContinue) {
    Finish 'Corporal Jackie is running. Close it, then run the update again.' 1
}
if (Test-Path -LiteralPath $journal) {
    Write-Host 'An earlier update did not finish. Putting the previous version back...'
    try { Undo-Journal } catch { Finish "Could not put the previous version back: $($_.Exception.Message). Download the full build." 4 }
    Write-Host 'Done: the previous version is back.'
    if (-not $Package) { Finish 'You can now apply the update again.' 0 }
} elseif (Test-Path -LiteralPath $backup) {
    Remove-Item -Recurse -Force -LiteralPath $backup  # a finished update's leftovers
}

# --- 2. Unpack -------------------------------------------------------------------

if (-not $Package) {
    $downloads = Join-Path $env:USERPROFILE 'Downloads'
    $found = Get-ChildItem -LiteralPath $downloads -Filter 'CorporalJackie-windows-x64-update*.zip' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) { Finish "No update found. Download CorporalJackie-windows-x64-update.zip, then drag it onto 'Update Corporal Jackie.cmd'." 1 }
    $Package = $found.FullName
}
$Package = [IO.Path]::GetFullPath($Package)
Write-Host "Update: $Package"
try {
    if (Test-Path -LiteralPath $Package -PathType Container) {
        $folder = $Package
    } else {
        $staging = Join-Path ([IO.Path]::GetTempPath()) ('cj-update-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $staging | Out-Null
        Expand-Safely $Package $staging
        $folder = $staging
    }
    foreach ($f in 'build.txt', 'manifest.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $folder $f))) { throw "the package has no $f (an older kind of update)" }
    }
} catch {
    Finish "This is not an update package Corporal Jackie can use: $($_.Exception.Message). Nothing was changed." 2
}

# --- 3. Check everything before touching the install -------------------------------

$why = Test-Signature $folder
if ($why) { Finish "Refused: $why. Nothing was changed. Download the full build instead." 2 }

$newBuild = ([IO.File]::ReadAllText((Join-Path $folder 'build.txt'))).Trim()
$installedBuildFile = Join-Path $Root 'build.txt'
$installedBuild = if (Test-Path -LiteralPath $installedBuildFile) { ([IO.File]::ReadAllText($installedBuildFile)).Trim() } else { '' }
if ((Get-BuildNumber $newBuild) -le (Get-BuildNumber $installedBuild)) {
    Finish "Refused: this folder has $installedBuild, and the update is $newBuild, which is not newer. Nothing was changed." 2
}
$installedManifestFile = Join-Path $Root 'manifest.txt'
if (-not (Test-Path -LiteralPath $installedManifestFile)) {
    Finish 'This folder has no manifest.txt (an older build). Download the full build instead. Nothing was changed.' 2
}

$plan = @()       # files the package brings
$remove = @()     # files the new build no longer has
try {
    $manifest = Read-Manifest (Join-Path $folder 'manifest.txt')
    $installed = Read-Manifest $installedManifestFile
    $files = Join-Path $folder 'files'
    $carried = @{}
    if (Test-Path -LiteralPath $files) {
        $files = (Get-Item -LiteralPath $files).FullName.TrimEnd('\', '/')
        foreach ($f in Get-ChildItem -LiteralPath $files -Recurse -Force) {
            if ($f.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "the package holds a link: $($f.FullName)" }
            if ($f.PSIsContainer) { continue }
            $carried[$f.FullName.Substring($files.Length + 1)] = $f.FullName
        }
    }
    foreach ($relative in $carried.Keys) {
        if (-not $manifest.Contains($relative)) { throw "the package holds a file the manifest does not list: $relative" }
        if ((Get-Sha256 $carried[$relative]) -ne $manifest[$relative]) { throw "a file in the package is damaged or was changed: $relative" }
    }
    foreach ($relative in $manifest.Keys) {
        $target = Resolve-Inside $Root $relative
        if ($carried.ContainsKey($relative)) {
            if ((Test-Path -LiteralPath $target) -and (Get-Sha256 $target) -eq $manifest[$relative]) { continue }
            $plan += $relative
        } elseif (-not (Test-Path -LiteralPath $target) -or (Get-Sha256 $target) -ne $manifest[$relative]) {
            throw "this folder is not the build the update starts from ($relative differs)"
        }
    }
    foreach ($relative in $installed.Keys) {
        if (-not $manifest.Contains($relative)) {
            $target = Resolve-Inside $Root $relative
            if (Test-Path -LiteralPath $target) { $remove += $relative }
        }
    }
} catch {
    Finish "Refused: $($_.Exception.Message). Nothing was changed. Download the full build instead." 2
}
# build.txt last: it says which build this folder holds.
$plan = @($plan | Where-Object { $_ -ne 'build.txt' }) + @($plan | Where-Object { $_ -eq 'build.txt' })

# --- 4. Nothing to replace or remove may be in use ---------------------------------

foreach ($relative in @($plan) + @($remove)) {
    $target = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $target)) { continue }
    try {
        [IO.File]::Open($target, 'Open', 'ReadWrite', 'None').Dispose()
    } catch {
        Finish "Refused: $relative is in use. Close Corporal Jackie (and anything using its files), then try again. Nothing was changed." 3
    }
}

# --- 5. Apply, journaled -----------------------------------------------------------------

$manifestSig = Join-Path $folder 'manifest.sig'
try {
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    [IO.File]::WriteAllText($journal, '')
    foreach ($relative in $remove) {
        Add-JournalLine 'moved' $relative
        $saved = Join-Path $backup $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved) | Out-Null
        Move-Item -Force -LiteralPath (Join-Path $Root $relative) -Destination $saved
        Step
    }
    foreach ($relative in $plan) {
        $target = Join-Path $Root $relative
        if (Test-Path -LiteralPath $target) {
            Add-JournalLine 'moved' $relative
            $saved = Join-Path $backup $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved) | Out-Null
            Move-Item -Force -LiteralPath $target -Destination $saved
            Step
        }
        Add-JournalLine 'added' $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -Force -LiteralPath $carried[$relative] -Destination $target
        if ((Get-Sha256 $target) -ne $manifest[$relative]) { throw "copying $relative went wrong" }
        Step
    }
    foreach ($name in 'manifest.txt', 'manifest.sig') {
        $target = Join-Path $Root $name
        if (Test-Path -LiteralPath $target) {
            Add-JournalLine 'moved' $name
            Move-Item -Force -LiteralPath $target -Destination (Join-Path $backup $name)
        }
        if ($name -eq 'manifest.sig' -and -not (Test-Path -LiteralPath $manifestSig)) { continue }
        Add-JournalLine 'added' $name
        Copy-Item -Force -LiteralPath (Join-Path $folder $name) -Destination $target
        Step
    }
} catch {
    $cause = $_.Exception.Message
    try { Undo-Journal } catch { Finish "The update failed ($cause), and putting the old files back failed too ($($_.Exception.Message)). Download the full build." 4 }
    Finish "The update failed ($cause). The previous version is back; nothing was changed." 3
}

# --- 6. Done -------------------------------------------------------------------------------

Remove-Item -Force -LiteralPath $journal
Remove-Item -Recurse -Force -LiteralPath $backup -ErrorAction SilentlyContinue
Finish "Updated to $newBuild`: $($plan.Count) files replaced or added, $($remove.Count) removed. Every file checked." 0
