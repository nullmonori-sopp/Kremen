#requires -Version 7.3
[CmdletBinding()]
param(
    [string]$Compiler = 'clang',
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build'),
    [switch]$Selfhost
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandArgumentPassing = 'Standard'
if (-not (Get-Command $Compiler -ErrorAction SilentlyContinue)) {
    throw 'A native Clang C compiler is required for a source build. Use a portable Kremen package to run without development tools.'
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
[IO.Directory]::CreateDirectory($BuildDir) | Out-Null
$sources = @('core','diagnostics','doctor','lexer','parser','modules','checker','learn','codegen','format','main') |
    ForEach-Object { Join-Path $PSScriptRoot "native/$_.c" }
$flags = @('-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O3',
    '-D_CRT_SECURE_NO_WARNINGS','-D_CRT_NONSTDC_NO_WARNINGS')
$target = & $Compiler '-dumpmachine'
if ($LASTEXITCODE -ne 0) { throw 'Cannot query the selected compiler target.' }
if ("$target" -match 'msvc') { $flags += '-fms-runtime-lib=static' }
& $Compiler @flags @sources (Join-Path $PSScriptRoot 'native/fast_copy.c') '-o' (Join-Path $BuildDir 'kremen.exe')
if ($LASTEXITCODE -ne 0) { throw "Native compiler build failed ($LASTEXITCODE). Use a Visual Studio developer terminal with the Windows SDK available." }
& $Compiler @flags (Join-Path $PSScriptRoot 'tools/backend_launcher.c') '-o' (Join-Path $BuildDir 'kremen-backend.exe')
if ($LASTEXITCODE -ne 0) { throw "Backend launcher build failed ($LASTEXITCODE)." }
if ($Selfhost) {
    & (Join-Path $PSScriptRoot 'selfhost/bootstrap.ps1') -Compiler $Compiler
    if ($LASTEXITCODE -ne 0) { throw 'Self-hosted subset bootstrap failed.' }
}
Write-Output "Built native Kremen in $BuildDir"
