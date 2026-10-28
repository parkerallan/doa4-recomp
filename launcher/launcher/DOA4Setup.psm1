#Requires -Version 5.1
# DOA4Setup.psm1 - all of the DOA4 launcher's logic, no UI.
# Design: notes/specs/2026-10-28-launcher-setup-design.md (the user's notes folder).
#
# Every external program (git, winget, vswhere, clang, cmd) is reached through ONE command
# table (New-DOA4CommandTable) so the tests can replace it with fakes. A table entry is a
# script block that returns a result object: ExitCode, Output (lines), NotFound.
#
# Keep this file ASCII only: Windows PowerShell 5.1 reads a file without a BOM in the ANSI
# code page.

Set-StrictMode -Version 2.0

$script:SupportedXexSha1 = 'af8011945037b37c98fb8a78d5a4e844dd56c33f'
$script:SupportedDisc = 'Dead or Alive 4 (USA, Europe) (Rev 1)'
$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$script:CMakeAnchor = 'rexglue_setup_target(dead_or_alive_4)'
$script:CMakePresentNeedle = 'rexglue-sdk/include'
$script:CMakeFixLines = @(
    '# DOA4 launcher build fix: upstream lacks these two include directories. The launcher',
    '# removes this block before it updates the repository and adds it again afterwards.',
    'if(TARGET imgui)',
    '    target_include_directories(dead_or_alive_4 PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)',
    'endif()',
    'if(REXSDK_DIR)',
    '    target_include_directories(dead_or_alive_4 PRIVATE ${CMAKE_BINARY_DIR}/rexglue-sdk/include)',
    'endif()'
)
$script:BuildDirRel = 'out\build\win-amd64-release'
$script:ExeName = 'dead_or_alive_4.exe'
$script:TomlName = 'dead_or_alive_4.toml'

# ------------------------------------------------------------------------------------------
# Processes
# ------------------------------------------------------------------------------------------

function New-DOA4Result {
    param([object]$ExitCode = $null, [string[]]$Output = @(), [switch]$NotFound)
    [pscustomobject]@{ ExitCode = $ExitCode; Output = [string[]]$Output; NotFound = [bool]$NotFound }
}

