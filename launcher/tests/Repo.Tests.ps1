. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Odd but legal folder names: a space, a Latin-1 letter, a CJK letter (outside code page 1252).
$script:Odd = 'my games ' + [char]0x00E9 + [char]0x65E5

Describe 'Sync-DOA4Repo with a fake git' {
    It 'clones a branch with --branch into an absent folder' {
        $git = New-FakeCommand 'git' @(@{ Match = '^clone ' })
        $p = Join-Path $TestDrive 'absent'
        Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://github.com/parkerallan/doa4-recomp' -Ref 'main' | Should Be 'cloned'
        $git.State.Calls[0] | Should BeExactly "clone --branch main -- https://github.com/parkerallan/doa4-recomp $p"
    }
    It 'clones a pinned commit, then checks it out detached' {
        $git = New-FakeCommand 'git' @(@{ Match = '^clone ' }, @{ Match = 'checkout --detach e8ce24f$' })
        $p = Join-Path $TestDrive 'absent2'
        Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://github.com/rexglue/rexglue-sdk' -Ref 'e8ce24f' | Out-Null
        $git.State.Calls[0] | Should BeExactly "clone -- https://github.com/rexglue/rexglue-sdk $p"
        $git.State.Calls[1] | Should BeExactly "-C $p checkout --detach e8ce24f"
    }
    It 'adds --no-hardlinks for a local-path URL (never hard-links into the source repository)' {
        $git = New-FakeCommand 'git' @(@{ Match = '^clone ' })
        $p = Join-Path $TestDrive 'absent3'
        Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'C:/Users/someone/doa4/doa4-recomp' -Ref 'perf-pacing' | Out-Null
        $git.State.Calls[0] | Should BeExactly "clone --no-hardlinks --branch perf-pacing -- C:/Users/someone/doa4/doa4-recomp $p"
    }
    It 'clones into an existing EMPTY folder' {
        $p = Join-Path $TestDrive 'emptydir'
        [void](New-Item -ItemType Directory -Path $p)
        $git = New-FakeCommand 'git' @(@{ Match = '^clone ' })
        Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://x/y' -Ref 'main' | Should Be 'cloned'
    }
    It 'reuses a folder that already holds a clone of the same URL (ignoring .git and case) and never clones' {
        $p = Join-Path $TestDrive 'have'
        [void](New-Item -ItemType Directory -Path (Join-Path $p '.git'))
        $git = New-FakeCommand 'git' @(
            @{ Match = 'rev-parse --show-toplevel'; Output = @($p.Replace('\', '/')) },
            @{ Match = 'rev-parse --verify HEAD'; Output = @('1d38c03ee363968594cbe6d5d46d28381b88d7fc') },
            @{ Match = 'remote get-url origin'; Output = @('https://GitHub.com/parkerallan/doa4-recomp.git') },
            @{ Match = 'status --porcelain=v2 --untracked-files=no$'; Output = @() })   # clean (review I2)
        Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://github.com/parkerallan/doa4-recomp' -Ref 'main' | Should Be 'reused'
        @($git.State.Calls | Where-Object { $_ -like 'clone*' }).Count | Should Be 0
    }
    It 'refuses a non-empty folder that is not a clone, and leaves it alone' {
        $p = Join-Path $TestDrive 'notrepo'
        Write-TFile (Join-Path $p 'mine.txt') 'user file'
        $git = New-FakeCommand 'git' @(@{ Match = 'rev-parse --show-toplevel'; Exit = 128; Output = @('fatal: not a git repository') })
        { Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://x/y' -Ref 'main' } | Should Throw 'already exists and is not a git clone'
        Get-Content -LiteralPath (Join-Path $p 'mine.txt') | Should Be 'user file'
    }
    It 'refuses a folder inside another repository (toplevel is a parent)' {
        $p = Join-Path $TestDrive 'inner'
        Write-TFile (Join-Path $p 'f.txt') 'x'
        $git = New-FakeCommand 'git' @(@{ Match = 'rev-parse --show-toplevel'; Output = @(([string]$TestDrive).Replace('\', '/')) })
        { Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://x/y' -Ref 'main' } | Should Throw 'not a git clone'
    }
    It 'refuses a clone of another URL' {
        $p = Join-Path $TestDrive 'other'
        Write-TFile (Join-Path $p 'f.txt') 'x'
        $git = New-FakeCommand 'git' @(
            @{ Match = 'rev-parse --show-toplevel'; Output = @($p) },
            @{ Match = 'rev-parse --verify HEAD'; Output = @('abc') },
            @{ Match = 'remote get-url origin'; Output = @('https://github.com/someone/fork') })
        { Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://github.com/parkerallan/doa4-recomp' -Ref 'main' } | Should Throw 'is a clone of https://github.com/someone/fork'
    }
    It 'refuses an unfinished clone (no HEAD)' {
        $p = Join-Path $TestDrive 'half'
        Write-TFile (Join-Path $p 'f.txt') 'x'
        $git = New-FakeCommand 'git' @(
            @{ Match = 'rev-parse --show-toplevel'; Output = @($p) },
            @{ Match = 'rev-parse --verify HEAD'; Exit = 128; Output = @('fatal: Needed a single revision') })
        { Sync-DOA4Repo -Cmd @{ git = $git.Invoke } -Path $p -Url 'https://x/y' -Ref 'main' } | Should Throw 'unfinished clone'
    }
}

Describe 'Sync-DOA4Repo with real git (paths with spaces and non-ANSI letters)' {
    $base = Join-Path $TestDrive $script:Odd
    $o = New-TOrigins -Base $base
    $target = Join-Path $base 'DOA4\doa4-recomp'
    $real = New-DOA4CommandTable
    It 'clones, then reuses on the second run' {
        Sync-DOA4Repo -Cmd $real -Path $target -Url $o.Game -Ref 'main' | Should Be 'cloned'
        Test-Path -LiteralPath (Join-Path $target 'CMakeLists.txt') | Should Be $true
        Sync-DOA4Repo -Cmd $real -Path $target -Url $o.Game -Ref 'main' | Should Be 'reused'
    }
    It 'moves a reused clean clone to a pinned commit' {
        $first = @(Invoke-TGit $o.Game @('rev-parse', 'HEAD'))[0]
        Write-TFile (Join-Path $o.Game 'later.txt') "later`n"
        Invoke-TGit $o.Game @('add', '-A') | Out-Null
        Invoke-TGit $o.Game @('commit', '-q', '-m', 'later') | Out-Null
        $pin = Join-Path $base 'DOA4\pinned'
        Sync-DOA4Repo -Cmd $real -Path $pin -Url $o.Game -Ref 'main' | Out-Null
        Sync-DOA4Repo -Cmd $real -Path $pin -Url $o.Game -Ref $first.Substring(0, 7) | Should Be 'reused (moved to the configured commit)'
        @(Invoke-TGit $pin @('rev-parse', 'HEAD'))[0] | Should Be $first
    }
}

Describe 'CMake include fix' {
    $fixLine = 'target_include_directories(dead_or_alive_4 PRIVATE ${CMAKE_BINARY_DIR}/rexglue-sdk/include)'

    It 'inserts both blocks right after rexglue_setup_target, in the file''s line endings (CRLF)' {
        $p = Join-Path $TestDrive 'crlf\CMakeLists.txt'
        Write-TFile $p ($script:TestCMakeText.Replace("`n", "`r`n"))
        Add-DOA4CMakeIncludeFix -Path $p | Should Be 'added'
        $t = [IO.File]::ReadAllText($p)
        $t | Should Match ('rexglue_setup_target\(dead_or_alive_4\)\r\n\r\n# DOA4 launcher build fix')
        $t.Contains('    ' + $fixLine + "`r`nendif()`r`n`r`ninstall(TARGETS") | Should Be $true
        $t.Contains('$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>') | Should Be $true
        ([regex]::Matches($t, '(?<!\r)\n')).Count | Should Be 0
    }
    It 'keeps LF files LF and a BOM a BOM' {
        $p = Join-Path $TestDrive 'lf\CMakeLists.txt'
        $bytes = [byte[]](0xEF, 0xBB, 0xBF) + [Text.Encoding]::UTF8.GetBytes($script:TestCMakeText)
        [void](New-Item -ItemType Directory -Force -Path (Split-Path $p))
        [IO.File]::WriteAllBytes($p, $bytes)
        Add-DOA4CMakeIncludeFix -Path $p | Out-Null
        $after = [IO.File]::ReadAllBytes($p)
        $after[0] | Should Be 0xEF
        [Text.Encoding]::UTF8.GetString($after).Contains("`r") | Should Be $false
    }
    It 'skips when the rexglue-sdk/include line is already there (perf-pacing has it), file byte-identical' {
        $p = Join-Path $TestDrive 'present\CMakeLists.txt'
        Write-TFile $p ($script:TestCMakeText + "if(REXSDK_DIR)`n    $fixLine`nendif()`n")
        $before = Get-TBytesHash $p
        Add-DOA4CMakeIncludeFix -Path $p | Should Be 'present'
        Get-TBytesHash $p | Should Be $before
    }
    It 'refuses a file without the anchor line, untouched' {
        $p = Join-Path $TestDrive 'noanchor\CMakeLists.txt'
        Write-TFile $p "project(x)`n"
        { Add-DOA4CMakeIncludeFix -Path $p } | Should Throw 'expected one line'
        [IO.File]::ReadAllText($p) | Should Be "project(x)`n"
    }

    Context 'removal before an update (real git, autocrlf)' {
        $o = New-TOrigins -Base (Join-Path $TestDrive 'rm')
        $clone = Join-Path $TestDrive ('rm\' + $script:Odd)
        Invoke-TGit $TestDrive @('clone', '-q', $o.Game, $clone) | Out-Null
        $cm = Join-Path $clone 'CMakeLists.txt'
        $real = New-DOA4CommandTable

        It 'removes exactly the inserted block: git status is clean afterwards and the bytes equal the checkout' {
            $pristine = Get-TBytesHash $cm
            Add-DOA4CMakeIncludeFix -Path $cm | Out-Null
            (Get-TStatus $clone) -join '|' | Should Be ' M CMakeLists.txt'
            Remove-DOA4CMakeIncludeFix -Cmd $real -RepoPath $clone | Should Be 'removed'
            @(Get-TStatus $clone).Count | Should Be 0
            Get-TBytesHash $cm | Should Be $pristine
        }
        It 'returns absent and changes nothing when the block is not there' {
            $before = Get-TBytesHash $cm
            Remove-DOA4CMakeIncludeFix -Cmd $real -RepoPath $clone | Should Be 'absent'
            Get-TBytesHash $cm | Should Be $before
        }
        It 'refuses when the file has another change besides the block, and leaves every byte' {
            Add-DOA4CMakeIncludeFix -Path $cm | Out-Null
            [IO.File]::AppendAllText($cm, "# the user's own line`r`n")
            $before = Get-TBytesHash $cm
            Remove-DOA4CMakeIncludeFix -Cmd $real -RepoPath $clone | Should Be 'refused'
            Get-TBytesHash $cm | Should Be $before
        }
        It 'refuses when the block itself was edited' {
            Invoke-TGit $clone @('checkout', '--', 'CMakeLists.txt') | Out-Null
            Add-DOA4CMakeIncludeFix -Path $cm | Out-Null
            $t = [IO.File]::ReadAllText($cm).Replace('if(TARGET imgui)', 'if(TARGET imgui_other)')
            [IO.File]::WriteAllText($cm, $t)
            $before = Get-TBytesHash $cm
            Remove-DOA4CMakeIncludeFix -Cmd $real -RepoPath $clone | Should Be 'refused'
            Get-TBytesHash $cm | Should Be $before
        }
    }
}

Describe 'Repair-DOA4SymlinkStubs (real git, core.symlinks false)' {
    $o = New-TOrigins -Base (Join-Path $TestDrive 'st')
    $sdk = Join-Path $TestDrive ('st\' + $script:Odd + '\rexglue-sdk')
    Invoke-TGit $TestDrive @('clone', '-q', '-c', 'core.symlinks=false', $o.Sdk, $sdk) | Out-Null
    Invoke-TGit $sdk @('-c', 'core.symlinks=false', 'submodule', 'update', '--init', '--recursive') | Out-Null
    $lib = Join-Path $sdk 'thirdparty\lib'
    $real = New-DOA4CommandTable

    It 'precondition: the checkout holds stubs (small files holding the link target)' {
        [IO.File]::ReadAllText((Join-Path $lib 'link\a.c')) | Should Be '../real/a.c'
        (Get-Item -LiteralPath (Join-Path $lib 'linkdir')).PSIsContainer | Should Be $false
    }
    It 'replaces fresh stubs with copies (files and a directory)' {
        $r = Repair-DOA4SymlinkStubs -Cmd $real -RepoPath $sdk
        $r.Fixed | Should Be 3
        [IO.File]::ReadAllText((Join-Path $lib 'link\a.c')) | Should Be "int a;`r`n"
        [IO.File]::ReadAllText((Join-Path $lib 'linkdir\x.h')) | Should Be "#define X 1`r`n"
        # git sees the file copies as modified symlinks and the folder copy as a deleted
        # symlink (observed on git 2.51 for Windows), nothing else.
        @(Get-TStatus $lib | Sort-Object) -join '|' | Should Be ' D linkdir| M link/a.c| M link/b.h'
    }
    It 'is safe to run again: already fixed, nothing replaced' {
        $r = Repair-DOA4SymlinkStubs -Cmd $real -RepoPath $sdk
        $r.Fixed | Should Be 0
        $r.Skipped | Should Be 3
    }
    It 'fixes only the stubs that are back (mixed)' {
        Invoke-TGit $lib @('checkout', '--', 'link/b.h') | Out-Null
        [IO.File]::ReadAllText((Join-Path $lib 'link\b.h')) | Should Be '../real/b.h'
        $r = Repair-DOA4SymlinkStubs -Cmd $real -RepoPath $sdk
        $r.Fixed | Should Be 1
        [IO.File]::ReadAllText((Join-Path $lib 'link\b.h')) | Should Be "#define B 1`r`n"
    }
    It 'skips a stub whose target does not exist' {
        $lone = Join-Path $TestDrive 'lone'
        [void](New-Item -ItemType Directory -Path $lone)
        Invoke-TGit $lone @('init', '-q') | Out-Null
        Add-TSymlinkEntry $lone 'dangling.h' '../nowhere/dangling.h'
        Invoke-TGit $lone @('commit', '-q', '-m', 'x') | Out-Null
        Invoke-TGit $lone @('checkout', '--', 'dangling.h') | Out-Null
        $r = Repair-DOA4SymlinkStubs -Cmd $real -RepoPath $lone
        $r.Fixed | Should Be 0
        [IO.File]::ReadAllText((Join-Path $lone 'dangling.h')) | Should Be '../nowhere/dangling.h'
    }
}

Describe 'Get-DOA4StubChanges (what an update may undo)' {
    $o = New-TOrigins -Base (Join-Path $TestDrive 'sc')
    $sdk = Join-Path $TestDrive 'sc\with space\rexglue-sdk'
    Invoke-TGit $TestDrive @('clone', '-q', $o.Sdk, $sdk) | Out-Null
    Invoke-TGit $sdk @('submodule', 'update', '--init', '--recursive') | Out-Null
    $real = New-DOA4CommandTable
    Repair-DOA4SymlinkStubs -Cmd $real -RepoPath $sdk | Out-Null

    It 'lists the three stub copies as the launcher''s and nothing as foreign' {
        $c = Get-DOA4StubChanges -Cmd $real -RepoPath $sdk
        $c.Foreign.Count | Should Be 0
        @($c.Stubs | ForEach-Object { $_.Shown } | Sort-Object) -join ',' | Should Be 'thirdparty/lib/link/a.c,thirdparty/lib/link/b.h,thirdparty/lib/linkdir'
    }
    It 'lists a copied folder whose files were edited as foreign' {
        $x = Join-Path $sdk 'thirdparty\lib\linkdir\x.h'
        $keep = [IO.File]::ReadAllBytes($x)
        [IO.File]::AppendAllText($x, "#define MINE 1`r`n")
        $c = Get-DOA4StubChanges -Cmd $real -RepoPath $sdk
        $c.Foreign -join ',' | Should Be 'thirdparty/lib/linkdir (copied folder differs from its link target)'
        [IO.File]::WriteAllBytes($x, $keep)
        (Get-DOA4StubChanges -Cmd $real -RepoPath $sdk).Foreign.Count | Should Be 0
    }
    It 'lists an edited ordinary file in the submodule as foreign' {
        [IO.File]::AppendAllText((Join-Path $sdk 'thirdparty\lib\real\a.c'), "int mine;`r`n")
        $c = Get-DOA4StubChanges -Cmd $real -RepoPath $sdk
        # real/a.c is also link/a.c's target, so the (unchanged) copy no longer equals its
        # target and is listed too (review I5: a copy is the launcher's only while equal).
        @($c.Foreign | Sort-Object) -join ',' | Should Be 'thirdparty/lib/link/a.c (copied file differs from its link target),thirdparty/lib/real/a.c'
    }
    It 'lists an edited file in the SDK itself as foreign' {
        [IO.File]::AppendAllText((Join-Path $sdk 'README.md'), "mine`r`n")
        $c = Get-DOA4StubChanges -Cmd $real -RepoPath $sdk
        @($c.Foreign | Sort-Object) -join ',' | Should Be 'README.md,thirdparty/lib/link/a.c (copied file differs from its link target),thirdparty/lib/real/a.c'
    }
}
