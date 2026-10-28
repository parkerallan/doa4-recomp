# Shared helpers for the launcher's Pester 3.4 tests. Dot-source from each test file.
# Fakes record every call and THROW on a call no rule expects, so a fake can fail a test.

$script:ModulePath = Join-Path $PSScriptRoot '..\launcher\DOA4Setup.psm1'
Import-Module $script:ModulePath -Force -DisableNameChecking

function New-FakeResult {
    param([object]$ExitCode = 0, [string[]]$Output = @(), [switch]$NotFound)
    [pscustomobject]@{ ExitCode = $ExitCode; Output = [string[]]$Output; NotFound = [bool]$NotFound }
}

# Rules: ordered hashtables @{ Match = <regex on the arguments joined by spaces>;
# Exit = 0; Output = @(); NotFound = $false; Do = { param($Arguments) ... } }.
function New-FakeCommand {
    param([string]$Name, [object[]]$Rules)
    $state = @{ Name = $Name; Calls = (New-Object System.Collections.ArrayList); Rules = $Rules }
    $invoke = {
        param($Arguments, $OnLine)
        $joined = (@($Arguments) -join ' ')
        [void]$state.Calls.Add($joined)
        foreach ($r in $state.Rules) {
            if ($joined -match $r.Match) {
                if ($r.ContainsKey('Do') -and $r.Do) { & $r.Do $Arguments }
                $exit = 0
                if ($r.ContainsKey('Exit')) { $exit = $r.Exit }
                $out = @()
                if ($r.ContainsKey('Output')) { $out = $r.Output }
                if ($r.ContainsKey('NotFound') -and $r.NotFound) {
                    return [pscustomobject]@{ ExitCode = $null; Output = [string[]]@('not found'); NotFound = $true }
                }
                if ($OnLine) { foreach ($l in $out) { & $OnLine $l } }
                return [pscustomobject]@{ ExitCode = $exit; Output = [string[]]$out; NotFound = $false }
            }
        }
        throw "fake $($state.Name): unexpected call: $joined"
    }.GetNewClosure()
    [pscustomobject]@{ Invoke = $invoke; State = $state }
}

# A fake cmd for the build: records the batch file text and environment; Do decides.
function New-FakeCmd {
    param([int]$Exit = 0, [scriptblock]$Do)
    $state = @{ Calls = (New-Object System.Collections.ArrayList); Exit = $Exit; Do = $Do }
    $invoke = {
        param([string]$BatchFile, [hashtable]$Environment, [string]$WorkingDirectory, [scriptblock]$OnLine)
        [void]$state.Calls.Add([pscustomobject]@{
                Batch = [IO.File]::ReadAllText($BatchFile); Env = $Environment.Clone(); Cwd = $WorkingDirectory })
        if ($state.Do) { & $state.Do $Environment }
        if ($OnLine) { & $OnLine 'fake build output' }
        return [pscustomobject]@{ ExitCode = $state.Exit; Output = [string[]]@('fake build output'); NotFound = $false }
    }.GetNewClosure()
    [pscustomobject]@{ Invoke = $invoke; State = $state }
}

function New-TestConfig {
    @{
        GameRepoUrl = 'https://github.com/parkerallan/doa4-recomp'; GameRef = 'main'
        SdkRepoUrl = 'https://github.com/rexglue/rexglue-sdk'; SdkRef = 'e8ce24f'
        WingetGitId = 'Git.Git'; WingetLlvmId = 'LLVM.LLVM'; LlvmVersion = '23.1.4'
        WingetVsId = 'Microsoft.VisualStudio.BuildTools'; VsWorkload = 'Microsoft.VisualStudio.Workload.VCTools'
        VsVersionRange = '[18,19)'; RequiredFreeGB = 6
        GitSize = 'about 70 MB download'; LlvmSize = 'about 400 MB download'; VsSize = 'about 2 GB download'
    }
}