# Quotes arguments the way CommandLineToArgvW / the MSVC runtime read them back.
function ConvertTo-DOA4ArgumentString {
    param([string[]]$Arguments)
    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($a in $Arguments) {
        if ($null -eq $a) { $a = '' }
        if ($a -ne '' -and $a -notmatch '[\s"]') { $parts.Add($a); continue }
        $sb = New-Object System.Text.StringBuilder
        [void]$sb.Append('"')
        $slashes = 0
        foreach ($ch in $a.ToCharArray()) {
            if ($ch -eq [char]'\') { $slashes++; continue }
            if ($ch -eq [char]'"') {
                [void]$sb.Append([char]'\', 2 * $slashes + 1)
                [void]$sb.Append([char]'"')
            } else {
                if ($slashes -gt 0) { [void]$sb.Append([char]'\', $slashes) }
                [void]$sb.Append($ch)
            }
            $slashes = 0
        }
        if ($slashes -gt 0) { [void]$sb.Append([char]'\', 2 * $slashes) }
        [void]$sb.Append('"')
        $parts.Add($sb.ToString())
    }
    return ($parts -join ' ')
}

# Runs a program, streams stdout and stderr lines (in arrival order) to OnLine, and returns
# a result object. A program that cannot be found returns NotFound instead of throwing.
function Invoke-DOA4Process {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$ArgumentString,
        [string]$WorkingDirectory,
        [hashtable]$Environment,
        [scriptblock]$OnLine
    )
    $exe = $FilePath
    if (-not [IO.Path]::IsPathRooted($exe)) {
        $found = Get-Command -Name $exe -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $found) { return New-DOA4Result -NotFound -Output @("$FilePath was not found") }
        $exe = $found.Source
    } elseif (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        return New-DOA4Result -NotFound -Output @("$FilePath was not found")
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    if ($PSBoundParameters.ContainsKey('ArgumentString')) {
        $psi.Arguments = $ArgumentString
    } else {
        $psi.Arguments = ConvertTo-DOA4ArgumentString $Arguments
    }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true
    $psi.StandardOutputEncoding = $script:Utf8NoBom
    $psi.StandardErrorEncoding = $script:Utf8NoBom
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    if ($Environment) {
        foreach ($k in $Environment.Keys) { $psi.EnvironmentVariables[[string]$k] = [string]$Environment[$k] }
    }
    $p = [System.Diagnostics.Process]::Start($psi)
    # Into the launcher's job: when the launcher dies (window closed, process killed), git,
    # cmake, ninja and winget die with it instead of running on unseen. A grandchild the
    # program starts in the few instructions before this call would escape; cmd, git and
    # winget start theirs later than that.
    [void](Add-DOA4ChildToJob $p)
    try {
        $p.StandardInput.Close()
        $lines = New-Object System.Collections.Generic.List[string]
        $readers = @($p.StandardOutput, $p.StandardError)
        $tasks = @($readers[0].ReadLineAsync(), $readers[1].ReadLineAsync())
        $open = 2
        while ($open -gt 0) {
            $pending = @($tasks | Where-Object { $null -ne $_ })
            $i = [System.Threading.Tasks.Task]::WaitAny([System.Threading.Tasks.Task[]]$pending, 250)
            if ($i -lt 0) { continue }
            $idx = [array]::IndexOf($tasks, $pending[$i])
            $line = $tasks[$idx].Result
            if ($null -eq $line) {
                $tasks[$idx] = $null
                $open--
                continue
            }
            $lines.Add($line)
            if ($OnLine) { & $OnLine $line }
            $tasks[$idx] = $readers[$idx].ReadLineAsync()
        }
        $p.WaitForExit()
        return New-DOA4Result -ExitCode $p.ExitCode -Output $lines.ToArray()
    } finally {
        $p.Dispose()
    }
}

# One job object per launcher process, with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: the job's
# handle closes when the launcher process ends, however it ends, and Windows then ends every
# process in the job. Only the launcher's tools are put in it; the game is not (Play starts
# it through the shell, and the launcher process itself is not in the job).
function Add-DOA4ChildToJob {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    if (-not ('DOA4Launcher.ChildJob' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace DOA4Launcher {
    public static class ChildJob {
        [StructLayout(LayoutKind.Sequential)]
        struct BasicLimits {
            public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
            public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct IoCounters { public ulong A, B, C, D, E, F; }
        [StructLayout(LayoutKind.Sequential)]
        struct ExtendedLimits {
            public BasicLimits Basic; public IoCounters Io; public UIntPtr ProcessMemoryLimit;
            public UIntPtr JobMemoryLimit; public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateJobObject(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        const int ExtendedLimitInformation = 9;
        const uint KillOnJobClose = 0x2000;
        static IntPtr job = IntPtr.Zero;   // never closed: it closes when this process ends
        static readonly object gate = new object();
        public static bool Assign(IntPtr process) {
            lock (gate) {
                if (job == IntPtr.Zero) {
                    IntPtr j = CreateJobObject(IntPtr.Zero, null);
                    if (j == IntPtr.Zero) return false;
                    ExtendedLimits info = new ExtendedLimits();
                    info.Basic.LimitFlags = KillOnJobClose;
                    int length = Marshal.SizeOf(typeof(ExtendedLimits));
                    IntPtr buffer = Marshal.AllocHGlobal(length);
                    try {
                        Marshal.StructureToPtr(info, buffer, false);
                        if (!SetInformationJobObject(j, ExtendedLimitInformation, buffer, (uint)length)) return false;
                    } finally { Marshal.FreeHGlobal(buffer); }
                    job = j;
                }
                return AssignProcessToJobObject(job, process);
            }
        }
    }
}
'@
    }
    try { return [DOA4Launcher.ChildJob]::Assign($Process.Handle) } catch { return $false }
}

# One launcher per DOA4 folder: a named mutex per root (full path, case-insensitive). Returns
# the held mutex, or $null when another launcher holds it. The caller keeps the object for
# as long as it runs; Windows releases it when the process ends.
function Enter-DOA4RootLock {
    param([Parameter(Mandatory = $true)][string]$Root)
    $sha = [Security.Cryptography.SHA1]::Create()
    try {
        $hex = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes((ConvertTo-DOA4PathKey $Root))) | ForEach-Object { $_.ToString('x2') })
    } finally { $sha.Dispose() }
    $m = New-Object System.Threading.Mutex($false, "Local\DOA4Launcher-$hex")
    $got = $false
    try { $got = $m.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $got = $true }
    if ($got) { return $m }
    $m.Dispose()
    return $null
}

function Get-DOA4GitPath {
    $onPath = Get-Command -Name 'git.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($onPath) { return $onPath.Source }
    return (Join-Path $env:ProgramFiles 'Git\cmd\git.exe')
}

function Get-DOA4LlvmBin { return (Join-Path $env:ProgramFiles 'LLVM\bin') }

# The real command table. Tests pass their own table with the same keys.
function New-DOA4CommandTable {
    @{
        git = {
            param([string[]]$Arguments, [scriptblock]$OnLine)
            Invoke-DOA4Process -FilePath (Get-DOA4GitPath) -Arguments $Arguments -OnLine $OnLine
        }
        winget = {
            param([string[]]$Arguments, [scriptblock]$OnLine)
            Invoke-DOA4Process -FilePath 'winget.exe' -Arguments $Arguments -OnLine $OnLine
        }
        vswhere = {
            param([string[]]$Arguments)
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            Invoke-DOA4Process -FilePath $vswhere -Arguments $Arguments
        }
        clang = {
            param([string[]]$Arguments)
            Invoke-DOA4Process -FilePath (Join-Path (Get-DOA4LlvmBin) 'clang.exe') -Arguments $Arguments
        }
        cmd = {
            param([string]$BatchFile, [hashtable]$Environment, [string]$WorkingDirectory, [scriptblock]$OnLine)
            $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
            Invoke-DOA4Process -FilePath $cmdExe -ArgumentString ('/d /s /c ""{0}""' -f $BatchFile) `
                -Environment $Environment -WorkingDirectory $WorkingDirectory -OnLine $OnLine
        }
        freeSpace = {
            param([string]$Path)
            $root = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path))
            try { return [int64](New-Object System.IO.DriveInfo($root)).AvailableFreeSpace } catch { return $null }
        }
    }
}

function Invoke-DOA4Git {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [string[]]$Arguments, [scriptblock]$OnLine, [switch]$Check)
    $r = & $Cmd.git $Arguments $OnLine
    if ($r.NotFound) { throw 'Git was not found.' }
    if ($Check -and $r.ExitCode -ne 0) {
        throw ("git {0} failed (exit {1}): {2}" -f ($Arguments -join ' '), $r.ExitCode, (($r.Output | Select-Object -Last 5) -join ' | '))
    }
    return $r
}

# ------------------------------------------------------------------------------------------
# Files
# ------------------------------------------------------------------------------------------

function Read-DOA4TextFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $start = 0
    if ($bom) { $start = 3 }
    $text = $script:Utf8NoBom.GetString($bytes, $start, $bytes.Length - $start)
    [pscustomobject]@{ Text = $text; Bom = $bom; Bytes = $bytes }
}

function ConvertTo-DOA4FileBytes {
    param([string]$Text, [bool]$Bom)
    $body = $script:Utf8NoBom.GetBytes($Text)
    if (-not $Bom) { return , $body }
    $all = New-Object byte[] ($body.Length + 3)
    $all[0] = 0xEF; $all[1] = 0xBB; $all[2] = 0xBF
    [Array]::Copy($body, 0, $all, 3, $body.Length)
    return , $all
}

# Writes beside the target, then swaps it in, so an interrupted write never leaves half a file.
function Write-DOA4FileAtomic {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][byte[]]$Bytes)
    $tmp = $Path + '.launcher-tmp'
    [IO.File]::WriteAllBytes($tmp, $Bytes)
    if (Test-Path -LiteralPath $Path) {
        [IO.File]::Replace($tmp, $Path, [NullString]::Value)
    } else {
        [IO.File]::Move($tmp, $Path)
    }
}

function Format-DOA4Bytes {
    param([double]$Bytes)
    return ('{0:N1} GB' -f ($Bytes / 1GB))
}

# ------------------------------------------------------------------------------------------
# Tools
# ------------------------------------------------------------------------------------------

function Get-DOA4ToolStatus {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)][hashtable]$Config)
    $tools = New-Object System.Collections.Generic.List[object]

    $w = & $Cmd.winget @('--version')
    $wOk = (-not $w.NotFound) -and $w.ExitCode -eq 0
    $wVer = ''
    if ($wOk) { $wVer = (@($w.Output) -join ' ').Trim() }
    $wDetail = 'missing'
    if ($wOk) { $wDetail = "winget $wVer" }
    $tools.Add([pscustomobject]@{ Key = 'winget'; Name = 'winget (App Installer)'; Present = $wOk; Version = $wVer; Detail = $wDetail })

    $g = & $Cmd.git @('--version') $null
    $gVer = ''
    $gOk = $false
    if ((-not $g.NotFound) -and $g.ExitCode -eq 0) {
        $m = [regex]::Match((@($g.Output) -join ' '), 'git version (\S+)')
        if ($m.Success) { $gOk = $true; $gVer = $m.Groups[1].Value }
    }
    $gDetail = 'missing'
    if ($gOk) { $gDetail = "git $gVer" }
    $tools.Add([pscustomobject]@{ Key = 'git'; Name = 'Git'; Present = $gOk; Version = $gVer; Detail = $gDetail })

    $c = & $Cmd.clang @('--version')
    $cVer = ''
    $cOk = $false
    $cDetail = 'missing'
    if ((-not $c.NotFound) -and $c.ExitCode -eq 0) {
        $m = [regex]::Match((@($c.Output) -join ' '), 'clang version (\d+)\.(\d+)\.(\d+)')
        if ($m.Success) {
            $cVer = '{0}.{1}.{2}' -f $m.Groups[1].Value, $m.Groups[2].Value, $m.Groups[3].Value
            $wantMajor = ([string]$Config.LlvmVersion).Split('.')[0]
            if ($m.Groups[1].Value -eq $wantMajor) {
                $cOk = $true
                $cDetail = "clang $cVer"
            } else {
                $cDetail = "found clang $cVer; the build needs LLVM $wantMajor"
            }
        } else {
            $cDetail = 'clang.exe did not report a version'
        }
    }
    $tools.Add([pscustomobject]@{ Key = 'llvm'; Name = 'LLVM / Clang'; Present = $cOk; Version = $cVer; Detail = $cDetail; Bin = (Get-DOA4LlvmBin) })

    $vsArgs = @('-products', '*', '-version', [string]$Config.VsVersionRange, '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath')
    $v = & $Cmd.vswhere $vsArgs
    $vOk = $false
    $vPath = ''
    $vsDevCmd = ''
    $vDetail = 'missing'
    if ($v.NotFound) {
        $vDetail = 'missing (no Visual Studio Installer)'
    } elseif ($v.ExitCode -eq 0) {
        $vPath = @(@($v.Output) | Where-Object { $_ -and $_.Trim() } | Select-Object -First 1)
        if ($vPath.Count -gt 0) {
            $vPath = $vPath[0].Trim()
            $vsDevCmd = Join-Path $vPath 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $vsDevCmd -PathType Leaf) {
                $vOk = $true
                $vDetail = "Visual Studio at $vPath"
            } else {
                $vDetail = "found $vPath but not Common7\Tools\VsDevCmd.bat"
            }
        } else {
            $vPath = ''
            $other = & $Cmd.vswhere @('-all', '-products', '*', '-property', 'installationVersion')
            $versions = @(@($other.Output) | Where-Object { $_ -and $_.Trim() })
            if ($versions.Count -gt 0) {
                $vDetail = 'found Visual Studio {0}; the build needs version 18 (2026) with the C++ x64 tools' -f ($versions -join ', ')
            }
        }
    }
    $tools.Add([pscustomobject]@{ Key = 'vs'; Name = 'Visual Studio 2026 Build Tools (C++)'; Present = $vOk; Version = ''; Detail = $vDetail; InstallPath = $vPath; VsDevCmd = $vsDevCmd })

    return , $tools.ToArray()
}

function Get-DOA4Tool {
    param([object[]]$Status, [string]$Key)
    return ($Status | Where-Object { $_.Key -eq $Key } | Select-Object -First 1)
}

# The winget commands for every missing tool. Blocked is set when winget itself is missing.
function Get-DOA4MissingToolPlan {
    param([Parameter(Mandatory = $true)][object[]]$Status, [Parameter(Mandatory = $true)][hashtable]$Config)
    $items = New-Object System.Collections.Generic.List[object]
    $winget = Get-DOA4Tool $Status 'winget'
    $blocked = $null
    $common = @('-e', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements')
    foreach ($t in $Status) {
        if ($t.Present -or $t.Key -eq 'winget') { continue }
        switch ($t.Key) {
            'git' { $a = @('install', '--id', [string]$Config.WingetGitId) + $common; $size = $Config.GitSize }
            'llvm' { $a = @('install', '--id', [string]$Config.WingetLlvmId) + $common + @('--version', [string]$Config.LlvmVersion); $size = $Config.LlvmSize }
            'vs' {
                $a = @('install', '--id', [string]$Config.WingetVsId) + $common +
                    @('--override', ('--add {0} --includeRecommended --passive --wait' -f $Config.VsWorkload))
                $size = $Config.VsSize
            }
        }
        $items.Add([pscustomobject]@{
                Key = $t.Key; Name = $t.Name; Size = [string]$size; Reason = $t.Detail
                Arguments = [string[]]$a
                CommandLine = 'winget ' + (ConvertTo-DOA4ArgumentString $a)
            })
    }
    # winget is only needed to install something: with every tool present it is not asked for.
    if (-not $winget.Present -and $items.Count -gt 0) {
        $blocked = 'winget (App Installer) is missing, so the tools cannot be installed for you. ' +
            'Install App Installer from the Microsoft Store (https://apps.microsoft.com/detail/9NBLGGH4NNS1), then run setup again.'
    }
    [pscustomobject]@{ Items = $items.ToArray(); Blocked = $blocked }
}

# Runs (or with DryRun only prints) the plan's winget commands. Never called without the
# user's one confirmation (the window asks; the non-interactive mode never installs).
function Install-DOA4Tools {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)]$Plan, [switch]$DryRun, [scriptblock]$OnLine)
    $codes = @{}
    foreach ($item in $Plan.Items) {
        if ($DryRun) {
            if ($OnLine) { & $OnLine $item.CommandLine }
            continue
        }
        if ($OnLine) { & $OnLine ('> ' + $item.CommandLine) }
        $r = & $Cmd.winget $item.Arguments $OnLine
        $codes[$item.Key] = $r.ExitCode
    }
    return $codes
}

# AVX2 through clang's own CPU detection (-march=native), which works on every Windows
# version, unlike IsProcessorFeaturePresent(PF_AVX2...). Supported is $null when unknown.
function Test-DOA4Avx2 {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd)
    $r = & $Cmd.clang @('-dM', '-E', '-march=native', '-x', 'c', '-')
    if ($r.NotFound -or $r.ExitCode -ne 0) {
        return [pscustomobject]@{ Supported = $null; Detail = 'could not ask clang about this CPU' }
    }
    $has = @($r.Output | Where-Object { $_ -match '^#define __AVX2__ 1\s*$' }).Count -gt 0
    if ($has) { return [pscustomobject]@{ Supported = $true; Detail = 'CPU has AVX2' } }
    return [pscustomobject]@{
        Supported = $false
        Detail = 'This CPU does not support AVX2. The game is built for x86-64-v3 (Intel Haswell / AMD Zen or newer) and cannot run on this PC.'
    }
}

function Test-DOA4DiskSpace {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][int64]$RequiredBytes)
    $free = & $Cmd.freeSpace $Path
    if ($null -eq $free) {
        return [pscustomobject]@{ Ok = $true; Free = $null; Required = $RequiredBytes; Message = 'free space could not be read; continuing' }
    }
    $free = [int64]$free
    if ($free -lt $RequiredBytes) {
        $msg = 'Not enough free space on {0}: {1} free, about {2} needed.' -f ([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path))), (Format-DOA4Bytes $free), (Format-DOA4Bytes $RequiredBytes)
        return [pscustomobject]@{ Ok = $false; Free = $free; Required = $RequiredBytes; Message = $msg }
    }
    return [pscustomobject]@{ Ok = $true; Free = $free; Required = $RequiredBytes; Message = ('{0} free, about {1} needed' -f (Format-DOA4Bytes $free), (Format-DOA4Bytes $RequiredBytes)) }
}

# ------------------------------------------------------------------------------------------
# Repositories
# ------------------------------------------------------------------------------------------

function Test-DOA4CommitRef { param([string]$Ref) return ($Ref -match '^[0-9a-fA-F]{7,40}$') }

function Test-DOA4LocalUrl {
    param([string]$Url)
    if ($Url -match '^[A-Za-z][A-Za-z0-9+.-]*://') { return $false }
    if ($Url -match '^[^/\\]+@[^:]+:') { return $false }
    return [IO.Path]::IsPathRooted($Url)
}

function ConvertTo-DOA4UrlKey {
    param([string]$Url)
    $u = $Url.Trim().Replace('\', '/').TrimEnd('/')
    if ($u.EndsWith('.git')) { $u = $u.Substring(0, $u.Length - 4) }
    return $u.ToLowerInvariant()
}

function ConvertTo-DOA4PathKey {
    param([string]$Path)
    return ([IO.Path]::GetFullPath($Path.Replace('/', '\'))).TrimEnd('\').ToLowerInvariant()
}

# Install's Code step: clone when the folder is absent or empty, reuse it when it already
# holds a clone of the configured repository. Never deletes or re-clones.
function Sync-DOA4Repo {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Cmd,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Ref,
        [scriptblock]$OnLine,
        # Which of the launcher's own fixes a reused clone may carry: the CMake block
        # ('game'), the symlink copies ('sdk'), or none ('plain').
        [ValidateSet('game', 'sdk', 'plain')][string]$Kind = 'plain'
    )
    $isCommit = Test-DOA4CommitRef $Ref
    $empty = -not (Test-Path -LiteralPath $Path)
    if (-not $empty) {
        $empty = @(Get-ChildItem -LiteralPath $Path -Force -ErrorAction Stop).Count -eq 0
    }
    if ($empty) {
        $cloneArgs = @('clone')
        if (Test-DOA4LocalUrl $Url) { $cloneArgs += '--no-hardlinks' }
        if (-not $isCommit) { $cloneArgs += @('--branch', $Ref) }
        $cloneArgs += @('--', $Url, $Path)
        Invoke-DOA4Git -Cmd $Cmd -Arguments $cloneArgs -OnLine $OnLine -Check | Out-Null
        if ($isCommit) {
            Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'checkout', '--detach', $Ref) -OnLine $OnLine -Check | Out-Null
        }
        return 'cloned'
    }
    $top = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'rev-parse', '--show-toplevel')
    $topPath = (@($top.Output) | Select-Object -First 1)
    if ($top.ExitCode -ne 0 -or -not $topPath -or (ConvertTo-DOA4PathKey $topPath) -ne (ConvertTo-DOA4PathKey $Path)) {
        throw "The folder $Path already exists and is not a git clone. Move it away (or delete it) and run Install again."
    }
    $head = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'rev-parse', '--verify', 'HEAD')
    if ($head.ExitCode -ne 0) {
        throw "The folder $Path holds an unfinished clone. Delete it and run Install again."
    }
    $origin = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'remote', 'get-url', 'origin')
    $originUrl = (@($origin.Output) | Select-Object -First 1)
    if ($origin.ExitCode -ne 0 -or -not $originUrl -or (ConvertTo-DOA4UrlKey $originUrl) -ne (ConvertTo-DOA4UrlKey $Url)) {
        throw "The folder $Path is a clone of $originUrl, not of $Url (config.ps1). Move it away, or change config.ps1."
    }
    # A clone killed during checkout has a HEAD but missing files (or no index); git also
    # keeps the folder after "Clone succeeded, but checkout failed". Reused only when its
    # tracked files match HEAD apart from the launcher's own fixes.
    $changes = Get-DOA4ForeignChanges -Cmd $Cmd -Path $Path -Kind $Kind
    if ($changes.Foreign.Count -gt 0) {
        $list = @($changes.Foreign | Select-Object -First 5) -join ', '
        if ($changes.Foreign.Count -gt 5) { $list += (', and {0} more' -f ($changes.Foreign.Count - 5)) }
        throw ("The folder $Path does not match its last commit (an unfinished clone, or local changes): $list. " +
            'If it is an unfinished clone, delete the folder and run Install again; if the changes are yours, move them out first.')
    }
    if ($isCommit) {
        $want = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'rev-parse', '--verify', "$Ref^{commit}")
        $headSha = (@($head.Output) | Select-Object -First 1)
        $wantSha = (@($want.Output) | Select-Object -First 1)
        if ($want.ExitCode -eq 0 -and $wantSha -ne $headSha) {
            $dirty = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'status', '--porcelain', '--untracked-files=no') -Check
            if (@($dirty.Output | Where-Object { $_ }).Count -eq 0) {
                Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'checkout', '--detach', $Ref) -OnLine $OnLine -Check | Out-Null
                return 'reused (moved to the configured commit)'
            }
        }
    }
    return 'reused'
}

# git status --porcelain=v2 (tracked files only) as objects.
function Get-DOA4StatusEntries {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)][string]$RepoPath)
    $r = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $RepoPath, '-c', 'core.quotepath=false', 'status',
        '--porcelain=v2', '--untracked-files=no') -Check
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($line in $r.Output) {
        if (-not $line) { continue }
        $type = $line.Substring(0, 1)
        if ($type -eq '1') {
            $f = $line.Split(' ', 9)
            $entries.Add([pscustomobject]@{ Type = '1'; XY = $f[1]; Sub = $f[2]; ModeHead = $f[3]; Path = $f[8] })
        } elseif ($type -eq '2') {
            $f = $line.Split(' ', 10)
            $entries.Add([pscustomobject]@{ Type = '2'; XY = $f[1]; Sub = $f[2]; ModeHead = $f[3]; Path = $f[9].Split("`t")[0] })
        } elseif ($type -eq 'u') {
            $f = $line.Split(' ', 11)
            $entries.Add([pscustomobject]@{ Type = 'u'; XY = $f[1]; Sub = $f[2]; ModeHead = ''; Path = $f[10] })
        }
    }
    return , $entries.ToArray()
}

# Walks a repository and its changed submodules. A change is allowed only when it is a
# symlink stub (mode 120000 in HEAD) changed in the working tree, i.e. the launcher's own
# stub-to-copy fix; everything else is foreign. Submodules checked out at another commit
# are allowed (git submodule update puts them back) and listed in Moved.
function Get-DOA4StubChanges {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)][string]$RepoPath, [string]$Prefix = '')
    $stubs = New-Object System.Collections.Generic.List[object]
    $foreign = New-Object System.Collections.Generic.List[string]
    $moved = New-Object System.Collections.Generic.List[string]
    foreach ($e in (Get-DOA4StatusEntries -Cmd $Cmd -RepoPath $RepoPath)) {
        $shown = $Prefix + $e.Path
        if ($e.Type -eq '1' -and $e.Sub.StartsWith('S')) {
            if ($e.XY.Substring(0, 1) -ne '.') { $foreign.Add("$shown (staged)"); continue }
            if ($e.Sub.Substring(1, 1) -eq 'C') { $moved.Add($shown) }
            if ($e.Sub.Substring(2, 1) -eq 'M') {
                $inner = Get-DOA4StubChanges -Cmd $Cmd -RepoPath (Join-Path $RepoPath $e.Path) -Prefix ($shown + '/')
                foreach ($s in $inner.Stubs) { $stubs.Add($s) }
                foreach ($x in $inner.Foreign) { $foreign.Add($x) }
                foreach ($x in $inner.Moved) { $moved.Add($x) }
            }
            continue
        }
        # A file link replaced by a copy is the launcher's own only while the copy's bytes
        # equal the link target's; a hand edit to it is a foreign change.
        if ($e.Type -eq '1' -and ($e.XY -eq '.M' -or $e.XY -eq '.T') -and $e.ModeHead -eq '120000') {
            $copy = Join-Path $RepoPath ($e.Path.Replace('/', '\'))
            $target = Get-DOA4LinkTarget -Cmd $Cmd -RepoPath $RepoPath -RelPath $e.Path
            if ($target -and (Test-Path -LiteralPath $copy -PathType Leaf) -and (Test-Path -LiteralPath $target -PathType Leaf) -and
                (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
                $stubs.Add([pscustomobject]@{ Repo = $RepoPath; Path = $e.Path; Shown = $shown; Folder = $false })
                continue
            }
            $foreign.Add("$shown (copied file differs from its link target)")
            continue
        }
        # A directory link replaced by a copied folder shows as deleted. It is the launcher's
        # own copy only while the folder still equals the link's target, file for file.
        if ($e.Type -eq '1' -and $e.XY -eq '.D' -and $e.ModeHead -eq '120000') {
            $copy = Join-Path $RepoPath ($e.Path.Replace('/', '\'))
            $target = Get-DOA4LinkTarget -Cmd $Cmd -RepoPath $RepoPath -RelPath $e.Path
            if ($target -and (Test-Path -LiteralPath $copy -PathType Container)) {
                if (Test-DOA4SameFolder $copy $target) {
                    $stubs.Add([pscustomobject]@{ Repo = $RepoPath; Path = $e.Path; Shown = $shown; Folder = $true; FullPath = $copy })
                    continue
                }
            }
            $foreign.Add("$shown (copied folder differs from its link target)")
            continue
        }
        $foreign.Add($shown)
    }
    [pscustomobject]@{ Stubs = $stubs.ToArray(); Foreign = $foreign.ToArray(); Moved = $moved.ToArray() }
}

# The full path a git symlink (mode 120000 in HEAD) points at, from the link text in HEAD;
# $null when it cannot be read.
function Get-DOA4LinkTarget {
    param([hashtable]$Cmd, [string]$RepoPath, [string]$RelPath)
    $link = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $RepoPath, 'cat-file', '-p', "HEAD:$RelPath")
    $linkText = (@($link.Output) -join "`n").Trim()
    if ($link.ExitCode -ne 0 -or -not $linkText) { return $null }
    $at = Join-Path $RepoPath ($RelPath.Replace('/', '\'))
    try {
        return [IO.Path]::GetFullPath([IO.Path]::Combine([IO.Path]::GetDirectoryName($at), $linkText.Replace('/', '\')))
    } catch {
        return $null
    }
}

function Test-DOA4SameFolder {
    param([string]$A, [string]$B)
    if (-not (Test-Path -LiteralPath $B -PathType Container)) { return $false }
    $list = {
        param($root)
        $full = [IO.Path]::GetFullPath($root).TrimEnd('\')
        @(Get-ChildItem -LiteralPath $root -Recurse -Force -File | ForEach-Object {
                $_.FullName.Substring($full.Length + 1) + '|' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            } | Sort-Object)
    }
    $x = & $list $A
    $y = & $list $B
    return (($x -join "`n") -ceq ($y -join "`n"))
}

function Get-DOA4StubTarget {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $info = Get-Item -LiteralPath $Path -Force
    if ($info.Length -gt 4096) { return $null }
    $text = $script:Utf8NoBom.GetString([IO.File]::ReadAllBytes($Path)).Trim()
    if (-not $text -or $text.Contains("`n")) { return $null }
    try {
        $target = [IO.Path]::GetFullPath([IO.Path]::Combine([IO.Path]::GetDirectoryName($Path), $text.Replace('/', '\')))
    } catch {
        return $null
    }
    if (Test-Path -LiteralPath $target) { return $target }
    return $null
}

# The logic of tools/fix_git_symlinks.py: every git symlink (mode 120000) in the repository
# and its submodules that is checked out as a stub (a small file holding a path) is replaced
# by a copy of its target. Safe to run again.
function Repair-DOA4SymlinkStubs {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)][string]$RepoPath)
    $root = [IO.Path]::GetFullPath($RepoPath)
    $repos = New-Object System.Collections.Generic.List[string]
    $repos.Add($root)
    $subs = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $root, '-c', 'core.quotepath=false', 'submodule', 'status', '--recursive') -Check
    foreach ($line in $subs.Output) {
        if (-not $line -or -not $line.Trim()) { continue }
        $f = $line.Trim() -split '\s+'
        if ($f.Count -ge 2) { $repos.Add((Join-Path $root ($f[1].Replace('/', '\')))) }
    }
    $fixed = 0
    $skipped = 0
    foreach ($repo in $repos) {
        $files = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $repo, '-c', 'core.quotepath=false', 'ls-files', '-s') -Check
        foreach ($line in $files.Output) {
            if (-not $line -or -not $line.StartsWith('120000 ')) { continue }
            $rel = $line.Split("`t", 2)[1]
            $path = Join-Path $repo ($rel.Replace('/', '\'))
            $target = Get-DOA4StubTarget $path
            if ($null -eq $target) { $skipped++; continue }
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $path -Force
                Copy-Item -LiteralPath $target -Destination $path -Recurse
            } else {
                Copy-Item -LiteralPath $target -Destination $path -Force
            }
            $fixed++
        }
    }
    [pscustomobject]@{ Fixed = $fixed; Skipped = $skipped }
}

# ------------------------------------------------------------------------------------------
# The CMake include fix
# ------------------------------------------------------------------------------------------

function Get-DOA4NewLine { param([string]$Text) if ($Text.Contains("`r`n")) { return "`r`n" } return "`n" }

function Get-DOA4CMakeFixText {
    param([string]$NewLine)
    return $NewLine + ($script:CMakeFixLines -join $NewLine) + $NewLine
}

# Returns the index just after the anchor line's line ending; throws when the anchor is not
# found exactly once or is the file's last line without a line ending.
function Get-DOA4CMakeAnchorEnd {
    param([string]$Text)
    $ms = [regex]::Matches($Text, '(?m)^[ \t]*' + [regex]::Escape($script:CMakeAnchor) + '[ \t]*(\r?\n)')
    if ($ms.Count -ne 1) {
        throw "CMakeLists.txt: expected one line '$($script:CMakeAnchor)', found $($ms.Count). The build fix cannot be placed."
    }
    return ($ms[0].Index + $ms[0].Length)
}

# Adds the two include-directory blocks after rexglue_setup_target(dead_or_alive_4).
# Returns 'present' (the rexglue-sdk/include line is already there) or 'added'.
function Add-DOA4CMakeIncludeFix {
    param([Parameter(Mandatory = $true)][string]$Path)
    $f = Read-DOA4TextFile $Path
    if ($f.Text.Contains($script:CMakePresentNeedle)) { return 'present' }
    $at = Get-DOA4CMakeAnchorEnd $f.Text
    $new = $f.Text.Insert($at, (Get-DOA4CMakeFixText (Get-DOA4NewLine $f.Text)))
    # Swapped in whole: a killed launcher leaves the old file or the new one, never half.
    Write-DOA4FileAtomic -Path $Path -Bytes (ConvertTo-DOA4FileBytes $new $f.Bom)
    return 'added'
}

# The file's text with the launcher's block removed, or why it cannot be:
# State 'absent' (no block), 'removable' (Text holds the result) or 'modified' (the block
# is not exactly the launcher's, or not where the launcher puts it).
function Get-DOA4CMakeFixRemoval {
    param([Parameter(Mandatory = $true)][string]$Path)
    $f = Read-DOA4TextFile $Path
    if (-not $f.Text.Contains($script:CMakeFixLines[0])) {
        return [pscustomobject]@{ State = 'absent'; Text = $f.Text; Bom = $f.Bom; Original = $f.Bytes }
    }
    $fix = Get-DOA4CMakeFixText (Get-DOA4NewLine $f.Text)
    try { $at = Get-DOA4CMakeAnchorEnd $f.Text } catch { return [pscustomobject]@{ State = 'modified'; Text = $f.Text; Bom = $f.Bom; Original = $f.Bytes } }
    if ($at + $fix.Length -gt $f.Text.Length -or $f.Text.Substring($at, $fix.Length) -cne $fix) {
        return [pscustomobject]@{ State = 'modified'; Text = $f.Text; Bom = $f.Bom; Original = $f.Bytes }
    }
    $rest = $f.Text.Remove($at, $fix.Length)
    if ($rest.Contains($script:CMakeFixLines[0])) {
        return [pscustomobject]@{ State = 'modified'; Text = $f.Text; Bom = $f.Bom; Original = $f.Bytes }
    }
    return [pscustomobject]@{ State = 'removable'; Text = $rest; Bom = $f.Bom; Original = $f.Bytes }
}

# Before an update: removes the launcher's CMake block when the file's ONLY change against
# HEAD is that block (checked with git's own filters: hash-object of the result must equal
# HEAD's blob). Returns 'absent', 'removed' or 'refused'; on 'refused' the file is untouched.
function Remove-DOA4CMakeIncludeFix {
    param([Parameter(Mandatory = $true)][hashtable]$Cmd, [Parameter(Mandatory = $true)][string]$RepoPath, [switch]$CheckOnly)
    $path = Join-Path $RepoPath 'CMakeLists.txt'
    $r = Get-DOA4CMakeFixRemoval $path
    if ($r.State -eq 'absent') { return 'absent' }
    if ($r.State -ne 'removable') { return 'refused' }
    $bytes = ConvertTo-DOA4FileBytes $r.Text $r.Bom
    $tmp = [IO.Path]::Combine([IO.Path]::GetTempPath(), 'doa4-launcher-cmake-' + [Guid]::NewGuid().ToString('N') + '.txt')
    try {
        [IO.File]::WriteAllBytes($tmp, $bytes)
        $h1 = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $RepoPath, 'hash-object', '--path=CMakeLists.txt', '--', $tmp)
        $h2 = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $RepoPath, 'rev-parse', 'HEAD:CMakeLists.txt')
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
    $a = (@($h1.Output) | Select-Object -First 1)
    $b = (@($h2.Output) | Select-Object -First 1)
    if ($h1.ExitCode -ne 0 -or $h2.ExitCode -ne 0 -or -not $a -or $a -ne $b) { return 'refused' }
    if ($CheckOnly) { return 'removable' }
    Write-DOA4FileAtomic -Path $path -Bytes $bytes
    return 'removed'
}

# ------------------------------------------------------------------------------------------
# Game files, the game's toml, the launcher's settings
# ------------------------------------------------------------------------------------------

function Test-DOA4PathFitsCodePage {
    param([string]$Path, [int]$CodePage = 0)
    if ($CodePage -eq 0) { $CodePage = [Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage }
    if (-not $Path -or $CodePage -eq 65001) { return $true }
    $enc = [Text.Encoding]::GetEncoding($CodePage, [Text.EncoderFallback]::ExceptionFallback, [Text.DecoderFallback]::ExceptionFallback)
    try { [void]$enc.GetBytes($Path); return $true } catch { return $false }
}

# The same checks, in the same order and with the same messages, as the game's CheckFolder
# (src/doa4_gamefiles.cpp, src/doa4_setup_flow.cpp VerdictMessage).
# Printable ASCII only (space to tilde).
function Test-DOA4AsciiPath {
    param([string]$Path)
    return ($Path -cmatch '^[\x20-\x7E]*$')
}

# Why the DOA4 folder cannot be used, or $null. The game gets --user_data_root (and, for
# builds without the first-run setup, --game_data_root) on its command line, and the SDK
# reads command-line paths in the ANSI code page (rexglue-sdk src/ui/rex_app.cpp): any
# non-ASCII letter in the DOA4 path sends saves to another folder or stops the game.
function Get-DOA4RootProblem {
    param([Parameter(Mandatory = $true)][string]$Root)
    if (Test-DOA4AsciiPath $Root) { return $null }
    return ('The DOA4 folder''s path has letters the game cannot use: {0}. Move the DOA4 folder to a path with plain letters, digits and spaces (for example C:\Games\DOA4) and run setup again.' -f $Root)
}

# -AsciiOnly for builds without the first-run setup: they get the folder on the command
# line, which the SDK turns into a path through the ANSI code page (a UTF-8 "e-acute"
# becomes two other letters), so only plain ASCII survives.
function Test-DOA4GameFolder {
    param([Parameter(Mandatory = $true)][string]$Path, [string]$ExpectedSha1 = $script:SupportedXexSha1, [int]$CodePage = 0, [switch]$AsciiOnly)
    $ok = { param($v, $m) [pscustomobject]@{ Verdict = $v; Ok = ($v -eq 'Ok'); Message = $m } }
    if (-not (Test-DOA4PathFitsCodePage -Path $Path -CodePage $CodePage) -or ($AsciiOnly -and -not (Test-DOA4AsciiPath $Path))) {
        return & $ok 'PathUnsupported' 'This path has characters that this build cannot use. Choose a folder whose path uses plain letters and digits.'
    }
    $xex = Join-Path $Path 'default.xex'
    if (-not (Test-Path -LiteralPath $xex -PathType Leaf)) {
        return & $ok 'NoXex' 'This folder does not contain default.xex. Choose the folder that holds default.xex.'
    }
    $part = Get-ChildItem -LiteralPath $Path -Recurse -Force -File -Filter '*.part' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($part) {
        return & $ok 'Incomplete' 'This folder holds an unfinished extraction (a .part file). Delete the folder and extract the disc image again.'
    }
    try {
        $hash = (Get-FileHash -LiteralPath $xex -Algorithm SHA1 -ErrorAction Stop).Hash.ToLowerInvariant()
    } catch {
        return & $ok 'Unreadable' 'default.xex could not be read.'
    }
    if ($hash -ne $ExpectedSha1.ToLowerInvariant()) {
        return & $ok 'WrongVersion' ('This is not the supported version. Dead or Alive 4 recomp needs {0}: its default.xex has SHA-1 {1}. The recompiled code matches that exact file; another version would crash.' -f $script:SupportedDisc, $script:SupportedXexSha1)
    }
    return & $ok 'Ok' 'default.xex is the supported version.'
}

function ConvertTo-DOA4TomlString {
    param([string]$Value)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    foreach ($ch in $Value.ToCharArray()) {
        $code = [int]$ch
        if ($ch -eq [char]'"') { [void]$sb.Append('\"') }
        elseif ($ch -eq [char]'\') { [void]$sb.Append('\\') }
        elseif ($code -lt 0x20 -or $code -eq 0x7F) { [void]$sb.Append(('\u{0:X4}' -f $code)) }
        else { [void]$sb.Append($ch) }
    }
    [void]$sb.Append('"')
    return $sb.ToString()
}

# Sets a top-level `key = "value"` in a flat TOML file (the game's dead_or_alive_4.toml),
# keeping every other byte. Refuses (throws, file untouched) when the file has a shape this
# simple editor cannot be sure about: anything before the first [table] that is not a blank
# line, a comment or a one-line `bare_key = value`.
function Set-DOA4TomlString {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Key, [Parameter(Mandatory = $true)][string]$Value)
    if ($Key -notmatch '^[A-Za-z0-9_-]+$') { throw "Not a bare TOML key: $Key" }
    $newLine = $Key + ' = ' + (ConvertTo-DOA4TomlString $Value)
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-DOA4FileAtomic -Path $Path -Bytes ($script:Utf8NoBom.GetBytes($newLine + "`n"))
        return 'created'
    }
    $f = Read-DOA4TextFile $Path
    $nl = Get-DOA4NewLine $f.Text
    $lines = [regex]::Split($f.Text, '(?<=\n)')
    $out = New-Object System.Text.StringBuilder
    $done = $false
    $inTables = $false
    foreach ($line in $lines) {
        if ($line -eq '') { continue }
        $content = $line.TrimEnd("`r", "`n").Trim()
        if (-not $inTables) {
            if ($content.StartsWith('[')) {
                if ($content -notmatch '^\[\[?\s*[A-Za-z0-9_.\-" ]+\s*\]\]?\s*(#.*)?$') {
                    throw "dead_or_alive_4.toml has a line this editor does not understand: $content"
                }
                $inTables = $true
                if (-not $done) { [void]$out.Append($newLine + $nl); $done = $true }
                [void]$out.Append($line)
                continue
            }
            if ($content -eq '' -or $content.StartsWith('#')) { [void]$out.Append($line); continue }
            $m = [regex]::Match($content, '^([A-Za-z0-9_-]+)\s*=\s*(.+)$')
            if (-not $m.Success) { throw "dead_or_alive_4.toml has a line this editor does not understand: $content" }
            $val = $m.Groups[2].Value.Trim()
            if ($val.StartsWith('"""') -or $val.StartsWith("'''") -or
                ($val.StartsWith('[') -and -not ($val -match '\]\s*(#.*)?$')) -or
                ($val.StartsWith('{') -and -not ($val -match '\}\s*(#.*)?$'))) {
                throw "dead_or_alive_4.toml has a multi-line value this editor does not handle: $content"
            }
            if ($m.Groups[1].Value -eq $Key) {
                if ($done) { throw "dead_or_alive_4.toml sets $Key twice" }
                $ending = $line.Substring($line.TrimEnd("`r", "`n").Length)
                [void]$out.Append($newLine + $ending)
                $done = $true
                continue
            }
            [void]$out.Append($line)
            continue
        }
        [void]$out.Append($line)
    }
    $text = $out.ToString()
    if (-not $done) {
        if ($text.Length -gt 0 -and -not $text.EndsWith("`n")) { $text += $nl }
        $text += $newLine + $nl
    }
    Write-DOA4FileAtomic -Path $Path -Bytes (ConvertTo-DOA4FileBytes $text $f.Bom)
    return 'updated'
}

# Reads a top-level basic-string value (`key = "..."`) from a flat TOML file; $null when absent.
function Get-DOA4TomlString {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Key)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    foreach ($line in ((Read-DOA4TextFile $Path).Text -split "\r?\n")) {
        $t = $line.Trim()
        if ($t.StartsWith('[')) { break }
        $m = [regex]::Match($t, '^' + [regex]::Escape($Key) + '\s*=\s*"((?:[^"\\]|\\.)*)"\s*(#.*)?$')
        if ($m.Success) {
            return [regex]::Replace($m.Groups[1].Value, '\\(u[0-9A-Fa-f]{4}|.)', {
                    param($x)
                    $s = $x.Groups[1].Value
                    switch -CaseSensitive ($s) {
                        'n' { "`n" } 't' { "`t" } 'r' { "`r" } '"' { '"' } '\' { '\' }
                        default { if ($s.Length -eq 5) { [string][char][Convert]::ToInt32($s.Substring(1), 16) } else { $s } }
                    }
                })
        }
    }
    return $null
}

function Get-DOA4Settings {
    param([Parameter(Mandatory = $true)][string]$Path)
    $s = @{ gameFiles = ''; lastBuild = $null }
    if (-not (Test-Path -LiteralPath $Path)) { return $s }
    try {
        $o = (Read-DOA4TextFile $Path).Text | ConvertFrom-Json
    } catch {
        return $s
    }
    if ($o.PSObject.Properties['gameFiles']) { $s.gameFiles = [string]$o.gameFiles }
    if ($o.PSObject.Properties['lastBuild'] -and $o.lastBuild) {
        $s.lastBuild = @{ game = [string]$o.lastBuild.game; sdk = [string]$o.lastBuild.sdk; time = [string]$o.lastBuild.time }
    }
    return $s
}

function Save-DOA4Settings {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][hashtable]$Settings)
    $json = $Settings | ConvertTo-Json -Depth 4
    Write-DOA4FileAtomic -Path $Path -Bytes ($script:Utf8NoBom.GetBytes($json))
}

# ------------------------------------------------------------------------------------------
# Shortcut
# ------------------------------------------------------------------------------------------

# Shortcuts go through IShellLinkW: WScript.Shell's CreateShortcut stores the arguments in
# the ANSI code page and turned a CJK folder name into '?' (seen in the tests).
function Initialize-DOA4ShellLink {
    if ('DOA4Launcher.ShellLink' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace DOA4Launcher {
    [ComImport, Guid("000214F9-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellLinkW {
        void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszFile, int cch, IntPtr pfd, uint fFlags);
        void GetIDList(out IntPtr ppidl);
        void SetIDList(IntPtr pidl);
        void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszName, int cch);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string pszName);
        void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszDir, int cch);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string pszDir);
        void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszArgs, int cch);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string pszArgs);
        void GetHotkey(out short pwHotkey);
        void SetHotkey(short wHotkey);
        void GetShowCmd(out int piShowCmd);
        void SetShowCmd(int iShowCmd);
        void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszIconPath, int cch, out int piIcon);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string pszIconPath, int iIcon);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string pszPathRel, uint dwReserved);
        void Resolve(IntPtr hwnd, uint fFlags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string pszFile);
    }
    [ComImport, Guid("0000010b-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPersistFile {
        void GetClassID(out Guid pClassID);
        [PreserveSig] int IsDirty();
        void Load([MarshalAs(UnmanagedType.LPWStr)] string pszFileName, uint dwMode);
        void Save([MarshalAs(UnmanagedType.LPWStr)] string pszFileName, [MarshalAs(UnmanagedType.Bool)] bool fRemember);
        void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string pszFileName);
        void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string ppszFileName);
    }
    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    class CShellLink { }
    public static class ShellLink {
        public static void Write(string lnk, string target, string args, string workDir, string icon, string description) {
            IShellLinkW link = (IShellLinkW)new CShellLink();
            try {
                link.SetPath(target);
                link.SetArguments(args);
                link.SetWorkingDirectory(workDir);
                if (!string.IsNullOrEmpty(icon)) link.SetIconLocation(icon, 0);
                link.SetDescription(description);
                ((IPersistFile)link).Save(lnk, true);
            } finally { Marshal.ReleaseComObject(link); }
        }
        public static string[] Read(string lnk) {
            IShellLinkW link = (IShellLinkW)new CShellLink();
            try {
                ((IPersistFile)link).Load(lnk, 0);
                StringBuilder p = new StringBuilder(2048); link.GetPath(p, p.Capacity, IntPtr.Zero, 0);
                StringBuilder a = new StringBuilder(8192); link.GetArguments(a, a.Capacity);
                StringBuilder w = new StringBuilder(2048); link.GetWorkingDirectory(w, w.Capacity);
                StringBuilder i = new StringBuilder(2048); int index; link.GetIconLocation(i, i.Capacity, out index);
                return new string[] { p.ToString(), a.ToString(), w.ToString(), i.ToString() };
            } finally { Marshal.ReleaseComObject(link); }
        }
    }
}
'@
}

function Get-DOA4Shortcut {
    param([Parameter(Mandatory = $true)][string]$Path)
    Initialize-DOA4ShellLink
    $r = [DOA4Launcher.ShellLink]::Read($Path)
    [pscustomobject]@{ TargetPath = $r[0]; Arguments = $r[1]; WorkingDirectory = $r[2]; IconLocation = $r[3] }
}

function New-DOA4Shortcut {
    param(
        [Parameter(Mandatory = $true)][string]$Folder,
        [Parameter(Mandatory = $true)][string]$LauncherScript,
        [string]$IconPath,
        [string]$Name = 'Dead or Alive 4'
    )
    Initialize-DOA4ShellLink
    $lnk = Join-Path $Folder ($Name + '.lnk')
    $wanted = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}"' -f $LauncherScript
    if (Test-Path -LiteralPath $lnk) {
        $old = Get-DOA4Shortcut -Path $lnk
        if ($old.Arguments -ne $wanted) { return 'kept (a different shortcut has that name)' }
    }
    $icon = ''
    if ($IconPath -and (Test-Path -LiteralPath $IconPath)) { $icon = $IconPath }
    [DOA4Launcher.ShellLink]::Write($lnk, (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'),
        $wanted, (Split-Path -Parent $LauncherScript), $icon, 'Dead or Alive 4 launcher')
    return 'created'
}

# ------------------------------------------------------------------------------------------
# The run context, logging, build, and the actions the window's buttons run
# ------------------------------------------------------------------------------------------

function New-DOA4Context {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][hashtable]$Config,
        [hashtable]$Cmd,
        [scriptblock]$Sink,
        [scriptblock]$OnStep,
        [string]$ShortcutFolder,
        [string]$Action = 'run'
    )
    if (-not $Cmd) { $Cmd = New-DOA4CommandTable }
    $root = [IO.Path]::GetFullPath($Root)
    $launcherDir = Join-Path $root 'launcher'
    $logs = Join-Path $launcherDir 'logs'
    if (-not (Test-Path -LiteralPath $logs)) { [void](New-Item -ItemType Directory -Path $logs) }
    $game = Join-Path $root 'doa4-recomp'
    $buildDir = Join-Path $game $script:BuildDirRel
    @{
        Root = $root; Config = $Config; Cmd = $Cmd; Sink = $Sink; OnStep = $OnStep
        ShortcutFolder = $ShortcutFolder
        LauncherDir = $launcherDir; LauncherScript = (Join-Path $launcherDir 'DOA4Launcher.ps1')
        LogsDir = $logs
        LogPath = (Join-Path $logs ('{0}-{1}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Action.ToLowerInvariant()))
        SettingsPath = (Join-Path $launcherDir 'settings.json')
        GamePath = $game; SdkPath = (Join-Path $root 'rexglue-sdk'); UserData = (Join-Path $root 'userdata')
        BuildDir = $buildDir; ExePath = (Join-Path $buildDir $script:ExeName); TomlPath = (Join-Path $buildDir $script:TomlName)
    }
}

function Write-DOA4Log {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [AllowEmptyString()][string]$Line)
    $stamped = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss'), $Line
    [IO.File]::AppendAllText($Ctx.LogPath, $stamped + "`r`n", $script:Utf8NoBom)
    if ($Ctx.Sink) { & $Ctx.Sink $Line }
}

function Set-DOA4Step {
    param([hashtable]$Ctx, [string]$Name, [string]$State, [string]$Detail = '')
    Write-DOA4Log $Ctx ("[{0}] {1}{2}" -f $Name, $State, $(if ($Detail) { ': ' + $Detail } else { '' }))
    if ($Ctx.OnStep) { & $Ctx.OnStep $Name $State $Detail }
}

function Get-DOA4LogSink {
    param([hashtable]$Ctx)
    $c = $Ctx
    return { param($line) Write-DOA4Log $c $line }.GetNewClosure()
}

function Test-DOA4HasFirstRunSetup {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx)
    return (Test-Path -LiteralPath (Join-Path $Ctx.GamePath 'src\doa4_setup_dialog.cpp'))
}

function Get-DOA4Head {
    param([hashtable]$Cmd, [string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $r = Invoke-DOA4Git -Cmd $Cmd -Arguments @('-C', $Path, 'rev-parse', 'HEAD')
    if ($r.ExitCode -ne 0) { return '' }
    return [string](@($r.Output) | Select-Object -First 1)
}

# 'not built', 'out of date' (a repository HEAD moved since the last successful build) or 'built'.
function Get-DOA4BuildStatus {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx)
    if (-not (Test-Path -LiteralPath $Ctx.ExePath)) { return 'not built' }
    $s = Get-DOA4Settings $Ctx.SettingsPath
    if (-not $s.lastBuild) { return 'out of date' }
    if ($s.lastBuild.game -ne (Get-DOA4Head $Ctx.Cmd $Ctx.GamePath) -or $s.lastBuild.sdk -ne (Get-DOA4Head $Ctx.Cmd $Ctx.SdkPath)) {
        return 'out of date'
    }
    return 'built'
}

# The build script cmd runs. ASCII only; every path arrives through an environment
# variable, so folder names with spaces or non-ASCII letters never pass through the
# batch file's code page.
function Get-DOA4BuildScript {
    return @(
        '@echo off',
        'call "%DOA4_VSDEVCMD%" -arch=amd64 -host_arch=amd64',
        'if errorlevel 1 exit /b 1',
        'set "PATH=%DOA4_LLVM_BIN%;%PATH%"',
        'cd /d "%DOA4_GAME%"',
        'if errorlevel 1 exit /b 1',
        'set "DOA4_CONFIGURE="',
        'if not exist "out\build\win-amd64-release\CMakeCache.txt" set "DOA4_CONFIGURE=1"',
        'if not exist "out\build\win-amd64-release\build.ninja" set "DOA4_CONFIGURE=1"',
        'if defined DOA4_CONFIGURE (',
        '  echo [launcher] configuring',
        '  cmake --preset win-amd64-release "-DREXSDK_DIR=%DOA4_SDK_FWD%" "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3" 2>&1',
        ')',
        'if errorlevel 1 exit /b 1',
        'echo [launcher] building',
        'cmake --build --preset win-amd64-release 2>&1',
        'exit /b %errorlevel%'
    ) -join "`r`n"
}

function Invoke-DOA4Build {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [Parameter(Mandatory = $true)][string]$VsDevCmd, [string]$LlvmBin = (Get-DOA4LlvmBin))
    $bat = Join-Path $Ctx.LogsDir 'build.cmd'
    [IO.File]::WriteAllText($bat, (Get-DOA4BuildScript) + "`r`n", [Text.Encoding]::ASCII)
    $envVars = @{
        DOA4_VSDEVCMD = $VsDevCmd
        DOA4_LLVM_BIN = $LlvmBin
        DOA4_GAME = $Ctx.GamePath
        DOA4_SDK_FWD = $Ctx.SdkPath.Replace('\', '/')
    }
    $r = & $Ctx.Cmd.cmd $bat $envVars $Ctx.GamePath (Get-DOA4LogSink $Ctx)
    if ($r.NotFound) { throw 'cmd.exe was not found.' }
    if ($r.ExitCode -ne 0) { throw ("The build failed (exit {0}). The log has the compiler's messages: {1}" -f $r.ExitCode, $Ctx.LogPath) }
    if (-not (Test-Path -LiteralPath $Ctx.ExePath)) { throw "The build finished but $($Ctx.ExePath) is missing." }
    $s = Get-DOA4Settings $Ctx.SettingsPath
    $s.lastBuild = @{ game = (Get-DOA4Head $Ctx.Cmd $Ctx.GamePath); sdk = (Get-DOA4Head $Ctx.Cmd $Ctx.SdkPath); time = (Get-Date).ToString('s') }
    Save-DOA4Settings -Path $Ctx.SettingsPath -Settings $s
    return 'built'
}

function Get-DOA4RequiredBytes { param([hashtable]$Config) return [int64]([double]$Config.RequiredFreeGB * 1GB) }

function New-DOA4StepResult {
    param([bool]$Ok, [string]$Step = '', [string]$Message = '', $Ctx, $Plan = $null)
    [pscustomobject]@{ Ok = $Ok; Step = $Step; Message = $Message; LogPath = $Ctx.LogPath; Plan = $Plan }
}

# Tool detection plus the install plan, without changing anything (the window shows the
# plan and asks once; -DryRun prints it).
function Get-DOA4InstallPlan {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [string[]]$AssumeMissing = @())
    $status = Get-DOA4ToolStatus -Cmd $Ctx.Cmd -Config $Ctx.Config
    foreach ($t in $status) {
        if ($AssumeMissing -contains $t.Key) { $t.Present = $false; $t.Detail = 'missing (assumed by -AssumeMissing)' }
    }
    $plan = Get-DOA4MissingToolPlan -Status $status -Config $Ctx.Config
    [pscustomobject]@{ Status = $status; Plan = $plan }
}

function Write-DOA4DryRun {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [string[]]$AssumeMissing = @())
    $p = Get-DOA4InstallPlan -Ctx $Ctx -AssumeMissing $AssumeMissing
    Write-DOA4Log $Ctx 'Dry run: nothing is installed, cloned or built.'
    foreach ($t in $p.Status) { Write-DOA4Log $Ctx ('  {0}: {1}' -f $t.Name, $t.Detail) }
    if ($p.Plan.Blocked) { Write-DOA4Log $Ctx $p.Plan.Blocked }
    if ($p.Plan.Items.Count -eq 0) {
        Write-DOA4Log $Ctx 'Tools: nothing to install.'
    } else {
        Write-DOA4Log $Ctx 'Tools: setup would run, after one confirmation:'
        Install-DOA4Tools -Cmd $Ctx.Cmd -Plan $p.Plan -DryRun -OnLine (Get-DOA4LogSink $Ctx) | Out-Null
        foreach ($i in $p.Plan.Items) { Write-DOA4Log $Ctx ('  ({0}: {1})' -f $i.Name, $i.Size) }
    }
    $c = $Ctx.Config
    Write-DOA4Log $Ctx ('Then: check free space (about {0}), clone {1} ({2}) into {3}, clone {4} ({5}) into {6} with its submodules, apply the fixes, build, and create the desktop shortcut.' -f (Format-DOA4Bytes (Get-DOA4RequiredBytes $c)), $c.GameRepoUrl, $c.GameRef, $Ctx.GamePath, $c.SdkRepoUrl, $c.SdkRef, $Ctx.SdkPath)
    return $p
}

# Install: Tools, Disk space, Code, Fixes, Build, Shortcut. Stops at the first failure.
# Tools are installed only with -ToolsApproved (the window's one confirmation).
function Invoke-DOA4Install {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [switch]$ToolsApproved)
    $c = $Ctx.Config
    $step = 'Folder'
    $problem = Get-DOA4RootProblem $Ctx.Root
    if ($problem) { Set-DOA4Step $Ctx $step 'failed' $problem; return New-DOA4StepResult $false $step $problem $Ctx }
    $step = 'Tools'
    try {
        Set-DOA4Step $Ctx $step 'running'
        $p = Get-DOA4InstallPlan -Ctx $Ctx
        foreach ($t in $p.Status) { Write-DOA4Log $Ctx ('  {0}: {1}' -f $t.Name, $t.Detail) }
        if ($p.Plan.Blocked) { Set-DOA4Step $Ctx $step 'failed' $p.Plan.Blocked; return New-DOA4StepResult $false $step $p.Plan.Blocked $Ctx $p.Plan }
        if ($p.Plan.Items.Count -gt 0) {
            $names = ($p.Plan.Items | ForEach-Object { '{0} ({1})' -f $_.Name, $_.Size }) -join ', '
            if (-not $ToolsApproved) {
                $msg = "Missing tools: $names. Run setup and answer yes to install them."
                Set-DOA4Step $Ctx $step 'failed' $msg
                return New-DOA4StepResult $false $step $msg $Ctx $p.Plan
            }
            Install-DOA4Tools -Cmd $Ctx.Cmd -Plan $p.Plan -OnLine (Get-DOA4LogSink $Ctx) | Out-Null
            $p = Get-DOA4InstallPlan -Ctx $Ctx
            if ($p.Plan.Items.Count -gt 0) {
                $msg = 'Still missing after installing: ' + (($p.Plan.Items | ForEach-Object { '{0} ({1})' -f $_.Name, $_.Reason }) -join ', ') + '. A restart of Windows may be needed; then run setup again.'
                Set-DOA4Step $Ctx $step 'failed' $msg
                return New-DOA4StepResult $false $step $msg $Ctx $p.Plan
            }
            Set-DOA4Step $Ctx $step 'done' 'installed'
        } else {
            Set-DOA4Step $Ctx $step 'done' 'nothing to install'
        }
        $vs = Get-DOA4Tool $p.Status 'vs'

        $step = 'Disk space'
        if (Test-Path -LiteralPath $Ctx.ExePath) {
            Set-DOA4Step $Ctx $step 'done' 'already built'
        } else {
            $d = Test-DOA4DiskSpace -Cmd $Ctx.Cmd -Path $Ctx.Root -RequiredBytes (Get-DOA4RequiredBytes $c)
            if (-not $d.Ok) { Set-DOA4Step $Ctx $step 'failed' $d.Message; return New-DOA4StepResult $false $step $d.Message $Ctx }
            Set-DOA4Step $Ctx $step 'done' $d.Message
        }

        $step = 'Code'
        Set-DOA4Step $Ctx $step 'running' 'the first clone downloads about 2 GB and takes several minutes'
        $sink = Get-DOA4LogSink $Ctx
        $g = Sync-DOA4Repo -Cmd $Ctx.Cmd -Path $Ctx.GamePath -Url $c.GameRepoUrl -Ref $c.GameRef -OnLine $sink -Kind 'game'
        Write-DOA4Log $Ctx "  doa4-recomp: $g"
        $s = Sync-DOA4Repo -Cmd $Ctx.Cmd -Path $Ctx.SdkPath -Url $c.SdkRepoUrl -Ref $c.SdkRef -OnLine $sink -Kind 'sdk'
        Write-DOA4Log $Ctx "  rexglue-sdk: $s"
        Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Ctx.SdkPath, 'submodule', 'update', '--init', '--recursive') -OnLine $sink -Check | Out-Null
        if (-not (Test-Path -LiteralPath $Ctx.UserData)) { [void](New-Item -ItemType Directory -Path $Ctx.UserData) }
        Set-DOA4Step $Ctx $step 'done' "doa4-recomp $g, rexglue-sdk $s"

        $step = 'Fixes'
        $stubs = Repair-DOA4SymlinkStubs -Cmd $Ctx.Cmd -RepoPath $Ctx.SdkPath
        $cm = Add-DOA4CMakeIncludeFix -Path (Join-Path $Ctx.GamePath 'CMakeLists.txt')
        Set-DOA4Step $Ctx $step 'done' ('symlink stubs replaced: {0}; CMake include fix: {1}' -f $stubs.Fixed, $cm)

        $step = 'Build'
        $avx = Test-DOA4Avx2 -Cmd $Ctx.Cmd
        if ($avx.Supported -eq $false) { Set-DOA4Step $Ctx $step 'failed' $avx.Detail; return New-DOA4StepResult $false $step $avx.Detail $Ctx }
        if ($null -eq $avx.Supported) { Write-DOA4Log $Ctx "  AVX2: $($avx.Detail); building anyway" }
        Set-DOA4Step $Ctx $step 'running' 'the first build takes a long time'
        Invoke-DOA4Build -Ctx $Ctx -VsDevCmd $vs.VsDevCmd | Out-Null
        Set-DOA4Step $Ctx $step 'done'

        $step = 'Shortcut'
        if ($Ctx.ShortcutFolder) {
            $r = New-DOA4Shortcut -Folder $Ctx.ShortcutFolder -LauncherScript $Ctx.LauncherScript -IconPath $Ctx.ExePath
            Set-DOA4Step $Ctx $step 'done' $r
        } else {
            Set-DOA4Step $Ctx $step 'skipped' 'no shortcut folder given'
        }
        return New-DOA4StepResult $true '' 'Installed.' $Ctx
    } catch {
        $msg = $_.Exception.Message
        Set-DOA4Step $Ctx $step 'failed' $msg
        return New-DOA4StepResult $false $step $msg $Ctx
    }
}

