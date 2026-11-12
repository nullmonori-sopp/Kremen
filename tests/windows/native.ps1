#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Kremen = (Join-Path $PSScriptRoot '../../build/kremen.exe'),
    [string]$Compiler = 'clang',
    [string]$PackageRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'This integration target requires native Windows.' }
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Kremen = [IO.Path]::GetFullPath($Kremen)
if ($PackageRoot -and -not $PSBoundParameters.ContainsKey('Compiler')) { $Compiler = Join-Path $PackageRoot 'backend/bin/clang.exe' }
$Compiler = (Get-Command $Compiler -ErrorAction Stop).Source
$Utf8 = [Text.UTF8Encoding]::new($false)
$Work = Join-Path ([IO.Path]::GetTempPath()) ('kremen native кириллица Ω 🌍 ' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($Work) | Out-Null
$Total = 0
$SavedCC = $env:CC
$SavedTemp = $env:TEMP
$SavedTmp = $env:TMP
$SavedPath = $env:PATH
$NativeOptions = @()
if (-not $PackageRoot) { $NativeOptions = @('--cc', $Compiler) }
function Pass([string]$Label) { $script:Total++; Write-Output "ok native048.$script:Total - $Label" }
function Write-Source([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $Utf8) }
function Run([string]$Executable, [string[]]$Arguments = @(), [int]$Expected = 0, [string]$Directory = $Work) {
    $Start = [Diagnostics.ProcessStartInfo]::new()
    $Start.FileName = $Executable
    $Start.WorkingDirectory = $Directory
    $Start.UseShellExecute = $false
    $Start.RedirectStandardOutput = $true
    $Start.RedirectStandardError = $true
    $Start.StandardOutputEncoding = $Utf8
    $Start.StandardErrorEncoding = $Utf8
    foreach ($Argument in $Arguments) { $Start.ArgumentList.Add($Argument) }
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Start
    try {
        if (-not $Process.Start()) { throw "Could not start $Executable" }
        $Output = $Process.StandardOutput.ReadToEndAsync()
        $Errors = $Process.StandardError.ReadToEndAsync()
        if (-not $Process.WaitForExit(120000)) {
            $Process.Kill($true)
            $Process.WaitForExit()
            throw "Command exceeded 120 seconds: $Executable"
        }
        $Result = [pscustomobject]@{ Out = $Output.GetAwaiter().GetResult(); Error = $Errors.GetAwaiter().GetResult(); Status = $Process.ExitCode }
        if ($Expected -ge 0 -and $Result.Status -ne $Expected) { throw "Expected status $Expected, got $($Result.Status): $Executable $($Arguments -join ' ')`n$($Result.Error)" }
        return $Result
    } finally { $Process.Dispose() }
}
function Invoke-Kremen([string[]]$Arguments, [int]$Expected = 0) { return Run $Kremen $Arguments $Expected }
function Expect-Output($Result, [string]$Expected) {
    if ($Result.Out.Replace("`r`n", "`n").TrimEnd("`n") -cne $Expected -or $Result.Error.Length) {
        throw "Unexpected output:`n$($Result.Out)`nstderr:`n$($Result.Error)"
    }
}
function Reject([string[]]$Arguments, [string]$Code) {
    $Result = Invoke-Kremen ($Arguments + '--diagnostic-format=json') 1
    if ($Result.Out.Length) { throw 'Rejected command wrote to stdout.' }
    $Diagnostic = $Result.Error | ConvertFrom-Json
    if ($Diagnostic.code -cne $Code -or $Diagnostic.kind -cne 'error' -or $Diagnostic.explain -cne "kremen explain $Code") {
        throw "Unexpected diagnostic: $($Result.Error)"
    }
    return $Diagnostic
}
try {
    $env:CC = $null
    $Version = [IO.File]::ReadAllText((Join-Path $Root 'VERSION'), $Utf8).Trim()
    $Result = Invoke-Kremen @('--version')
    if (-not $Result.Out.Contains("Kremen $Version")) { throw 'Wrong native compiler version.' }
    Pass 'native compiler version'
    $Result = Invoke-Kremen (@('doctor') + $NativeOptions)
    if (-not $Result.Out.Contains('Worker spawn/join: OK') -or $Result.Error.Length) { throw "Doctor probe failed: $($Result.Error)" }
    Pass 'doctor compiles and executes the native thread runtime'
    $null = Reject @('doctor','--cc',(Join-Path $Work 'missing backend.exe')) 'E016'
    Pass 'missing backend has a structured explained diagnostic'
    $Main = Join-Path $Work 'задача Ω🌍.kr'
    Write-Source $Main "func main() { println(7) }`n"
    foreach ($Mode in @('work','learn')) {
        $Result = Invoke-Kremen @('check',$Main,"--mode=$Mode")
        if ($Result.Error.Length) { throw $Result.Error }
        Pass "$Mode checks a Unicode source path"
        Expect-Output (Invoke-Kremen (@('run',$Main,"--mode=$Mode") + $NativeOptions)) '7'
        Pass "$Mode compiles and runs a Unicode source path"
    }
    $null = Invoke-Kremen (@('build',$Main) + $NativeOptions)
    Expect-Output (Run (Join-Path $Work 'a.exe')) '7'
    Pass 'default build creates a runnable a.exe'
    $Quoted = Join-Path $Work "путь with ' quote & ampersand"
    [IO.Directory]::CreateDirectory($Quoted) | Out-Null
    $QuotedSource = Join-Path $Quoted 'исходник.kr'
    $QuotedBinary = Join-Path $Quoted 'программа.exe'
    Copy-Item -LiteralPath $Main -Destination $QuotedSource
    $null = Invoke-Kremen (@('build',$QuotedSource,'-o',$QuotedBinary) + $NativeOptions)
    Expect-Output (Run $QuotedBinary) '7'
    Pass 'Unicode source and output paths preserve spaces apostrophes and shell characters'
    $Temp = Join-Path $Work 'временный каталог 🌍'
    [IO.Directory]::CreateDirectory($Temp) | Out-Null
    $env:TEMP = $Temp; $env:TMP = $Temp
    Expect-Output (Invoke-Kremen (@('run',$Main) + $NativeOptions)) '7'
    $env:TEMP = $SavedTemp; $env:TMP = $SavedTmp
    if (@(Get-ChildItem -LiteralPath $Temp -Force).Count) { throw 'Run left temporary output behind.' }
    Pass 'run uses and cleans a Unicode temporary directory'
    $Crlf = Join-Path $Work 'строки.kr'
    Write-Source $Crlf "func main() {`r`n    println(7)`r`n}`r`n"
    Expect-Output (Invoke-Kremen (@('run',$Crlf) + $NativeOptions)) '7'
    Pass 'CRLF source input retains normal semantics'
    $null = Invoke-Kremen @('fmt','-w',$Crlf)
    $Formatted = (Get-FileHash -LiteralPath $Crlf).Hash
    $null = Invoke-Kremen @('fmt','--check',$Crlf)
    $null = Invoke-Kremen @('fmt','-w',$Crlf)
    if ((Get-FileHash -LiteralPath $Crlf).Hash -ne $Formatted) { throw 'Formatter is not idempotent.' }
    Pass 'formatter replaces an existing Unicode source idempotently'
    $Readonly = Join-Path $Work 'только чтение.kr'
    Copy-Item -LiteralPath $Main -Destination $Readonly
    $Before = (Get-FileHash -LiteralPath $Readonly).Hash
    [IO.File]::SetAttributes($Readonly, [IO.FileAttributes]::ReadOnly)
    try { $null = Reject @('fmt','-w',$Readonly) 'E015' }
    finally { [IO.File]::SetAttributes($Readonly, [IO.FileAttributes]::Normal) }
    if ((Get-FileHash -LiteralPath $Readonly).Hash -ne $Before -or @(Get-ChildItem -LiteralPath $Work -Filter '*.kremen-*' -Recurse -Force).Count) { throw 'Read-only formatter failure changed files or left staging.' }
    Pass 'read-only formatter failure preserves source and cleans staging'
    $Module = Join-Path $Work 'модули'
    [IO.Directory]::CreateDirectory($Module) | Out-Null
    Write-Source (Join-Path $Module 'вычисление.krh') "func answer() i64`n"
    Write-Source (Join-Path $Module 'вычисление.kr') "func answer() i64 { return 42 }`n"
    $Imported = Join-Path $Work 'импорт.kr'
    Write-Source $Imported "import calc `"модули/вычисление`"`nfunc main() { println(calc.answer()) }`n"
    Expect-Output (Invoke-Kremen (@('run',$Imported,'--mode=learn') + $NativeOptions)) '42'
    Pass 'Unicode local imports load matching declaration headers'
    $null = Invoke-Kremen @('check',$Main,'--root',$Work.ToUpperInvariant())
    Pass 'equivalent path case selects the same project root'
    $InvalidImport = Join-Path $Work 'invalid-import.kr'
    Write-Source $InvalidImport "import outside `"C:/Windows`"`nfunc main() {}`n"
    $null = Reject @('check',$InvalidImport) 'E013'
    Pass 'drive-absolute imports are rejected'
    Write-Source $InvalidImport ('import outside "\\\\server\\share"' + "`nfunc main() {}`n")
    $null = Reject @('check',$InvalidImport) 'E013'
    Pass 'UNC imports are rejected'
    $NestedRoot = Join-Path $Work 'nested-root'
    [IO.Directory]::CreateDirectory($NestedRoot) | Out-Null
    $null = Reject @('check',$Main,'--root',$NestedRoot) 'E013'
    Pass 'an explicit root cannot exclude its entry source'
    $SourceHash = (Get-FileHash -LiteralPath $Main).Hash
    $null = Reject @('emit-c',$Main,'-o',$Main.ToUpperInvariant()) 'E001'
    if ((Get-FileHash -LiteralPath $Main).Hash -ne $SourceHash) { throw 'Path case alias replaced the source.' }
    Pass 'path case aliases cannot overwrite source'
    $Alias = Join-Path $Work 'жёсткая ссылка.kr'
    $null = New-Item -ItemType HardLink -Path $Alias -Target $Main
    $null = Reject @('emit-c',$Main,'-o',$Alias) 'E001'
    if ((Get-FileHash -LiteralPath $Main).Hash -ne $SourceHash) { throw 'Hard link replaced the source.' }
    Pass 'file identity prevents hard-link source overwrite'
    $Invalid = Join-Path $Work 'ошибка 🌍.kr'
    Write-Source $Invalid "func main() { println(@) }`n"
    $Diagnostic = Reject @('check',$Invalid) 'E100'
    if (-not $Diagnostic.file.Contains('ошибка 🌍.kr') -or $Diagnostic.line -ne 1 -or $Diagnostic.column -lt 1) { throw 'Unicode diagnostic path or range lost.' }
    Pass 'JSON diagnostics retain Unicode paths and source ranges'
    $Uninitialized = Join-Path $Work 'инициализация.kr'
    Write-Source $Uninitialized "func main() { var value i64; println(value) }`n"
    $null = Invoke-Kremen @('check',$Uninitialized,'--mode=work')
    $null = Reject @('check',$Uninitialized,'--mode=learn') 'L001'
    Pass 'learn keeps its explicit initialization requirement'
    $Chain = Join-Path $Work 'chain'
    [IO.Directory]::CreateDirectory($Chain) | Out-Null
    for ($Index = 0; $Index -lt 82; $Index++) {
        $Text = if ($Index -eq 81) { "func value() i64 { return 82 }`n" } else { "import next `"m$($Index+1)`"`nfunc value() i64 { return next.value() }`n" }
        Write-Source (Join-Path $Chain "m$Index.kr") $Text
    }
    $ChainMain = Join-Path $Work 'chain.kr'
    Write-Source $ChainMain "import first `"chain/m0`"`nfunc main() { println(first.value()) }`n"
    Expect-Output (Invoke-Kremen (@('run',$ChainMain) + $NativeOptions)) '82'
    Pass '82 loaded modules fit the native Windows process stack'
    $CaseDirectory = Join-Path $Work 'case-sensitive'
    [IO.Directory]::CreateDirectory($CaseDirectory) | Out-Null
    $Fsutil = Join-Path $env:SystemRoot 'System32/fsutil.exe'
    $CaseEnabled = if (Test-Path -LiteralPath $Fsutil) { (Run $Fsutil @('file','setCaseSensitiveInfo',$CaseDirectory,'enable') -1).Status -eq 0 } else { $false }
    if ($CaseEnabled) {
        Write-Source (Join-Path $CaseDirectory 'value.kr') "func answer() i64 { return 1 }`n"
        Write-Source (Join-Path $CaseDirectory 'VALUE.kr') "func answer() i64 { return 2 }`n"
    }
    if ($CaseEnabled -and [IO.Directory]::GetFiles($CaseDirectory).Length -eq 2) {
        $CaseMain = Join-Path $Work 'case-sensitive.kr'
        Write-Source $CaseMain "import lower `"case-sensitive/value.kr`"`nimport upper `"case-sensitive/VALUE.kr`"`nfunc main() { println(lower.answer(), upper.answer()) }`n"
        Expect-Output (Invoke-Kremen (@('run',$CaseMain) + $NativeOptions)) '1 2'
        Pass 'case-sensitive NTFS modules retain distinct file identities'
    } else {
        Write-Output 'SKIP native048 case-sensitive module identity: per-directory case sensitivity is unavailable on this host.'
    }
    foreach ($Mode in @('work','learn')) {
        Expect-Output (Invoke-Kremen (@('run',(Join-Path $Root 'tests/windows/runtime048.kr'),"--mode=$Mode") + $NativeOptions)) 'native runtime 048 ok'
        Pass "$Mode retains Unicode filesystem bytes and native worker behavior"
    }
    $Panicking = Join-Path $Work 'паника.kr'
    Write-Source $Panicking ('func main() { defer println("cleanup"); panic("expected native failure") }' + "`n")
    $Result = Invoke-Kremen (@('run',$Panicking) + $NativeOptions) 1
    if ($Result.Out.Replace("`r`n","`n").TrimEnd("`n") -cne 'cleanup' -or -not $Result.Error.Contains('expected native failure')) { throw 'Native panic lost cleanup or diagnostic.' }
    Pass 'runtime panic preserves deferred cleanup'
    $Runtime = Join-Path $Work 'native-runtime.exe'
    $Flags = @('-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O2','-D_CRT_SECURE_NO_WARNINGS','-D_CRT_NONSTDC_NO_WARNINGS')
    $null = Run $Compiler ($Flags + @((Join-Path $Root 'tests/native/runtime_048.c'),'-o',$Runtime))
    foreach ($Mode in @('unicode-file','unicode-open','unicode-rename','unicode-same-file','unicode-directory','unicode-list','unicode-size','unicode-missing','unicode-cwd','invalid-path','native-workers','nested-workers','nested-cleanup','foreign-owner','repeat-join','invalid-worker','autojoin-exit','nested-failure')) {
        $Case = Join-Path $Work "runtime-$Mode"
        [IO.Directory]::CreateDirectory($Case) | Out-Null
        $Result = Run $Runtime @($Mode) 0 $Case
        if ($Mode -eq 'nested-failure') {
            if (-not $Result.Error.Contains('runtime error: native worker failure')) { throw 'Nested worker panic was hidden.' }
        } elseif ($Result.Error.Length) { throw $Result.Error }
        if ($Result.Error -match 'AddressSanitizer|UndefinedBehaviorSanitizer') { throw 'A runtime test produced a sanitizer failure.' }
        Pass "direct native runtime: $Mode"
    }
    if (Get-Command dumpbin -ErrorAction SilentlyContinue) {
        foreach ($Executable in @($Kremen,$Runtime,(Join-Path $Work 'a.exe'))) {
            $Result = Run (Get-Command dumpbin).Source @('/dependents',$Executable)
            if ($Result.Out -match '(?i)libwinpthread|msys-2\.0|cygwin1') { throw "Compatibility DLL dependency found: $Executable" }
        }
        Pass 'native compiler and generated programs have no MSYS Cygwin or winpthreads imports'
    }
    if ($PackageRoot) {
        $PackageRoot = [IO.Path]::GetFullPath($PackageRoot)
        $Installer = Join-Path $PackageRoot 'install.ps1'
        if (-not (Test-Path -LiteralPath $Installer)) { throw 'Portable package installer missing.' }
        $Prefix = Join-Path $Work 'установка Ω'
        & $Installer -Prefix $Prefix -SourceRoot $PackageRoot
        $Installed = Join-Path $Prefix 'bin/kremen.exe'
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $Result = Run $Installed @('doctor')
        if (-not $Result.Out.Contains('Worker spawn/join: OK')) { throw 'Installed backend discovery failed with clean PATH.' }
        Pass 'portable installation runs without a system compiler or compatibility environment'
        $Moved = Join-Path $Work 'перенесённая установка 🌍'
        Move-Item -LiteralPath $Prefix -Destination $Moved
        Expect-Output (Run (Join-Path $Moved 'bin/kremen.exe') @('run',$Main)) '7'
        Pass 'installed compiler runtime and backend survive Unicode prefix relocation'
        Write-Source (Join-Path $Moved 'unrelated.txt') 'keep'
        $Manifest = Join-Path $Moved 'share/kremen/install.manifest'
        $ManifestBytes = [IO.File]::ReadAllBytes($Manifest)
        [IO.File]::AppendAllText($Manifest,"../outside`n",$Utf8)
        $Refused = $false
        try { & (Join-Path $Moved 'install.ps1') -Prefix $Moved -Uninstall } catch { $Refused = $true }
        if (-not $Refused -or -not (Test-Path -LiteralPath (Join-Path $Moved 'bin/kremen.exe'))) { throw 'Invalid installation manifest was accepted.' }
        [IO.File]::WriteAllBytes($Manifest,$ManifestBytes)
        Pass 'uninstall validates the complete ownership manifest before deleting files'
        & (Join-Path $Moved 'install.ps1') -Prefix $Moved -Uninstall
        if ((Test-Path -LiteralPath (Join-Path $Moved 'bin/kremen.exe')) -or [IO.File]::ReadAllText((Join-Path $Moved 'unrelated.txt'),$Utf8) -ne 'keep') { throw 'Uninstall ownership boundary failed.' }
        Pass 'uninstall removes owned files and preserves unrelated content'
    }
    Write-Output "Native Windows 0.4.8 checks passed: $Total."
} finally {
    $env:CC = $SavedCC; $env:TEMP = $SavedTemp; $env:TMP = $SavedTmp; $env:PATH = $SavedPath
    Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
}
