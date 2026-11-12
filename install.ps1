[CmdletBinding()]
param(
    [string]$Prefix = (Join-Path $env:LOCALAPPDATA 'Programs/Kremen'),
    [string]$SourceRoot = $PSScriptRoot,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$Prefix = [IO.Path]::GetFullPath($Prefix).TrimEnd('\','/')
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
if ($Prefix -eq [IO.Path]::GetPathRoot($Prefix).TrimEnd('\','/')) { throw 'Choose an installation directory, not a drive or share root.' }
$marker = Join-Path $Prefix 'share/kremen/install.marker'
$manifest = Join-Path $Prefix 'share/kremen/install.manifest'
$owned = (Test-Path -LiteralPath $marker -PathType Leaf) -and (Test-Path -LiteralPath $manifest -PathType Leaf) -and
    ([IO.File]::ReadAllText($marker).Trim() -eq 'kremen-native-install-v1')
function Check-Path([string]$Relative) {
    if ($Relative -notmatch '^(bin/kremen(?:-selfhost)?\.exe|share/kremen/(?:selfhost/[^:]+|native/platform\.h|runtime/kremen_rt\.h|install\.(?:marker|manifest))|share/doc/kremen/[^:]+|backend/[^:]+|install\.ps1)$' -or
        $Relative -match '(^|/)(\.|\.\.|)($|/)' -or $Relative.Contains('\')) { throw "Invalid installation manifest path: $Relative" }
    $path = Join-Path $Prefix $Relative
    $current = $path
    while ($current -and $current.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing an installation path through a reparse point: $current" }
        }
        if ($current -eq $Prefix) { break }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    return $path
}
$oldFiles = @()
if ($owned) {
    $oldFiles = [IO.File]::ReadAllLines($manifest)
    foreach ($relative in $oldFiles) { $null = Check-Path $relative }
}
if ($Uninstall) {
    if (-not $owned) { throw 'No installation owned by this installer at the selected prefix.' }
    foreach ($relative in $oldFiles) {
        $path = Check-Path $relative
        if ($relative -ne 'share/kremen/install.manifest' -and (Test-Path -LiteralPath $path -PathType Leaf)) { Remove-Item -LiteralPath $path -Force }
    }
    Remove-Item -LiteralPath $manifest -Force
    function Remove-EmptyDirectory([string]$Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return }
        if ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
        foreach ($directory in @(Get-ChildItem -LiteralPath $Path -Directory -Force)) {
            Remove-EmptyDirectory $directory.FullName
        }
        if (@(Get-ChildItem -LiteralPath $Path -Force).Count -eq 0) { Remove-Item -LiteralPath $Path -Force }
    }
    foreach ($relative in @('backend','share/kremen','share/doc/kremen')) { Remove-EmptyDirectory (Join-Path $Prefix $relative) }
    Write-Output "Removed owned Kremen files from $Prefix"
    return
}
if (-not $owned) {
    foreach ($reserved in @('bin/kremen.exe','bin/kremen-selfhost.exe','share/kremen','share/doc/kremen','backend')) {
        if (Test-Path -LiteralPath (Join-Path $Prefix $reserved)) { throw "Refusing to overwrite an unowned path: $reserved" }
    }
}
$stage = Join-Path ([IO.Path]::GetTempPath()) ('kremen-install-' + [Guid]::NewGuid().ToString('N'))
try {
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    if (Test-Path -LiteralPath (Join-Path $SourceRoot 'bin/kremen.exe')) {
        foreach ($directory in @('bin','share','backend')) {
            $from = Join-Path $SourceRoot $directory
            if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination $stage -Recurse }
        }
    } else {
        if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot 'build/kremen.exe'))) { & (Join-Path $SourceRoot 'build.ps1') -Selfhost }
        [IO.Directory]::CreateDirectory((Join-Path $stage 'bin')) | Out-Null
        Copy-Item -LiteralPath (Join-Path $SourceRoot 'build/kremen.exe') -Destination (Join-Path $stage 'bin/kremen.exe')
        $subset = Join-Path $SourceRoot 'selfhost/build/kremen-selfhost.exe'
        if (Test-Path -LiteralPath $subset) { Copy-Item -LiteralPath $subset -Destination (Join-Path $stage 'bin/kremen-selfhost.exe') }
        foreach ($directory in @('runtime','selfhost','native')) {
            [IO.Directory]::CreateDirectory((Join-Path $stage "share/kremen/$directory")) | Out-Null
        }
        Copy-Item -LiteralPath (Join-Path $SourceRoot 'runtime/kremen_rt.h') -Destination (Join-Path $stage 'share/kremen/runtime')
        Copy-Item -LiteralPath (Join-Path $SourceRoot 'native/platform.h') -Destination (Join-Path $stage 'share/kremen/native')
        foreach ($name in @('bootstrap.kr','toolchain.kr','stage0.c','runtime.c','runtime.h','driver.c','host.c','host.h','platform.h')) {
            Copy-Item -LiteralPath (Join-Path $SourceRoot "selfhost/$name") -Destination (Join-Path $stage 'share/kremen/selfhost')
        }
        [IO.Directory]::CreateDirectory((Join-Path $stage 'share/doc/kremen')) | Out-Null
        foreach ($name in @('docs','examples','README.md','CHANGELOG.md','LICENSE')) {
            Copy-Item -LiteralPath (Join-Path $SourceRoot $name) -Destination (Join-Path $stage 'share/doc/kremen') -Recurse
        }
    }
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $stage 'install.ps1')
    $files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\','/') })
    $files = @($files | Where-Object { $_ -notin @('share/kremen/install.marker','share/kremen/install.manifest') }) + @('share/kremen/install.marker','share/kremen/install.manifest')
    foreach ($relative in $files) { $null = Check-Path $relative }
    [IO.Directory]::CreateDirectory((Join-Path $stage 'share/kremen')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $stage 'share/kremen/install.marker'), "kremen-native-install-v1`n", (New-Object Text.UTF8Encoding $false))
    [IO.File]::WriteAllText((Join-Path $stage 'share/kremen/install.manifest'), (($files | Sort-Object) -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))
    foreach ($relative in $files) {
        $destination = Check-Path $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $stage $relative) -Destination $destination -Force
    }
    Write-Output "Installed Kremen in $Prefix; add $Prefix\bin to PATH if desired."
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