# Every local change in both repositories, without changing anything. Allowed are only the
# launcher's own fixes: the CMake block in doa4-recomp (CMakeFix) and the symlink copies in
# rexglue-sdk (Sdk.Stubs). Everything else is in Foreign, and then the update writes nothing.
# The tracked changes in one repository that are not the launcher's own fixes (no writes).
# Kind 'game': the CMake block is allowed (CMakeFix tells whether it is there); 'sdk': the
# symlink copies are allowed (Sdk holds Get-DOA4StubChanges' answer); 'plain': nothing is.
function Get-DOA4ForeignChanges {
    param([hashtable]$Cmd, [string]$Path, [ValidateSet('game', 'sdk', 'plain')][string]$Kind = 'plain')
    if ($Kind -eq 'sdk') {
        $sdk = Get-DOA4StubChanges -Cmd $Cmd -RepoPath $Path
        return [pscustomobject]@{ Foreign = @($sdk.Foreign); CMakeFix = $false; Sdk = $sdk }
    }
    $foreign = New-Object System.Collections.Generic.List[string]
    $cmakeChanged = $false
    foreach ($e in (Get-DOA4StatusEntries -Cmd $Cmd -RepoPath $Path)) {
        if ($Kind -eq 'game' -and $e.Type -eq '1' -and $e.XY -eq '.M' -and $e.Sub.StartsWith('N') -and $e.Path -eq 'CMakeLists.txt') { $cmakeChanged = $true; continue }
        $foreign.Add($e.Path)
    }
    $cmakeFix = $false
    if ($cmakeChanged) {
        if ((Remove-DOA4CMakeIncludeFix -Cmd $Cmd -RepoPath $Path -CheckOnly) -eq 'removable') { $cmakeFix = $true }
        else { $foreign.Add("CMakeLists.txt (changes besides the launcher's build fix)") }
    }
    [pscustomobject]@{ Foreign = $foreign.ToArray(); CMakeFix = $cmakeFix; Sdk = $null }
}

