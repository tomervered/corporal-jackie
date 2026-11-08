# Applies an update package (see README-update.txt) to the folder it was
# unzipped into: moves the new and changed files in, deletes the removed ones,
# then checks every file against the new build's manifest.
$ErrorActionPreference = 'Stop'
$update = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $update

function Finish([string]$message, [int]$code) {
    Write-Host ''
    Write-Host $message
    Read-Host 'Press Enter to close' | Out-Null
    exit $code
}

function CleanUp {
    Remove-Item -Recurse -Force $update -ErrorAction SilentlyContinue
    Remove-Item -Force (Join-Path $root 'README-update.txt') -ErrorAction SilentlyContinue
}

$from = (Get-Content -Raw (Join-Path $update 'from-build.txt')).Trim()
$buildFile = Join-Path $root 'build.txt'
$installed = if (Test-Path $buildFile) { (Get-Content -Raw $buildFile).Trim() } else { '' }
if ($installed -ne $from) {
    CleanUp
    $have = if ($installed) { $installed } else { 'an older build without build.txt' }
    Finish "This update starts from $from, but this folder has $have. Nothing was changed. Download the full build instead." 1
}
if (Get-Process -Name 'CorporalJackie' -ErrorAction SilentlyContinue) {
    Finish 'Corporal Jackie is running. Close it, then double-click apply-update.cmd again.' 1
}

$files = Join-Path $update 'files'
$moved = 0
Get-ChildItem -Recurse -File $files | ForEach-Object {
    $relative = $_.FullName.Substring($files.Length + 1)
    $target = Join-Path $root $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Move-Item -Force -LiteralPath $_.FullName -Destination $target
    $moved++
}
$removed = 0
Get-Content (Join-Path $update 'removed.txt') | Where-Object { $_ } | ForEach-Object {
    $path = Join-Path $root $_
    if (Test-Path -LiteralPath $path) { Remove-Item -Force -LiteralPath $path; $removed++ }
}

# Every file of the new build, as CI built it.
$bad = @()
Get-Content (Join-Path $root 'manifest.txt') | Where-Object { $_ } | ForEach-Object {
    $hash, $relative = $_ -split "`t", 2
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower() -ne $hash) {
        $bad += $relative
    }
}
CleanUp
$build = (Get-Content -Raw $buildFile).Trim()
if ($bad.Count -gt 0) {
    Finish ("Updated $moved files, but these do not match $build`n  " + ($bad -join "`n  ") + "`nDownload the full build instead.") 2
}
Finish "Updated to $build`: $moved files replaced or added, $removed removed. Every file checked." 0
