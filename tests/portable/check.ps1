#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$PSNativeCommandArgumentPassing = 'Standard'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$Work = Join-Path ([IO.Path]::GetDirectoryName($PackageRoot)) ('portable кир Ω ' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($Work) | Out-Null
$OldPath = $env:PATH
$OldCC = $env:CC
$OldSupport = $env:KREMEN_SELFHOST_ROOT
$OldInclude = $env:INCLUDE
$OldLib = $env:LIB
$OldLibPath = $env:LIBPATH
$Total = 0
$Utf8 = [Text.UTF8Encoding]::new($false)
function Pass([string]$Label) { $script:Total++; Write-Output "ok portable048.$script:Total - $Label" }
function Invoke-Portable([string[]]$CommandArguments, [int]$Expected = 0, [string]$Executable = $script:Kremen) {
    $OutputFile = Join-Path $Work 'output.txt'
    $ErrorFile = Join-Path $Work 'error.txt'
    & $Executable @CommandArguments 1> $OutputFile 2> $ErrorFile
    if ($LASTEXITCODE -ne $Expected) { throw "Unexpected status $LASTEXITCODE for $Executable $CommandArguments : $([IO.File]::ReadAllText($ErrorFile))" }
    return [IO.File]::ReadAllText($OutputFile).TrimEnd("`r","`n")
}
function Dependencies([string]$Binary) {
    $Data = [IO.File]::ReadAllBytes($Binary)
    $Pe = [BitConverter]::ToUInt32($Data, 60)
    if ([BitConverter]::ToUInt32($Data, $Pe) -ne 0x4550) { throw 'Invalid PE file.' }
    $Count = [BitConverter]::ToUInt16($Data, $Pe + 6)
    $Size = [BitConverter]::ToUInt16($Data, $Pe + 20)
    $Optional = $Pe + 24
    if ([BitConverter]::ToUInt16($Data, $Optional) -ne 0x20b) { throw 'Expected an x86-64 PE executable.' }
    $Import = [BitConverter]::ToUInt32($Data, $Optional + 120)
    if (-not $Import) { return }
    $Sections = @()
    for ($Index = 0; $Index -lt $Count; $Index++) {
        $At = $Optional + $Size + $Index * 40
        $Sections += @{ Virtual = [BitConverter]::ToUInt32($Data, $At + 12); Raw = [BitConverter]::ToUInt32($Data, $At + 20); Size = [Math]::Max([BitConverter]::ToUInt32($Data, $At + 8), [BitConverter]::ToUInt32($Data, $At + 16)) }
    }
    function Offset([uint32]$Rva) {
        foreach ($Section in $Sections) {
            if ($Rva -ge $Section.Virtual -and $Rva -lt $Section.Virtual + $Section.Size) { return [int]($Section.Raw + $Rva - $Section.Virtual) }
        }
        throw 'PE import RVA is outside its sections.'
    }
    $At = Offset $Import
    for ($Index = 0; $Index -lt 4096; $Index++) {
        $NameRva = [BitConverter]::ToUInt32($Data, $At + 12)
        if (-not $NameRva) { return }
        $NameAt = Offset $NameRva
        $End = $NameAt
        while ($End -lt $Data.Length -and $Data[$End]) { $End++ }
        [Text.Encoding]::ASCII.GetString($Data, $NameAt, $End - $NameAt)
        $At += 20
    }
    throw 'PE import descriptor limit exceeded.'
}
try {
    foreach ($Line in [IO.File]::ReadAllLines((Join-Path $PackageRoot 'SHA256SUMS'))) {
        if ($Line -notmatch '^([a-f0-9]{64})  \./(.+)$') { throw 'Invalid package checksum manifest.' }
        $Path = Join-Path $PackageRoot $Matches[2]
        if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Matches[1]) { throw "Package checksum mismatch: $Path" }
    }
    Pass 'complete portable archive checksum manifest'
    $Moved = Join-Path $Work 'пакет 🌍 with spaces'
    Move-Item -LiteralPath $PackageRoot -Destination $Moved
    $PackageRoot = $Moved
    $script:Kremen = Join-Path $PackageRoot 'bin/kremen.exe'
    $Backend = Join-Path $PackageRoot 'backend/bin/clang.exe'
    $env:PATH = "$env:WINDIR\System32;$env:WINDIR"
    Remove-Item Env:CC -ErrorAction SilentlyContinue
    Remove-Item Env:KREMEN_SELFHOST_ROOT -ErrorAction SilentlyContinue
    Remove-Item Env:INCLUDE -ErrorAction SilentlyContinue
    Remove-Item Env:LIB -ErrorAction SilentlyContinue
    Remove-Item Env:LIBPATH -ErrorAction SilentlyContinue
    $Doctor = Invoke-Portable @('doctor')
    if (-not $Doctor.Contains('Worker spawn/join: OK') -or -not $Doctor.Contains('backend/bin/clang.exe')) { throw 'Bundled backend was not selected.' }
    Pass 'Unicode relocation and doctor without developer tools on PATH'
    $Source = Join-Path $Work 'задача 🌍.kr'
    Copy-Item -LiteralPath (Join-Path $Root 'tests/windows/runtime048.kr') -Destination $Source
    Push-Location $Work
    try {
        foreach ($Mode in @('work','learn')) {
            if ((Invoke-Portable @('run',$Source,"--mode=$Mode")) -ne 'native runtime 048 ok') { throw "Runtime output differs in $Mode." }
            Pass "$Mode Unicode filesystem and native worker runtime"
        }
        $CSource = Join-Path $Work 'generated.c'
        Invoke-Portable @('emit-c',$Source,'-o',$CSource) | Out-Null
        $Program = Join-Path $Work 'program.exe'
        Invoke-Portable @('-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',$CSource,'-o',$Program) 0 $Backend | Out-Null
        if ((Invoke-Portable @() 0 $Program) -ne 'native runtime 048 ok') { throw 'Standalone C runtime failed.' }
        Pass 'emitted standalone C compiles with only the bundled backend'
    } finally { Pop-Location }
    foreach ($Binary in @($Kremen, (Join-Path $PackageRoot 'bin/kremen-selfhost.exe'), $Backend, (Join-Path $Work 'program.exe'), (Join-Path $PackageRoot 'backend/zig.exe'))) {
        foreach ($Library in @(Dependencies $Binary)) {
            if ($Library -match '(?i)(winpthread|msys-|cygwin|libgcc|libstdc\+\+|vcruntime|msvcp|libclang|libllvm)') { throw "External runtime DLL required by $Binary : $Library" }
        }
    }
    Pass 'native binaries and generated code have no compatibility or developer runtime DLL dependency'
    $env:CC = Join-Path $Work 'missing compiler.exe'
    $null = Invoke-Portable @('doctor','--diagnostic-format=json') 1
    if (([IO.File]::ReadAllText((Join-Path $Work 'error.txt')) | ConvertFrom-Json).code -ne 'E016') { throw 'CC override did not take precedence.' }
    Remove-Item Env:CC
    Pass 'an explicit CC override retains precedence over the bundled backend'
    $Subset = Join-Path $Work 'subset.kr'
    [IO.File]::WriteAllText($Subset, 'func main() i64 { return 7 }', $Utf8)
    Invoke-Portable @('selfhost','check',$Subset) | Out-Null
    Invoke-Portable @('selfhost','run',$Subset) 7 | Out-Null
    Pass 'native subset compiles and executes through the bundled backend'
    $Rebuilt = Join-Path $PackageRoot 'bin/rebuilt-selfhost.exe'
    Invoke-Portable @('selfhost','bootstrap','-o',$Rebuilt) | Out-Null
    Invoke-Portable @('check',$Subset) 0 $Rebuilt | Out-Null
    Remove-Item -LiteralPath $Rebuilt
    Pass 'bundled backend reproduces and runs the subset compiler and driver'
    $Prefix = Join-Path $Work 'установленная копия'
    [IO.Directory]::CreateDirectory((Join-Path $Prefix 'bin')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $Prefix 'bin/unrelated.txt'), 'retain', $Utf8)
    & (Join-Path $PackageRoot 'install.ps1') -SourceRoot $PackageRoot -Prefix $Prefix
    Pass 'prebuilt installation copies the native compiler and SDK'
    if (-not (Invoke-Portable @('doctor') 0 (Join-Path $Prefix 'bin/kremen.exe')).Contains('Worker spawn/join: OK')) { throw 'Installed doctor failed.' }
    Pass 'installed portable compiler discovers its own bundled backend'
    & (Join-Path $Prefix 'install.ps1') -Prefix $Prefix -Uninstall
    if ((Test-Path -LiteralPath (Join-Path $Prefix 'bin/kremen.exe')) -or (Test-Path -LiteralPath (Join-Path $Prefix 'backend/zig.exe'))) { throw 'Owned installation files remain.' }
    if ([IO.File]::ReadAllText((Join-Path $Prefix 'bin/unrelated.txt')) -ne 'retain') { throw 'Uninstall removed unrelated data.' }
    Pass 'prebuilt uninstall removes owned files and preserves unrelated data'
    Write-Output "Portable Windows 0.4.8 checks passed: $Total."
} finally {
    $env:PATH = $OldPath
    $env:CC = $OldCC
    $env:KREMEN_SELFHOST_ROOT = $OldSupport
    $env:INCLUDE = $OldInclude
    $env:LIB = $OldLib
    $env:LIBPATH = $OldLibPath
    if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
}