# Every local change in both repositories, without changing anything. Allowed are only the
# launcher's own fixes: the CMake block in doa4-recomp (CMakeFix) and the symlink copies in
# rexglue-sdk (Sdk.Stubs). Everything else is in Foreign, and then the update writes nothing.
function Get-DOA4UpdateBlockers {
    param([hashtable]$Ctx)
    $game = Get-DOA4ForeignChanges -Cmd $Ctx.Cmd -Path $Ctx.GamePath -Kind 'game'
    $sdk = Get-DOA4ForeignChanges -Cmd $Ctx.Cmd -Path $Ctx.SdkPath -Kind 'sdk'
    $foreign = @($game.Foreign | ForEach-Object { 'doa4-recomp/' + $_ }) + @($sdk.Foreign | ForEach-Object { 'rexglue-sdk/' + $_ })
    [pscustomobject]@{ Foreign = $foreign; CMakeFix = $game.CMakeFix; Sdk = $sdk.Sdk }
}

# Takes the launcher's two fixes out (only ever called after Get-DOA4UpdateBlockers found
# nothing foreign): the CMake block, and the symlink copies back to git's stubs.
function Remove-DOA4FixesForUpdate {
    param([hashtable]$Ctx, $Blockers)
    if ($Blockers.CMakeFix) {
        $r = Remove-DOA4CMakeIncludeFix -Cmd $Ctx.Cmd -RepoPath $Ctx.GamePath
        if ($r -ne 'removed') { throw "doa4-recomp/CMakeLists.txt changed during the update ($r)." }
        Write-DOA4Log $Ctx '  doa4-recomp: build fix removed for the update'
    }
    $ch = $Blockers.Sdk
    foreach ($m in $ch.Moved) { Write-DOA4Log $Ctx "  rexglue-sdk/$m is at another commit; the submodule update puts it back" }
    foreach ($s in @($ch.Stubs | Where-Object { $_.Folder })) {
        Remove-Item -LiteralPath $s.FullPath -Recurse -Force
    }
    foreach ($grp in @($ch.Stubs | Group-Object -Property Repo)) {
        $paths = @($grp.Group | ForEach-Object { $_.Path })
        Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments (@('-C', $grp.Name, 'checkout', '--') + $paths) -Check | Out-Null
    }
    if ($ch.Stubs.Count -gt 0) { Write-DOA4Log $Ctx ('  rexglue-sdk: {0} symlink copies restored to stubs for the update' -f $ch.Stubs.Count) }
}

