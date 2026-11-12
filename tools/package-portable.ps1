# Assemble a native Windows package, including the complete official Zig SDK.
# Use -BackendRoot for an offline operation, or explicitly request a download.
[CmdletBinding()]
param(
    [string]$BackendRoot,
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$Selfhost,
    [switch]$DownloadBackend
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
    throw 'This packager targets Windows x86-64.'
}
$Root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $Root 'build/cmake' }
if (-not $OutputDir) { $OutputDir = Join-Path $Root 'build/portable' }
if ($DownloadBackend -and $BackendRoot) { throw 'Select -BackendRoot or -DownloadBackend, not both.' }
$ZigVersion = '0.15.2'
$null = New-Item -ItemType Directory -Path $OutputDir -Force
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
if ($DownloadBackend) {
    $Cache = Join-Path $OutputDir 'backend-download'
    $null = New-Item -ItemType Directory -Path $Cache -Force
    $Index = Invoke-RestMethod -Uri 'https://ziglang.org/download/index.json'
    $Release = $Index.PSObject.Properties[$ZigVersion]
    if (-not $Release) { throw "Official Zig catalog has no release $ZigVersion." }
    $Target = $Release.Value.PSObject.Properties['x86_64-windows']
    if (-not $Target) { throw 'Official Zig catalog has no Windows x86-64 SDK.' }
    $Entry = $Target.Value
    $ExpectedUrl = "https://ziglang.org/download/$ZigVersion/zig-x86_64-windows-$ZigVersion.zip"
    if ($Entry.tarball -ne $ExpectedUrl -or $Entry.shasum -notmatch '^[a-f0-9]{64}$') {
        throw 'Official Zig catalog does not describe the pinned Windows SDK.'
    }
    $Archive = Join-Path $Cache "zig-x86_64-windows-$ZigVersion.zip"
    Invoke-WebRequest -UseBasicParsing -Uri $Entry.tarball -OutFile "$Archive.partial"
    $Actual = (Get-FileHash -Algorithm SHA256 -LiteralPath "$Archive.partial").Hash.ToLowerInvariant()
    if ($Actual -ne $Entry.shasum) {
        Remove-Item -LiteralPath "$Archive.partial" -Force
        throw 'Zig archive checksum mismatch.'
    }
    Move-Item -LiteralPath "$Archive.partial" -Destination $Archive -Force
    $BackendRoot = Join-Path $Cache "zig-x86_64-windows-$ZigVersion"
    if (Test-Path -LiteralPath $BackendRoot) { Remove-Item -LiteralPath $BackendRoot -Recurse -Force }
    Expand-Archive -LiteralPath $Archive -DestinationPath $Cache
}
if (-not $BackendRoot) { throw 'Supply -BackendRoot or -DownloadBackend.' }
$BackendRoot = (Resolve-Path -LiteralPath $BackendRoot).Path
foreach ($Required in @('zig.exe', 'lib', 'LICENSE')) {
    if (-not (Test-Path -LiteralPath (Join-Path $BackendRoot $Required))) {
        throw 'Backend root must contain the complete Zig SDK (zig.exe, lib, LICENSE).'
    }
}
$ObservedVersion = & (Join-Path $BackendRoot 'zig.exe') version
if ($LASTEXITCODE -ne 0 -or "$ObservedVersion".Trim() -ne $ZigVersion) { throw "Expected Zig $ZigVersion." }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
function Find-Binary([string]$Name) {
    foreach ($Relative in @($Name, "Release/$Name", "bin/$Name", "bin/Release/$Name")) {
        $Candidate = Join-Path $BuildDir $Relative
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) { return $Candidate }
    }
    throw "Missing built executable $Name in $BuildDir."
}
$Compiler = Find-Binary 'kremen.exe'
$Launcher = Find-Binary 'kremen-backend.exe'
if (-not $Selfhost) {
    foreach ($Candidate in @(
        (Join-Path $BuildDir 'kremen-selfhost.exe'),
        (Join-Path $Root 'selfhost/build/kremen-selfhost.exe')
    )) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) { $Selfhost = $Candidate; break }
    }
}
if ($Selfhost -and -not (Test-Path -LiteralPath $Selfhost -PathType Leaf)) { throw "Missing self-hosted executable $Selfhost." }
$Version = (Get-Content -LiteralPath (Join-Path $Root 'VERSION') -Raw).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain a numeric release version.' }
$Name = "kremen-v$Version-windows-x86_64"
$Stage = Join-Path $OutputDir ('.portable-stage-' + [Guid]::NewGuid().ToString('N'))
$Dest = Join-Path $Stage $Name
try {
    foreach ($Directory in @('bin', 'backend', 'share/kremen/runtime', 'share/doc/kremen')) {
        $null = New-Item -ItemType Directory -Path (Join-Path $Dest $Directory) -Force
    }
    Copy-Item -LiteralPath $Compiler -Destination (Join-Path $Dest 'bin/kremen.exe')
    # Retain all SDK components and license notices; do not prune its libc/LLVM files.
    Get-ChildItem -LiteralPath $BackendRoot -Force | Copy-Item -Destination (Join-Path $Dest 'backend') -Recurse -Force
    $null = New-Item -ItemType Directory -Path (Join-Path $Dest 'backend/bin') -Force
    Copy-Item -LiteralPath $Launcher -Destination (Join-Path $Dest 'backend/bin/clang.exe') -Force
    Copy-Item -LiteralPath (Join-Path $Root 'runtime/kremen_rt.h') -Destination (Join-Path $Dest 'share/kremen/runtime/kremen_rt.h')
    Copy-Item -LiteralPath (Join-Path $Root 'LICENSE') -Destination (Join-Path $Dest 'LICENSE')
    Copy-Item -LiteralPath (Join-Path $Root 'LICENSE') -Destination (Join-Path $Dest 'share/doc/kremen/LICENSE')
    Copy-Item -LiteralPath (Join-Path $Root 'VERSION') -Destination (Join-Path $Dest 'VERSION')
    Copy-Item -LiteralPath (Join-Path $Root 'install.ps1') -Destination (Join-Path $Dest 'install.ps1')
    Get-ChildItem -LiteralPath $Root -Filter '*.md' -File | Copy-Item -Destination (Join-Path $Dest 'share/doc/kremen')
    foreach ($Directory in @('docs', 'examples')) {
        $From = Join-Path $Root $Directory
        $To = Join-Path $Dest "share/doc/kremen/$Directory"
        $null = New-Item -ItemType Directory -Path $To -Force
        foreach ($File in Get-ChildItem -LiteralPath $From -Recurse -File -Force) {
            if ($File.Name -eq 'A.a') { continue }
            $Relative = $File.FullName.Substring($From.Length).TrimStart([char[]]'\/')
            $TargetPath = Join-Path $To $Relative
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $TargetPath) -Force
            Copy-Item -LiteralPath $File.FullName -Destination $TargetPath
        }
    }
    $Subset = 'no'
    if ($Selfhost) {
        Copy-Item -LiteralPath $Selfhost -Destination (Join-Path $Dest 'bin/kremen-selfhost.exe')
        $null = New-Item -ItemType Directory -Path (Join-Path $Dest 'share/kremen/selfhost') -Force
        $null = New-Item -ItemType Directory -Path (Join-Path $Dest 'share/kremen/native') -Force
        foreach ($Support in @('bootstrap.kr', 'toolchain.kr', 'stage0.c', 'runtime.c', 'runtime.h', 'driver.c', 'host.c', 'host.h', 'platform.h')) {
            Copy-Item -LiteralPath (Join-Path $Root "selfhost/$Support") -Destination (Join-Path $Dest "share/kremen/selfhost/$Support")
        }
        Copy-Item -LiteralPath (Join-Path $Root 'native/platform.h') -Destination (Join-Path $Dest 'share/kremen/native/platform.h')
        $Subset = 'yes'
    }
    $Readme = @"