# Fake tool set: everything present unless named in -Missing; -ClangVersion for a wrong
# LLVM; -NoAvx2 for an old CPU. VsDevCmd.bat is created under -VsRoot.
function New-FakeTools {
    param([string]$VsRoot, [string[]]$Missing = @(), [string]$ClangVersion = '23.1.4', [switch]$NoAvx2, [string]$OtherVsVersion = '')
    $vsPath = Join-Path $VsRoot 'BuildTools'
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $vsPath 'Common7\Tools'))
    Set-Content -LiteralPath (Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat') -Value '@echo off' -Encoding ASCII
    $winget = if ($Missing -contains 'winget') {
        New-FakeCommand 'winget' @(@{ Match = '.'; NotFound = $true })
    } else {
        New-FakeCommand 'winget' @(@{ Match = '^--version$'; Output = @('v1.12.0') })
    }
    $clangRules = @()
    if ($Missing -contains 'llvm') { $clangRules += @{ Match = '.'; NotFound = $true } }
    $clangRules += @{ Match = '^--version$'; Output = @("clang version $ClangVersion (https://github.com/llvm/llvm-project abc)", 'Target: x86_64-pc-windows-msvc') }
    $avxOut = @('#define __AVX__ 1', '#define __SSE4_2__ 1')
    if (-not $NoAvx2) { $avxOut += '#define __AVX2__ 1' }
    $clangRules += @{ Match = '-march=native'; Output = $avxOut }
    $clang = New-FakeCommand 'clang' $clangRules
    $vsRules = @()
    if ($Missing -contains 'vs') {
        $vsRules += @{ Match = '-version'; Output = @() }
        $vsRules += @{ Match = '^-all'; Output = @($OtherVsVersion | Where-Object { $_ }) }
    } else {
        $vsRules += @{ Match = '-version \[18,19\) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath'; Output = @($vsPath) }
    }
    $vswhere = New-FakeCommand 'vswhere' $vsRules
    $git = if ($Missing -contains 'git') {
        New-FakeCommand 'git' @(@{ Match = '.'; NotFound = $true })
    } else {
        New-FakeCommand 'git' @(@{ Match = '^--version$'; Output = @('git version 2.51.0.windows.1') })
    }
    [pscustomobject]@{ Winget = $winget; Clang = $clang; Vswhere = $vswhere; Git = $git; VsPath = $vsPath }
}

function New-FakeTable {
    param($Tools, [int64]$Free = 500GB, $Cmd = $null, [switch]$RealGit)
    $t = @{
        winget = $Tools.Winget.Invoke; clang = $Tools.Clang.Invoke; vswhere = $Tools.Vswhere.Invoke
        freeSpace = { param($p) $Free }.GetNewClosure()
    }
    if ($RealGit) { $t.git = (New-DOA4CommandTable).git } else { $t.git = $Tools.Git.Invoke }
    if ($Cmd) { $t.cmd = $Cmd.Invoke } else { $t.cmd = { throw 'fake cmd: unexpected build' } }
    return $t
}

# ---- real git fixtures (repositories in TestDrive) ----

function Invoke-TGit {
    param([string]$Dir, [string[]]$GitArgs)
    $a = @('-C', $Dir, '-c', 'user.name=Launcher Test', '-c', 'user.email=test@example.invalid',
        '-c', 'protocol.file.allow=always', '-c', 'core.autocrlf=true', '-c', 'init.defaultBranch=main') + $GitArgs
    $r = Invoke-DOA4Process -FilePath 'git.exe' -Arguments $a
    if ($r.ExitCode -ne 0) { throw ("test git {0} failed: {1}" -f ($GitArgs -join ' '), ($r.Output -join ' | ')) }
    return $r.Output
}