# After a fetch: would updating move this repository (another commit, or another branch)?
function Test-DOA4RepoWouldMove {
    param([hashtable]$Ctx, [string]$Path, [string]$Ref)
    if (-not (Test-DOA4CommitRef $Ref)) {
        $cur = Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'symbolic-ref', '--short', '-q', 'HEAD')
        if ([string](@($cur.Output) | Select-Object -First 1) -ne $Ref) { return $true }
        $target = Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'rev-parse', '--verify', "origin/$Ref^{commit}")
    } else {
        $target = Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'rev-parse', '--verify', "$Ref^{commit}")
    }
    if ($target.ExitCode -ne 0) { return $true }
    return ([string](@($target.Output) | Select-Object -First 1) -ne (Get-DOA4Head $Ctx.Cmd $Path))
}

function Update-DOA4RepoToRef {
    param([hashtable]$Ctx, [string]$Path, [string]$Ref, [string]$Name)
    $sink = Get-DOA4LogSink $Ctx
    $before = Get-DOA4Head $Ctx.Cmd $Path
    if (Test-DOA4CommitRef $Ref) {
        Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'checkout', '--detach', $Ref) -OnLine $sink -Check | Out-Null
    } else {
        $cur = Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'symbolic-ref', '--short', '-q', 'HEAD')
        $curName = [string](@($cur.Output) | Select-Object -First 1)
        if ($curName -ne $Ref) {
            Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'checkout', $Ref) -OnLine $sink -Check | Out-Null
        }
        Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Path, 'merge', '--ff-only', "origin/$Ref") -OnLine $sink -Check | Out-Null
    }
    $after = Get-DOA4Head $Ctx.Cmd $Path
    if ($before -eq $after) { Write-DOA4Log $Ctx "  ${Name}: already up to date ($($after.Substring(0, [Math]::Min(10, $after.Length))))" }
    else { Write-DOA4Log $Ctx "  ${Name}: $before -> $after" }
}