# Kremen $Version portable package

Run .\bin\kremen.exe doctor, then:
.\bin\kremen.exe run .\share\doc\kremen\examples\hello.kr

The package uses its bundled backend automatically when CC and --cc are omitted.
It needs no download, MSYS2, Visual Studio, separately installed compiler, or PATH
change during ordinary use. The Zig backend keeps its own compilation cache in
the user's cache directory. This package targets Windows x86-64 and still uses
the operating system's standard Windows/UCRT services.
To copy the prebuilt files into a prefix, run .\install.ps1 -Prefix C:\your\directory.
The same installer accepts -Uninstall and removes only its owned files.
Self-hosted subset included: $Subset.

Kremen's license is in LICENSE. The complete unmodified Zig $ZigVersion SDK,
including its license and bundled LLVM/libc notices, is in backend. The tiny
backend/bin/clang.exe adapter forwards arguments to backend/zig.exe cc without
a command shell. See share/doc/kremen/docs for platform and language details.
"@
    $Utf8 = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $Dest 'README-PORTABLE.md'), $Readme.Replace("`r`n", "`n") + "`n", $Utf8)
    $Info = "Kremen version: $Version`nBackend: Zig $ZigVersion`nTarget: windows-x86_64`nSelf-hosted subset: $Subset`n"
    [IO.File]::WriteAllText((Join-Path $Dest 'PACKAGE-INFO.txt'), $Info, $Utf8)
    $Checksums = foreach ($File in Get-ChildItem -LiteralPath $Dest -Recurse -File -Force | Sort-Object FullName) {
        $Relative = $File.FullName.Substring($Dest.Length + 1).Replace('\', '/')
        '{0}  ./{1}' -f (Get-FileHash -Algorithm SHA256 -LiteralPath $File.FullName).Hash.ToLowerInvariant(), $Relative
    }
    [IO.File]::WriteAllText((Join-Path $Dest 'SHA256SUMS'), ($Checksums -join "`n") + "`n", $Utf8)
    $Archive = Join-Path $OutputDir "$Name-portable.zip"
    $Partial = Join-Path $OutputDir "$Name.partial.zip"
    if (Test-Path -LiteralPath $Partial) { Remove-Item -LiteralPath $Partial -Force }
    # Compress-Archive omits hidden files. ZipFile retains the full SDK,
    # including any hidden notices, and writes UTF-8 entry names.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($Stage, $Partial, [IO.Compression.CompressionLevel]::Optimal, $false)
    Move-Item -LiteralPath $Partial -Destination $Archive -Force
    $Sum = (Get-FileHash -Algorithm SHA256 -LiteralPath $Archive).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$Archive.sha256", "$Sum  $(Split-Path -Leaf $Archive)`n", $Utf8)
    Write-Output "Created $Archive"
} finally {
    if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
}
