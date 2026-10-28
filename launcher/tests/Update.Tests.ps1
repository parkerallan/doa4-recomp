. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Install and Update against real git repositories in TestDrive, with faked tools and a
# faked build (it only creates the exe). Each Describe gets its own DOA4 folder
# (New-TInstalled, TestHelpers.ps1).

function New-TUpdateCtx { param($T) New-TCtx $T 'update' }

Describe 'Install end to end (real git, faked tools and build)' {
    $t = New-TInstalled -Base (Join-Path $TestDrive 'i')
    $game = Join-Path $t.Root 'doa4-recomp'
    $sdk = Join-Path $t.Root 'rexglue-sdk'

    It 'succeeds through every step' {
        $t.Install.Message | Should Be 'Installed.'
        $t.Install.Ok | Should Be $true
    }
    It 'cloned both repositories, the submodule, and made userdata' {
        Test-Path -LiteralPath (Join-Path $game 'src\main.cpp') | Should Be $true
        Test-Path -LiteralPath (Join-Path $sdk 'thirdparty\lib\real\a.c') | Should Be $true
        Test-Path -LiteralPath (Join-Path $t.Root 'userdata') | Should Be $true
    }
    It 'applied both fixes' {
        [IO.File]::ReadAllText((Join-Path $game 'CMakeLists.txt')).Contains('rexglue-sdk/include') | Should Be $true
        [IO.File]::ReadAllText((Join-Path $sdk 'thirdparty\lib\link\a.c')) | Should Be "int a;`r`n"
    }
    It 'built once, configuring through environment variables only (no path in the batch file)' {
        $t.Build.State.Calls.Count | Should Be 1
        $call = $t.Build.State.Calls[0]
        $call.Env.DOA4_SDK_FWD | Should Be ($sdk.Replace('\', '/'))
        $call.Env.DOA4_VSDEVCMD | Should Be (Join-Path $t.Tools.VsPath 'Common7\Tools\VsDevCmd.bat')
        $call.Batch.Contains($t.Root) | Should Be $false
        $call.Batch | Should Match 'cmake --preset win-amd64-release "-DREXSDK_DIR=%DOA4_SDK_FWD%" "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3"'
        $call.Batch | Should Match 'cmake --build --preset win-amd64-release'
    }
    It 'recorded the build, so the status is built' {
        Get-DOA4BuildStatus -Ctx $t.Ctx | Should Be 'built'
    }
    It 'made the shortcut in the given folder only, pointing at the launcher' {
        $lnk = Join-Path $t.Shortcuts 'Dead or Alive 4.lnk'
        Test-Path -LiteralPath $lnk | Should Be $true
        $s = Get-DOA4Shortcut -Path $lnk
        $s.TargetPath | Should Match 'powershell\.exe$'
        $s.WorkingDirectory | Should BeExactly (Join-Path $t.Root 'launcher')
        $s.Arguments | Should BeExactly ('-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "{0}"' -f (Join-Path $t.Root 'launcher\DOA4Launcher.ps1'))
    }
    It 'resumes on a second Install: reuses both clones, does not rebuild configure' {
        $r = Invoke-DOA4Install -Ctx (New-DOA4Context -Root $t.Root -Config $t.Config -Cmd (New-FakeTable $t.Tools -Cmd $t.Build -RealGit))
        $r.Ok | Should Be $true
        $log = [IO.File]::ReadAllText($r.LogPath)
        $log | Should Match 'doa4-recomp: reused'
        $log | Should Match 'rexglue-sdk: reused'
        $log | Should Match 'CMake include fix: present'
        $log | Should Match '\[Disk space\] done: already built'
    }
}

Describe 'New-DOA4Shortcut (judged by the shell that opens it)' {
    It 'a shortcut to a script in a non-ANSI folder really starts that script' {
        $dir = Join-Path $TestDrive ('lnk ' + [char]0x65E5 + [char]0x672C + '\launcher')
        [void](New-Item -ItemType Directory -Force -Path $dir)
        $probe = Join-Path $dir 'DOA4Launcher.ps1'
        Set-Content -LiteralPath $probe -Value "Set-Content -LiteralPath (Join-Path `$PSScriptRoot 'ran.txt') -Value 'ran'" -Encoding ASCII
        $folder = Join-Path $TestDrive 'lnkout'
        [void](New-Item -ItemType Directory -Force -Path $folder)
        New-DOA4Shortcut -Folder $folder -LauncherScript $probe | Should Be 'created'
        Start-Process -FilePath (Join-Path $folder 'Dead or Alive 4.lnk')
        $marker = Join-Path $dir 'ran.txt'
        $deadline = (Get-Date).AddSeconds(30)
        while (-not (Test-Path -LiteralPath $marker) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
        Test-Path -LiteralPath $marker | Should Be $true
    }
    It 'keeps a different shortcut that has the same name' {
        $folder = Join-Path $TestDrive 'lnkkeep'
        [void](New-Item -ItemType Directory -Force -Path $folder)
        New-DOA4Shortcut -Folder $folder -LauncherScript 'C:\elsewhere\other.ps1' | Out-Null
        $before = Get-TBytesHash (Join-Path $folder 'Dead or Alive 4.lnk')
        New-DOA4Shortcut -Folder $folder -LauncherScript 'C:\DOA4\launcher\DOA4Launcher.ps1' | Should Be 'kept (a different shortcut has that name)'
        Get-TBytesHash (Join-Path $folder 'Dead or Alive 4.lnk') | Should Be $before
    }
}

Describe 'Update with nothing new' {
    $t = New-TInstalled -Base (Join-Path $TestDrive 'u1')
    $game = Join-Path $t.Root 'doa4-recomp'
    $sdk = Join-Path $t.Root 'rexglue-sdk'
    $headGame = @(Invoke-TGit $game @('rev-parse', 'HEAD'))[0]
    # Touch times of the fixed files: with nothing new they must not be rewritten (a rewrite
    # makes CMake re-run and objects rebuild, seen in the end-to-end run).
    $cm = Join-Path $game 'CMakeLists.txt'
    $stub = Join-Path $sdk 'thirdparty\lib\link\a.c'
    $old = (Get-Date).AddHours(-1)
    (Get-Item -LiteralPath $cm).LastWriteTime = $old
    (Get-Item -LiteralPath $stub).LastWriteTime = $old
    $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)

    It 'succeeds' { $r.Message | Should Be 'Updated and rebuilt.' }
    It 'leaves git status showing only the two fixes' {
        (Get-TStatus $game) -join '|' | Should Be ' M CMakeLists.txt'
        (Get-TStatus $sdk) -join '|' | Should Be ' M thirdparty/lib'
        @(Get-TStatus (Join-Path $sdk 'thirdparty\lib') | Sort-Object) -join '|' | Should Be ' D linkdir| M link/a.c| M link/b.h'
    }
    It 'fetched, found nothing new, and left both fixes untouched (no rewrite, same times)' {
        $log = [IO.File]::ReadAllText($r.LogPath)
        $log | Should Match 'nothing new: doa4-recomp and rexglue-sdk are at their configured versions'
        $log | Should Not Match 'build fix removed for the update'
        (Get-Item -LiteralPath $cm).LastWriteTime | Should Be $old
        (Get-Item -LiteralPath $stub).LastWriteTime | Should Be $old
    }
    It 'did not move HEAD, and built again' {
        @(Invoke-TGit $game @('rev-parse', 'HEAD'))[0] | Should Be $headGame
        $t.Build.State.Calls.Count | Should Be 2
    }
}

Describe 'Update with a new upstream commit' {
    $t = New-TInstalled -Base (Join-Path $TestDrive 'u2')
    $game = Join-Path $t.Root 'doa4-recomp'
    Write-TFile (Join-Path $t.Origins.Game 'src\new.cpp') "// new`n"
    Invoke-TGit $t.Origins.Game @('add', '-A') | Out-Null
    Invoke-TGit $t.Origins.Game @('commit', '-q', '-m', 'new upstream') | Out-Null
    $upstream = @(Invoke-TGit $t.Origins.Game @('rev-parse', 'HEAD'))[0]
    Get-DOA4BuildStatus -Ctx $t.Ctx | Out-Null
    $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)

    It 'fast-forwards to the new commit and rebuilds' {
        $r.Ok | Should Be $true
        @(Invoke-TGit $game @('rev-parse', 'HEAD'))[0] | Should Be $upstream
        Test-Path -LiteralPath (Join-Path $game 'src\new.cpp') | Should Be $true
    }
    It 'has the CMake fix again' {
        (Get-TStatus $game) -join '|' | Should Be ' M CMakeLists.txt'
    }
    It 'took the fixes out for the pull and put them back' {
        $log = [IO.File]::ReadAllText($r.LogPath)
        $log | Should Match 'build fix removed for the update'
        $log | Should Match '3 symlink copies restored to stubs'
        $log | Should Match 'fixes re-applied: symlink stubs replaced: 3; CMake include fix: added'
        $log | Should Match 'rexglue-sdk: already up to date'
    }
}

Describe 'Update refuses foreign local changes' {
    It 'in the game repository: lists the file, changes nothing, does not fetch or build' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'r1')
        $game = Join-Path $t.Root 'doa4-recomp'
        $main = Join-Path $game 'src\main.cpp'
        [IO.File]::AppendAllText($main, "// mine`r`n")
        $mainBefore = Get-TBytesHash $main
        $cmBefore = Get-TBytesHash (Join-Path $game 'CMakeLists.txt')
        Write-TFile (Join-Path $t.Origins.Game 'src\new.cpp') "// new`n"
        Invoke-TGit $t.Origins.Game @('add', '-A') | Out-Null
        Invoke-TGit $t.Origins.Game @('commit', '-q', '-m', 'new upstream') | Out-Null
        $before = @(Invoke-TGit $game @('rev-parse', 'origin/main'))[0]
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Ok | Should Be $false
        $r.Step | Should Be 'Local changes'
        $r.Message | Should Match 'doa4-recomp/src/main\.cpp'
        Get-TBytesHash $main | Should Be $mainBefore
        Get-TBytesHash (Join-Path $game 'CMakeLists.txt') | Should Be $cmBefore
        @(Invoke-TGit $game @('rev-parse', 'origin/main'))[0] | Should Be $before
        $t.Build.State.Calls.Count | Should Be 1
    }
    It 'in CMakeLists.txt beside the fix: refuses, bytes unchanged' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'r2')
        $cm = Join-Path $t.Root 'doa4-recomp\CMakeLists.txt'
        [IO.File]::AppendAllText($cm, "# mine`r`n")
        $before = Get-TBytesHash $cm
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Ok | Should Be $false
        $r.Message | Should Match "CMakeLists\.txt \(changes besides the launcher's build fix\)"
        Get-TBytesHash $cm | Should Be $before
    }
    It 'in an SDK submodule: refuses, the stub copies stay copies' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'r3')
        $lib = Join-Path $t.Root 'rexglue-sdk\thirdparty\lib'
        [IO.File]::AppendAllText((Join-Path $lib 'real\a.c'), "int mine;`r`n")
        $cm = Join-Path $t.Root 'doa4-recomp\CMakeLists.txt'
        $cmBefore = Get-TBytesHash $cm
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Ok | Should Be $false
        $r.Message | Should Match 'rexglue-sdk/thirdparty/lib/real/a\.c'
        [IO.File]::ReadAllText((Join-Path $lib 'link\b.h')) | Should Be "#define B 1`r`n"
        # nothing written in the game repository either (its fix was never taken out)
        Get-TBytesHash $cm | Should Be $cmBefore
    }
    It 'reports foreign changes in both repositories at once' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'r5')
        [IO.File]::AppendAllText((Join-Path $t.Root 'doa4-recomp\src\main.cpp'), "// mine`r`n")
        [IO.File]::AppendAllText((Join-Path $t.Root 'rexglue-sdk\README.md'), "mine`r`n")
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Message | Should Match 'doa4-recomp/src/main\.cpp, rexglue-sdk/README\.md$'
    }
    It 'with a staged change: refuses' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'r4')
        $game = Join-Path $t.Root 'doa4-recomp'
        Write-TFile (Join-Path $game 'src\staged.cpp') "// staged`n"
        Invoke-TGit $game @('add', 'src/staged.cpp') | Out-Null
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Ok | Should Be $false
        $r.Message | Should Match 'doa4-recomp/src/staged\.cpp'
    }
}