# Update & rebuild. Local changes stop it, except the launcher's own two fixes, which are
# taken out before the pull and put back afterwards (also when the pull fails).
function Invoke-DOA4Update {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx)
    $c = $Ctx.Config
    $step = 'Folder'
    $problem = Get-DOA4RootProblem $Ctx.Root
    if ($problem) { Set-DOA4Step $Ctx $step 'failed' $problem; return New-DOA4StepResult $false $step $problem $Ctx }
    $step = 'Tools'
    try {
        if (-not (Test-Path -LiteralPath (Join-Path $Ctx.GamePath '.git')) -or -not (Test-Path -LiteralPath (Join-Path $Ctx.SdkPath '.git'))) {
            $msg = 'Not installed yet: run Install first.'
            Set-DOA4Step $Ctx $step 'failed' $msg
            return New-DOA4StepResult $false $step $msg $Ctx
        }
        $status = Get-DOA4ToolStatus -Cmd $Ctx.Cmd -Config $c
        $missing = @($status | Where-Object { -not $_.Present -and $_.Key -ne 'winget' })
        if ($missing.Count -gt 0) {
            $msg = 'Missing tools: ' + (($missing | ForEach-Object { $_.Name }) -join ', ') + '. Run Install.'
            Set-DOA4Step $Ctx $step 'failed' $msg
            return New-DOA4StepResult $false $step $msg $Ctx
        }
        Set-DOA4Step $Ctx $step 'done' 'present'
        $vs = Get-DOA4Tool $status 'vs'

        # Install's rule: a folder builds the repository it was cloned from. A changed URL in
        # config.ps1 is a new source, installed into a new DOA4 folder (never fetched here).
        $step = 'Sources'
        foreach ($repo in @(@{ Name = 'doa4-recomp'; Path = $Ctx.GamePath; Url = $c.GameRepoUrl }, @{ Name = 'rexglue-sdk'; Path = $Ctx.SdkPath; Url = $c.SdkRepoUrl })) {
            $origin = Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $repo.Path, 'remote', 'get-url', 'origin')
            $originUrl = [string](@($origin.Output) | Select-Object -First 1)
            if ($origin.ExitCode -ne 0 -or -not $originUrl -or (ConvertTo-DOA4UrlKey $originUrl) -ne (ConvertTo-DOA4UrlKey ([string]$repo.Url))) {
                $msg = ('Not updated: {0} is a clone of {1}, but config.ps1 names {2}. To build that source, unpack the launcher into a new DOA4 folder and run setup there; this folder keeps building {1}.' -f $repo.Name, $originUrl, $repo.Url)
                Set-DOA4Step $Ctx $step 'failed' $msg
                return New-DOA4StepResult $false $step $msg $Ctx
            }
        }
        Set-DOA4Step $Ctx $step 'done' 'as in config.ps1'

        $step = 'Local changes'
        $blockers = Get-DOA4UpdateBlockers $Ctx
        if ($blockers.Foreign.Count -gt 0) {
            $msg = 'Not updated: these files have local changes (the launcher never stashes, resets or overwrites them): ' + ($blockers.Foreign -join ', ')
            Set-DOA4Step $Ctx $step 'failed' $msg
            return New-DOA4StepResult $false $step $msg $Ctx
        }
        Set-DOA4Step $Ctx $step 'done' 'none besides the launcher fixes'

        $step = 'Update'
        Set-DOA4Step $Ctx $step 'running'
        # Fetching changes no working-tree file; only when a repository would move are the
        # fixes taken out (rewriting them unchanged would make CMake re-run and objects rebuild).
        $sink = Get-DOA4LogSink $Ctx
        Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Ctx.GamePath, 'fetch', 'origin') -OnLine $sink -Check | Out-Null
        Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Ctx.SdkPath, 'fetch', 'origin') -OnLine $sink -Check | Out-Null
        $moves = (Test-DOA4RepoWouldMove $Ctx $Ctx.GamePath $c.GameRef) -or (Test-DOA4RepoWouldMove $Ctx $Ctx.SdkPath $c.SdkRef) -or
            ($blockers.Sdk.Moved.Count -gt 0)
        if (-not $moves) {
            Set-DOA4Step $Ctx $step 'done' 'nothing new: doa4-recomp and rexglue-sdk are at their configured versions'
        } else {
            $updateError = $null
            try {
                Remove-DOA4FixesForUpdate $Ctx $blockers
                Update-DOA4RepoToRef -Ctx $Ctx -Path $Ctx.GamePath -Ref $c.GameRef -Name 'doa4-recomp'
                Update-DOA4RepoToRef -Ctx $Ctx -Path $Ctx.SdkPath -Ref $c.SdkRef -Name 'rexglue-sdk'
                Invoke-DOA4Git -Cmd $Ctx.Cmd -Arguments @('-C', $Ctx.SdkPath, 'submodule', 'update', '--init', '--recursive') -OnLine $sink -Check | Out-Null
            } catch {
                $updateError = $_.Exception.Message
            }
            # The fixes go back whether or not the pull worked.
            $stubs = Repair-DOA4SymlinkStubs -Cmd $Ctx.Cmd -RepoPath $Ctx.SdkPath
            $cm = Add-DOA4CMakeIncludeFix -Path (Join-Path $Ctx.GamePath 'CMakeLists.txt')
            Write-DOA4Log $Ctx ('  fixes re-applied: symlink stubs replaced: {0}; CMake include fix: {1}' -f $stubs.Fixed, $cm)
            if ($updateError) {
                Set-DOA4Step $Ctx $step 'failed' $updateError
                return New-DOA4StepResult $false $step $updateError $Ctx
            }
            Set-DOA4Step $Ctx $step 'done'
        }

        $step = 'Build'
        $avx = Test-DOA4Avx2 -Cmd $Ctx.Cmd
        if ($avx.Supported -eq $false) { Set-DOA4Step $Ctx $step 'failed' $avx.Detail; return New-DOA4StepResult $false $step $avx.Detail $Ctx }
        Set-DOA4Step $Ctx $step 'running'
        Invoke-DOA4Build -Ctx $Ctx -VsDevCmd $vs.VsDevCmd | Out-Null
        Set-DOA4Step $Ctx $step 'done'
        return New-DOA4StepResult $true '' 'Updated and rebuilt.' $Ctx
    } catch {
        $msg = $_.Exception.Message
        Set-DOA4Step $Ctx $step 'failed' $msg
        return New-DOA4StepResult $false $step $msg $Ctx
    }
}