function Write-TFile {
    param([string]$Path, [string]$Text)
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Force -Path $dir) }
    [IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Add-TSymlinkEntry {
    param([string]$Repo, [string]$LinkPath, [string]$Target)
    $tmp = Join-Path $Repo ('.linktmp' + [Guid]::NewGuid().ToString('N'))
    [IO.File]::WriteAllText($tmp, $Target, (New-Object System.Text.UTF8Encoding($false)))
    $sha = @(Invoke-TGit $Repo @('hash-object', '-w', '--', $tmp))[0].Trim()
    Remove-Item -LiteralPath $tmp -Force
    Invoke-TGit $Repo @('update-index', '--add', '--cacheinfo', "120000,$sha,$LinkPath") | Out-Null
}

$script:TestCMakeText = "cmake_minimum_required(VERSION 3.25)`nproject(dead_or_alive_4)`nadd_executable(dead_or_alive_4 src/main.cpp)`nrexglue_setup_target(dead_or_alive_4)`n`ninstall(TARGETS dead_or_alive_4)`n"

# Origins under $Base: game (CMakeLists like upstream, no include fix), lib (a submodule with
# two file symlinks and one directory symlink), sdk (has lib at thirdparty/lib).
function New-TOrigins {
    param([string]$Base, [switch]$WithSetupDialog)
    $game = Join-Path $Base 'origin-game'
    $lib = Join-Path $Base 'origin-lib'
    $sdk = Join-Path $Base 'origin-sdk'
    foreach ($d in @($game, $lib, $sdk)) { [void](New-Item -ItemType Directory -Force -Path $d); Invoke-TGit $d @('init', '-q') | Out-Null }
    Write-TFile (Join-Path $game 'CMakeLists.txt') $script:TestCMakeText
    Write-TFile (Join-Path $game 'src\main.cpp') "int main() { return 0; }`n"
    if ($WithSetupDialog) { Write-TFile (Join-Path $game 'src\doa4_setup_dialog.cpp') "// setup`n" }
    Invoke-TGit $game @('add', '-A') | Out-Null
    Invoke-TGit $game @('commit', '-q', '-m', 'game') | Out-Null

    Write-TFile (Join-Path $lib 'real\a.c') "int a;`n"
    Write-TFile (Join-Path $lib 'real\b.h') "#define B 1`n"
    Write-TFile (Join-Path $lib 'real\dir\x.h') "#define X 1`n"
    Invoke-TGit $lib @('add', '-A') | Out-Null
    Add-TSymlinkEntry $lib 'link/a.c' '../real/a.c'
    Add-TSymlinkEntry $lib 'link/b.h' '../real/b.h'
    Add-TSymlinkEntry $lib 'linkdir' 'real/dir'
    Invoke-TGit $lib @('commit', '-q', '-m', 'lib') | Out-Null

    Write-TFile (Join-Path $sdk 'README.md') "sdk`n"
    Invoke-TGit $sdk @('add', '-A') | Out-Null
    Invoke-TGit $sdk @('commit', '-q', '-m', 'sdk') | Out-Null
    Invoke-TGit $sdk @('submodule', 'add', '-q', $lib, 'thirdparty/lib') | Out-Null
    Invoke-TGit $sdk @('commit', '-q', '-m', 'add lib') | Out-Null
    [pscustomobject]@{ Game = $game; Lib = $lib; Sdk = $sdk }
}

function Get-TBytesHash {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-TStatus {
    param([string]$Repo)
    return @(Invoke-TGit $Repo @('-c', 'core.quotepath=false', 'status', '--porcelain', '--untracked-files=no') | Where-Object { $_ })
}

# An installed DOA4 folder: real git origins under $Base, faked tools, a faked build (it only
# creates the exe). The root is ASCII with a space: Install refuses other roots (review I1).
function New-TInstalled {
    param([string]$Base, [switch]$WithSetupDialog, [scriptblock]$BeforeInstall, [string]$RootName = 'DOA4 test')
    Enable-TFileProtocol
    $o = New-TOrigins -Base $Base -WithSetupDialog:$WithSetupDialog
    if ($BeforeInstall) { & $BeforeInstall $o }
    $cfg = New-TestConfig
    $cfg.GameRepoUrl = $o.Game; $cfg.GameRef = 'main'
    $sdkHead = @(Invoke-TGit $o.Sdk @('rev-parse', 'HEAD'))[0]
    # The SDK pinned to a commit, as in the default config (e8ce24f).
    $cfg.SdkRepoUrl = $o.Sdk; $cfg.SdkRef = $sdkHead.Substring(0, 7)
    $tools = New-FakeTools -VsRoot (Join-Path $Base 'vs')
    $build = New-FakeCmd -Do {
        param($envVars)
        $exeDir = Join-Path $envVars.DOA4_GAME 'out\build\win-amd64-release'
        [void](New-Item -ItemType Directory -Force -Path $exeDir)
        [IO.File]::WriteAllText((Join-Path $exeDir 'dead_or_alive_4.exe'), 'fake exe')
    }
    $root = Join-Path $Base $RootName
    $shortcuts = Join-Path $Base 'shortcuts'
    [void](New-Item -ItemType Directory -Force -Path $shortcuts)
    $ctx = New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools -Cmd $build -RealGit) -ShortcutFolder $shortcuts
    $r = Invoke-DOA4Install -Ctx $ctx
    [pscustomobject]@{ Origins = $o; Config = $cfg; Tools = $tools; Build = $build; Root = $root; Ctx = $ctx; Install = $r; Shortcuts = $shortcuts }
}

function New-TCtx {
    param($T, [string]$Action = 'run')
    New-DOA4Context -Root $T.Root -Config $T.Config -Cmd (New-FakeTable $T.Tools -Cmd $T.Build -RealGit) -Action $Action
}

function Add-TUpstreamCommit {
    param($T)
    Write-TFile (Join-Path $T.Origins.Game 'src\new.cpp') "// new`n"
    Invoke-TGit $T.Origins.Game @('add', '-A') | Out-Null
    Invoke-TGit $T.Origins.Game @('commit', '-q', '-m', 'new upstream') | Out-Null
}

# Lets the module's own git calls clone file:// submodules in tests (never set outside tests).
function Enable-TFileProtocol {
    $env:GIT_CONFIG_COUNT = '1'
    $env:GIT_CONFIG_KEY_0 = 'protocol.file.allow'
    $env:GIT_CONFIG_VALUE_0 = 'always'
}
function Disable-TFileProtocol {
    Remove-Item Env:GIT_CONFIG_COUNT, Env:GIT_CONFIG_KEY_0, Env:GIT_CONFIG_VALUE_0 -ErrorAction SilentlyContinue
}