Describe 'Update when the fetch fails' {
    It 'reports the failure and leaves both fixes as they were' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'f1')
        $cm = Join-Path $t.Root 'doa4-recomp\CMakeLists.txt'
        $cmBefore = Get-TBytesHash $cm
        Rename-Item -LiteralPath $t.Origins.Game -NewName 'origin-game-gone'
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Ok | Should Be $false
        $r.Step | Should Be 'Update'
        $r.Message | Should Match 'git -C .* fetch origin failed'
        Get-TBytesHash $cm | Should Be $cmBefore
        [IO.File]::ReadAllText((Join-Path $t.Root 'rexglue-sdk\thirdparty\lib\link\a.c')) | Should Be "int a;`r`n"
        $t.Build.State.Calls.Count | Should Be 1
    }
}

Describe 'Update when the fast-forward fails (local commit on the branch)' {
    It 'reports the failure and still puts both fixes back' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'f2')
        $game = Join-Path $t.Root 'doa4-recomp'
        Write-TFile (Join-Path $game 'src\local.cpp') "// local`n"
        Invoke-TGit $game @('add', 'src/local.cpp') | Out-Null
        Invoke-TGit $game @('commit', '-q', '-m', 'local commit') | Out-Null
        Write-TFile (Join-Path $t.Origins.Game 'src\new.cpp') "// new`n"
        Invoke-TGit $t.Origins.Game @('add', '-A') | Out-Null
        Invoke-TGit $t.Origins.Game @('commit', '-q', '-m', 'new upstream') | Out-Null
        $r = Invoke-DOA4Update -Ctx (New-TUpdateCtx $t)
        $r.Ok | Should Be $false
        $r.Step | Should Be 'Update'
        $r.Message | Should Match 'merge --ff-only origin/main failed'
        [IO.File]::ReadAllText((Join-Path $game 'CMakeLists.txt')).Contains('rexglue-sdk/include') | Should Be $true
        [IO.File]::ReadAllText((Join-Path $t.Root 'rexglue-sdk\thirdparty\lib\link\a.c')) | Should Be "int a;`r`n"
        $t.Build.State.Calls.Count | Should Be 1
    }
}

Describe 'Update before Install' {
    It 'says to run Install' {
        $cfg = New-TestConfig
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'nv')
        $ctx = New-DOA4Context -Root (Join-Path $TestDrive 'none\DOA4') -Config $cfg -Cmd (New-FakeTable $tools)
        $r = Invoke-DOA4Update -Ctx $ctx
        $r.Message | Should Be 'Not installed yet: run Install first.'
    }
}

Disable-TFileProtocol