# Play: a build with the first-run setup gets no game path (it asks for its files itself);
# any other build gets the launcher's Game files setting as --game_data_root.
function Get-DOA4PlayArguments {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [string]$GameFiles)
    # Always passed, on purpose: it overrides the game's own doa4_user_data setting so saves
    # and the shader cache stay inside this DOA4 folder. Plain ASCII is guaranteed by the
    # root check (Get-DOA4RootProblem), so the SDK reads it back unchanged.
    $a = @("--user_data_root=$($Ctx.UserData)")
    if (-not (Test-DOA4HasFirstRunSetup $Ctx)) {
        if (-not $GameFiles) { $GameFiles = (Get-DOA4Settings $Ctx.SettingsPath).gameFiles }
        if (-not $GameFiles) { throw 'NEED_GAME_FILES' }
        $a += "--game_data_root=$GameFiles"
    }
    return , $a
}

function Get-DOA4GameStartInfo {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [string]$GameFiles)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Ctx.ExePath
    $psi.Arguments = ConvertTo-DOA4ArgumentString (Get-DOA4PlayArguments -Ctx $Ctx -GameFiles $GameFiles)
    $psi.WorkingDirectory = $Ctx.BuildDir
    # Through the shell: the game must not inherit the launcher's console or output pipe
    # (a caller capturing the launcher's output would otherwise wait for the game to exit).
    $psi.UseShellExecute = $true
    return $psi
}

