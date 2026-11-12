#requires -Version 7.3
param(
    [string]$Toolchain = (Join-Path $PSScriptRoot "../../selfhost/build/kremen-selfhost.exe"),
    [string]$Compiler = $(if ($env:CC) { $env:CC } else { "clang" })
)
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$PSNativeCommandArgumentPassing = 'Standard'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw "This target requires native Windows." }
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$Toolchain = [IO.Path]::GetFullPath($Toolchain)
$Work = Join-Path ([IO.Path]::GetTempPath()) ("kremen selfhost кир Ω " + [Guid]::NewGuid().ToString("N"))
[IO.Directory]::CreateDirectory($Work) | Out-Null
$Utf8 = [Text.UTF8Encoding]::new($false)
$Total = 0
$OriginalOverride = $env:KREMEN_SELFHOST_ROOT
function Pass([string]$Label) { $script:Total++; Write-Output "ok selfhost048.$script:Total - $Label" }
function Invoke-Driver([string[]]$Arguments, [int]$Expected = 0, [string]$Executable = $Toolchain) {
    $ErrorFile = Join-Path $Work "error.txt"
    $Text = & $Executable @Arguments 2> $ErrorFile
    if ($LASTEXITCODE -ne $Expected) { throw "Unexpected status ${LASTEXITCODE}: $([IO.File]::ReadAllText($ErrorFile, $Utf8))" }
    return ($Text -join "`n")
}
function Write-Source([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $Utf8) }
try {
    if (!(Invoke-Driver @("--version")).Contains("0.4.8")) { throw "Wrong driver version." }
    Pass "native subset driver version"
    $Source = Join-Path $Work "задача Ω.kr"
    Write-Source $Source "func main() i64 { return 0 }`n"
    Invoke-Driver @("check", $Source, "--cc", $Compiler) | Out-Null
    Pass "check accepts Unicode source paths"
    $CSource = Join-Path $Work "исходник.c"
    Invoke-Driver @("emit-c", $Source, "-o", $CSource) | Out-Null
    if (!(Test-Path -LiteralPath $CSource)) { throw "C output missing." }
    Pass "emit-c publishes a Unicode output path"
    Push-Location $Work
    try { Invoke-Driver @("build", $Source, "--cc", $Compiler) | Out-Null }
    finally { Pop-Location }
    $DefaultProgram = Join-Path $Work "a.exe"
    if (!(Test-Path -LiteralPath $DefaultProgram)) { throw "Default Windows executable name missing." }
    & $DefaultProgram
    if ($LASTEXITCODE -ne 0) { throw "Default executable failed." }
    Pass "default build publishes runnable a.exe"
    $ArgumentsSource = Join-Path $Work "аргументы.kr"
    Write-Source $ArgumentsSource @'
extern func rt_arg(index i64) string
extern func rt_cat(left string, right string) string
extern func rt_write(path string, value string) i64
func main() i64 {
    unsafe { return rt_write(rt_arg(1), rt_cat(rt_arg(2), rt_cat("|", rt_arg(3)))) }
}
'@
    $ArgumentOutput = Join-Path $Work "аргументы.txt"
    $Literal = 'кириллица Ω space & "quote" slash\'
    Invoke-Driver @("run", $ArgumentsSource, "--cc", $Compiler, "--", $ArgumentOutput, $Literal, "") | Out-Null
    if ([IO.File]::ReadAllText($ArgumentOutput, $Utf8) -ne ($Literal + "|")) { throw "Literal arguments changed." }
    Pass "run preserves Unicode quotes backslashes and empty arguments"
    $Before = (Get-FileHash -LiteralPath $Source).Hash
    Invoke-Driver @("emit-c", $Source, "-o", $Source) 1 | Out-Null
    if ((Get-FileHash -LiteralPath $Source).Hash -ne $Before) { throw "Source was overwritten." }
    Pass "source output protection remains active"
    $Existing = Join-Path $Work "existing.exe"
    Write-Source $Existing "previous artifact"
    Invoke-Driver @("build", $Source, "--cc", (Join-Path $Work "missing.exe"), "-o", $Existing) 127 | Out-Null
    if ([IO.File]::ReadAllText($Existing, $Utf8) -ne "previous artifact") { throw "Backend failure replaced existing output." }
    Pass "backend launch failure preserves status and old artifact"
    $Invalid = Join-Path $Work "invalid.kr"
    Write-Source $Invalid "func main() i64 { return @ }`n"
    Invoke-Driver @("build", $Invalid, "--cc", $Compiler, "-o", $Existing) 1 | Out-Null
    if ([IO.File]::ReadAllText($Existing, $Utf8) -ne "previous artifact") { throw "Source failure replaced output." }
    Pass "frontend failure preserves old output"
    $Return = Join-Path $Work "return.kr"
    Write-Source $Return "func main() i64 { return 7 }`n"
    Invoke-Driver @("run", $Return, "--cc", $Compiler) 7 | Out-Null
    Pass "run preserves program exit status"
    $Fixture = Join-Path $Work "output-fixture.exe"
    & $Compiler '-std=c11' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-O1' '-D_CRT_SECURE_NO_WARNINGS' '-D_CRT_NONSTDC_NO_WARNINGS' `
        (Join-Path $Root 'tests/selfhost/output_048.c') (Join-Path $Root 'selfhost/runtime.c') `
        (Join-Path $Root 'selfhost/driver.c') '-o' $Fixture
    if ($LASTEXITCODE -ne 0) { throw 'Subset output fixture compilation failed.' }
    Push-Location $Work
    try {
        $Text = Invoke-Driver @('sticky') 1 $Fixture
        if ($Text -ne '') { throw 'A failed subset stream produced unexpected output.' }
        if (![IO.File]::ReadAllText((Join-Path $Work 'error.txt'), $Utf8).Contains('standard output failed')) { throw 'Missing subset output failure diagnostic.' }
        Pass 'subset entry shim rejects a sticky output failure'
        Invoke-Driver @('sticky-status') 7 $Fixture | Out-Null
        Pass 'subset entry shim preserves an existing failure status'
    }
    finally { Pop-Location }
    $Rebuilt = Join-Path $Work "новый driver.exe"
    Invoke-Driver @("bootstrap", "--cc", $Compiler, "-o", $Rebuilt) | Out-Null
    Invoke-Driver @("check", $Source, "--cc", $Compiler) 0 $Rebuilt | Out-Null
    Pass "native driver rebuild verifies compiler and driver fixed points"
    $Prefix = Join-Path $Work "пакет"
    $Support = Join-Path $Prefix "share/kremen/selfhost"
    $Native = Join-Path $Prefix "share/kremen/native"
    [IO.Directory]::CreateDirectory($Support) | Out-Null
    [IO.Directory]::CreateDirectory($Native) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $Prefix "bin")) | Out-Null
    foreach ($Name in @("bootstrap.kr", "toolchain.kr", "stage0.c", "runtime.c", "runtime.h", "driver.c", "host.c", "host.h", "platform.h")) {
        Copy-Item -LiteralPath (Join-Path $Root "selfhost/$Name") -Destination $Support
    }
    Copy-Item -LiteralPath (Join-Path $Root "native/platform.h") -Destination $Native
    $env:KREMEN_SELFHOST_ROOT = $Support
    Invoke-Driver @("run", $Return, "--cc", $Compiler) 7 | Out-Null
    Pass "Unicode support-source override reaches child builds"
    $env:KREMEN_SELFHOST_ROOT = $null
    $Installed = Join-Path $Prefix "bin/kremen-selfhost.exe"
    Copy-Item -LiteralPath $Toolchain -Destination $Installed
    Invoke-Driver @("check", $Source, "--cc", $Compiler) 0 $Installed | Out-Null
    $Moved = Join-Path $Work "перенесённый пакет"
    Move-Item -LiteralPath $Prefix -Destination $Moved
    Invoke-Driver @("run", $Return, "--cc", $Compiler) 7 (Join-Path $Moved "bin/kremen-selfhost.exe") | Out-Null
    Pass "installed subset resources survive Unicode prefix relocation"
    Write-Output "Native selfhost 0.4.8 checks passed: $Total."
}
finally {
    $env:KREMEN_SELFHOST_ROOT = $OriginalOverride
    Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
}
