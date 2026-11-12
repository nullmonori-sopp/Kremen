#requires -Version 7.3
param(
    [string]$Compiler = $(if ($env:CC) { $env:CC } else { "clang" }),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot "build")
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$PSNativeCommandArgumentPassing = 'Standard'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "Use selfhost/bootstrap.sh on POSIX systems."
}
$SourceDirectory = $PSScriptRoot
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
[IO.Directory]::CreateDirectory($BuildDirectory) | Out-Null
$Strict = @("-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-D_CRT_SECURE_NO_WARNINGS", "-D_CRT_NONSTDC_NO_WARNINGS")
$Target = (& $Compiler '-dumpmachine' | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'The selected source-build compiler must support the Clang/GCC command-line interface.' }
if ($Target -match 'msvc') { $Strict += '-fms-runtime-lib=static' }
$Utf8 = [Text.UTF8Encoding]::new($false)

function Invoke-Tool([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed with status ${LASTEXITCODE}: $Executable"
    }
}
function Build-Stage([string]$Source, [string]$Output) {
    Invoke-Tool $Compiler ($Strict + @($Source, (Join-Path $SourceDirectory "runtime.c"),
        (Join-Path $SourceDirectory "driver.c"), "-o", $Output))
}
function Assert-SameFile([string]$Left, [string]$Right) {
    if ((Get-FileHash -LiteralPath $Left -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $Right -Algorithm SHA256).Hash) {
        throw "Bootstrap fixed point differs: $Left and $Right"
    }
}
function Assert-Rejected([string]$Source, [string]$Message) {
    $InputPath = Join-Path $BuildDirectory "invalid.kr"
    $ErrorPath = Join-Path $BuildDirectory "invalid.error"
    [IO.File]::WriteAllText($InputPath, $Source, $Utf8)
    & (Join-Path $BuildDirectory "stage2.exe") $InputPath (Join-Path $BuildDirectory "invalid.c") 2> $ErrorPath
    if ($LASTEXITCODE -ne 1) { throw "Malformed bootstrap input did not fail normally." }
    if (![IO.File]::ReadAllText($ErrorPath, $Utf8).Contains($Message)) {
        throw "Unexpected bootstrap diagnostic: $([IO.File]::ReadAllText($ErrorPath, $Utf8))"
    }
}

$BootstrapSource = Join-Path $SourceDirectory "bootstrap.kr"
$Seed = Join-Path $SourceDirectory "stage0.c"
Build-Stage $Seed (Join-Path $BuildDirectory "stage0.exe")
Invoke-Tool (Join-Path $BuildDirectory "stage0.exe") @($BootstrapSource, (Join-Path $BuildDirectory "stage1.c"))
Build-Stage (Join-Path $BuildDirectory "stage1.c") (Join-Path $BuildDirectory "stage1.exe")
Invoke-Tool (Join-Path $BuildDirectory "stage1.exe") @($BootstrapSource, (Join-Path $BuildDirectory "stage2.c"))
Assert-SameFile $Seed (Join-Path $BuildDirectory "stage1.c")
Assert-SameFile (Join-Path $BuildDirectory "stage1.c") (Join-Path $BuildDirectory "stage2.c")
Build-Stage (Join-Path $BuildDirectory "stage2.c") (Join-Path $BuildDirectory "stage2.exe")
Invoke-Tool (Join-Path $BuildDirectory "stage2.exe") @($BootstrapSource, (Join-Path $BuildDirectory "stage3.c"))
Assert-SameFile (Join-Path $BuildDirectory "stage2.c") (Join-Path $BuildDirectory "stage3.c")

Invoke-Tool (Join-Path $BuildDirectory "stage2.exe") @((Join-Path $SourceDirectory "example.kr"), (Join-Path $BuildDirectory "example.c"))
Build-Stage (Join-Path $BuildDirectory "example.c") (Join-Path $BuildDirectory "example.exe")
Invoke-Tool (Join-Path $BuildDirectory "example.exe") @((Join-Path $BuildDirectory "example.output"))
[IO.File]::WriteAllText((Join-Path $BuildDirectory "example.expected"), "720|18|ok`n", $Utf8)
Assert-SameFile (Join-Path $BuildDirectory "example.expected") (Join-Path $BuildDirectory "example.output")

Assert-Rejected "func main() i64 {`n" "expected '}' before end of file"
Assert-Rejected "func main() i64 { return @ }`n" "unexpected character: @"
Assert-Rejected "func main() i64 { return 010 }`n" "decimal integer literals must not have leading zeros"
Assert-Rejected ("func main() i64 { return " + ("(" * 200) + "1" + (")" * 200) + " }`n") "parser nesting limit (128) exceeded"

Write-Output "Self-hosting fixed point verified: seed == stage1.c == stage2.c == stage3.c."
Write-Output "Stage2 compiled and ran the example; diagnostic checks passed."

Invoke-Tool (Join-Path $BuildDirectory "stage2.exe") @((Join-Path $SourceDirectory "toolchain.kr"), (Join-Path $BuildDirectory "toolchain.c"))
Invoke-Tool $Compiler ($Strict + @("-Dkr_main=kr_bootstrap_entry", "-c", (Join-Path $BuildDirectory "stage2.c"),
    "-o", (Join-Path $BuildDirectory "compiler.o")))
$StagedDriver = Join-Path $BuildDirectory "kremen-selfhost.next.exe"
Invoke-Tool $Compiler ($Strict + @((Join-Path $BuildDirectory "compiler.o"), (Join-Path $BuildDirectory "toolchain.c"),
    (Join-Path $SourceDirectory "runtime.c"), (Join-Path $SourceDirectory "host.c"),
    (Join-Path $SourceDirectory "driver.c"), "-o", $StagedDriver))
$Driver = Join-Path $BuildDirectory "kremen-selfhost.exe"
if ([IO.File]::Exists($Driver)) { [IO.File]::Replace($StagedDriver, $Driver, $null) }
else { [IO.File]::Move($StagedDriver, $Driver) }
Write-Output "Kremen-written selfhost driver built: check, emit-c, build, run, bootstrap."