function Start-DOA4Game {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [string]$GameFiles)
    $problem = Get-DOA4RootProblem $Ctx.Root
    if ($problem) { throw $problem }
    if (-not (Test-Path -LiteralPath $Ctx.ExePath)) { throw 'The game is not built yet: run Install.' }
    $a = Get-DOA4PlayArguments -Ctx $Ctx -GameFiles $GameFiles
    if (-not (Test-DOA4HasFirstRunSetup $Ctx)) {
        $gf = ($a | Where-Object { $_.StartsWith('--game_data_root=') }).Substring(17)
        $check = Test-DOA4GameFolder -Path $gf -AsciiOnly
        if (-not $check.Ok) { throw $check.Message }
    }
    if (-not (Test-Path -LiteralPath $Ctx.UserData)) { [void](New-Item -ItemType Directory -Path $Ctx.UserData) }
    $psi = Get-DOA4GameStartInfo -Ctx $Ctx -GameFiles $GameFiles
    Write-DOA4Log $Ctx ('Play: {0} {1}' -f $Ctx.ExePath, $psi.Arguments)
    return [System.Diagnostics.Process]::Start($psi)
}

# Change game files: checked like Play; a first-run-setup build gets doa4_game_files in its
# toml (forward slashes, as the game itself saves it); other builds get the launcher setting.
function Set-DOA4GameFiles {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx, [Parameter(Mandatory = $true)][string]$Folder)
    # A first-run-setup build reads the folder from its toml as UTF-8 (the code-page rule is
    # the game's own); other builds get it on the command line, so it must be plain ASCII.
    $check = Test-DOA4GameFolder -Path $Folder -AsciiOnly:(-not (Test-DOA4HasFirstRunSetup $Ctx))
    if (-not $check.Ok) { throw $check.Message }
    $full = [IO.Path]::GetFullPath($Folder)
    if (Test-DOA4HasFirstRunSetup $Ctx) {
        if (-not (Test-Path -LiteralPath $Ctx.BuildDir)) { throw 'The game is not built yet: run Install first.' }
        $r = Set-DOA4TomlString -Path $Ctx.TomlPath -Key 'doa4_game_files' -Value ($full.Replace('\', '/'))
        Write-DOA4Log $Ctx "Game files: $full (doa4_game_files in $($Ctx.TomlPath), $r; used from the next start)"
        return 'toml'
    }
    $s = Get-DOA4Settings $Ctx.SettingsPath
    $s.gameFiles = $full
    Save-DOA4Settings -Path $Ctx.SettingsPath -Settings $s
    Write-DOA4Log $Ctx "Game files: $full (launcher setting)"
    return 'settings'
}

function Get-DOA4FolderTargets {
    param([Parameter(Mandatory = $true)][hashtable]$Ctx)
    $photos = Get-DOA4TomlString -Path $Ctx.TomlPath -Key 'doa4_photo_dir'
    if (-not $photos) { $photos = Join-Path ([Environment]::GetFolderPath('MyPictures')) 'DOA4' }
    @{ Photos = $photos; Saves = $Ctx.UserData; Logs = (Join-Path $Ctx.BuildDir 'logs') }
}

Export-ModuleMember -Function *-DOA4*
