. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

$script:Py = (Get-Command py.exe -ErrorAction SilentlyContinue)

# Parses a TOML file with Python's tomllib (an independent TOML 1.0 reader; the game's own
# reader, toml++, is exercised by the end-to-end run). Returns $null when it does not parse.
function Read-TToml {
    param([string]$Path)
    $r = Invoke-DOA4Process -FilePath 'py.exe' -Arguments @('-3', '-I', '-c',
        'import sys, json, tomllib; print(json.dumps(tomllib.load(open(sys.argv[1], "rb"))))', $Path)
    if ($r.ExitCode -ne 0) { return $null }
    return ($r.Output -join '') | ConvertFrom-Json
}

Describe 'Test-DOA4GameFolder' {
    $dir = Join-Path $TestDrive ('game files ' + [char]0x00E9)
    Write-TFile (Join-Path $dir 'default.xex') 'not really a xex'
    $sha = (Get-FileHash -LiteralPath (Join-Path $dir 'default.xex') -Algorithm SHA1).Hash.ToLowerInvariant()

    It 'accepts a folder whose default.xex has the expected SHA-1 (path with a space and e-acute)' {
        $r = Test-DOA4GameFolder -Path $dir -ExpectedSha1 $sha -CodePage 1252
        $r.Verdict | Should Be 'Ok'
        $r.Ok | Should Be $true
    }
    It 'refuses the same folder against the supported SHA-1, with the game''s wrong-version message' {
        $r = Test-DOA4GameFolder -Path $dir -CodePage 1252
        $r.Verdict | Should Be 'WrongVersion'
        $r.Message | Should BeExactly 'This is not the supported version. Dead or Alive 4 recomp needs Dead or Alive 4 (USA, Europe) (Rev 1): its default.xex has SHA-1 af8011945037b37c98fb8a78d5a4e844dd56c33f. The recompiled code matches that exact file; another version would crash.'
    }
    It 'refuses a folder without default.xex' {
        $empty = Join-Path $TestDrive 'nothing'
        [void](New-Item -ItemType Directory -Path $empty)
        (Test-DOA4GameFolder -Path $empty -CodePage 1252).Verdict | Should Be 'NoXex'
    }
    It 'refuses an unfinished extraction (.part file in a subfolder), even with the right hash' {
        $p = Join-Path $TestDrive 'partial'
        Write-TFile (Join-Path $p 'default.xex') 'not really a xex'
        Write-TFile (Join-Path $p 'movie\intro.wmv.part') 'x'
        (Test-DOA4GameFolder -Path $p -ExpectedSha1 $sha -CodePage 1252).Verdict | Should Be 'Incomplete'
    }
    It 'refuses a path outside the ANSI code page before reading anything' {
        $p = Join-Path $TestDrive ('cjk ' + [char]0x65E5)
        Write-TFile (Join-Path $p 'default.xex') 'not really a xex'
        (Test-DOA4GameFolder -Path $p -ExpectedSha1 $sha -CodePage 1252).Verdict | Should Be 'PathUnsupported'
        # A best-fit letter (a-macron would become plain a) is refused too, as the game does.
        (Test-DOA4PathFitsCodePage -Path ('C:\' + [char]0x0101) -CodePage 1252) | Should Be $false
        (Test-DOA4PathFitsCodePage -Path ('C:\' + [char]0x65E5) -CodePage 65001) | Should Be $true
    }
    # Opt-in: set DOA4_TEST_GAME_DIR to your extracted game folder (for example
    # C:\Games\DOA4, the folder holding default.xex) to run the real-folder checks.
    $realXex = $env:DOA4_TEST_GAME_DIR
    if ($realXex) {
        It 'accepts the real extracted game in DOA4_TEST_GAME_DIR (read only)' {
            Test-Path -LiteralPath (Join-Path $realXex 'default.xex') | Should Be $true
            (Test-DOA4GameFolder -Path $realXex).Verdict | Should Be 'Ok'
        }
    } else {
        It 'accepts the real extracted game (set DOA4_TEST_GAME_DIR to run)' -Skip { }
    }
}

Describe 'Set-DOA4TomlString (checked by an independent TOML reader)' {
    if (-not $script:Py) { It 'needs Python for the reader' { $false | Should Be $true } ; return }
    $value = 'C:/Users/J' + [char]0x00F6 + 'rg/games ' + [char]0x65E5 + '/DOA4'

    It 'creates a missing file' {
        $p = Join-Path $TestDrive 'new.toml'
        Set-DOA4TomlString -Path $p -Key 'doa4_game_files' -Value $value | Should Be 'created'
        (Read-TToml $p).doa4_game_files | Should BeExactly $value
    }
    It 'replaces the key in place and keeps every other line, CRLF and BOM' {
        $p = Join-Path $TestDrive 'crlf.toml'
        $orig = "# settings`r`nfullscreen = true`r`ndoa4_game_files = `"C:/old`"   `r`nresolution_scale = 4`r`n"
        [IO.File]::WriteAllBytes($p, [byte[]](0xEF, 0xBB, 0xBF) + [Text.Encoding]::UTF8.GetBytes($orig))
        Set-DOA4TomlString -Path $p -Key 'doa4_game_files' -Value $value | Should Be 'updated'
        $bytes = [IO.File]::ReadAllBytes($p)
        $bytes[0] | Should Be 0xEF
        $text = [Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
        $text | Should BeExactly ("# settings`r`nfullscreen = true`r`ndoa4_game_files = `"$value`"`r`nresolution_scale = 4`r`n")
        # tomllib refuses a BOM (toml++, the game's reader, accepts it): parse the text after it.
        $nobom = Join-Path $TestDrive 'crlf-nobom.toml'
        [IO.File]::WriteAllBytes($nobom, [byte[]]($bytes[3..($bytes.Length - 1)]))
        $t = Read-TToml $nobom
        $t.doa4_game_files | Should BeExactly $value
        $t.resolution_scale | Should Be 4
    }
    It 'adds a missing key before the first table, never inside it' {
        $p = Join-Path $TestDrive 'table.toml'
        Write-TFile $p "fullscreen = true`n[GPU]`nvsync = false`n"
        Set-DOA4TomlString -Path $p -Key 'doa4_game_files' -Value $value | Out-Null
        $t = Read-TToml $p
        $t.doa4_game_files | Should BeExactly $value
        $t.GPU.PSObject.Properties['doa4_game_files'] | Should BeNullOrEmpty
    }
    It 'appends to a file without a final newline' {
        $p = Join-Path $TestDrive 'nonl.toml'
        Write-TFile $p 'fullscreen = true'
        Set-DOA4TomlString -Path $p -Key 'doa4_game_files' -Value 'C:/x' | Out-Null
        (Read-TToml $p).fullscreen | Should Be $true
        (Read-TToml $p).doa4_game_files | Should Be 'C:/x'
    }
    It 'escapes a quote and a backslash so the reader gets the value back' {
        $p = Join-Path $TestDrive 'esc.toml'
        Set-DOA4TomlString -Path $p -Key 'k' -Value 'a"b\c' | Out-Null
        (Read-TToml $p).k | Should BeExactly 'a"b\c'
    }
    foreach ($case in @(
            @{ Name = 'a quoted key'; Text = "`"doa4_game_files`" = `"C:/old`"`n" },
            @{ Name = 'a multi-line string'; Text = "notes = `"`"`"`nline`n`"`"`"`n" },
            @{ Name = 'a multi-line array'; Text = "list = [`n  1,`n]`n" })) {
        It "refuses a file with $($case.Name) and leaves it byte for byte" {
            $p = Join-Path $TestDrive ('refuse-' + $case.Name.Replace(' ', '-') + '.toml')
            Write-TFile $p $case.Text
            $before = Get-TBytesHash $p
            { Set-DOA4TomlString -Path $p -Key 'doa4_game_files' -Value 'C:/x' } | Should Throw 'dead_or_alive_4.toml has'
            Get-TBytesHash $p | Should Be $before
        }
    }
    It 'Get-DOA4TomlString reads back what it wrote, and $null for a missing key' {
        $p = Join-Path $TestDrive 'readback.toml'
        Write-TFile $p "doa4_photo_dir = `"D:/Photos \u00e9`"`n"
        Get-DOA4TomlString -Path $p -Key 'doa4_photo_dir' | Should BeExactly ('D:/Photos ' + [char]0x00E9)
        $null -eq (Get-DOA4TomlString -Path $p -Key 'nope') | Should Be $true
    }
}

Describe 'Play and Change game files' {
    $cfg = New-TestConfig
    $xexDir = Join-Path $TestDrive 'xex dir'
    Write-TFile (Join-Path $xexDir 'default.xex') 'not really a xex'

    function New-TGameCtx {
        param([string]$Name, [switch]$WithSetup)
        $root = Join-Path $TestDrive $Name
        $ctx = New-DOA4Context -Root $root -Config $cfg -Cmd @{}
        Write-TFile (Join-Path $ctx.GamePath 'CMakeLists.txt') 'x'
        if ($WithSetup) { Write-TFile (Join-Path $ctx.GamePath 'src\doa4_setup_dialog.cpp') '// setup' }
        [void](New-Item -ItemType Directory -Force -Path $ctx.BuildDir)
        return $ctx
    }

    It 'a build with the first-run setup gets no game path' {
        $ctx = New-TGameCtx 'p1' -WithSetup
        $a = Get-DOA4PlayArguments -Ctx $ctx -GameFiles 'C:\ignored'
        $a -join '|' | Should BeExactly ('--user_data_root=' + (Join-Path $ctx.Root 'userdata'))
    }
    It 'an upstream build gets the launcher setting as --game_data_root' {
        $ctx = New-TGameCtx 'p2'
        Save-DOA4Settings -Path $ctx.SettingsPath -Settings @{ gameFiles = 'D:\My Games\DOA4'; lastBuild = $null }
        $a = Get-DOA4PlayArguments -Ctx $ctx
        $a -join '|' | Should BeExactly ('--user_data_root=' + (Join-Path $ctx.Root 'userdata') + '|--game_data_root=D:\My Games\DOA4')
    }
    It 'starts the game through the shell, so it inherits none of the launcher''s console or pipe handles' {
        # With UseShellExecute=false a caller capturing the launcher's output waited until the
        # game exited (seen in the end-to-end Play run).
        $ctx = New-TGameCtx 'p4' -WithSetup
        $psi = Get-DOA4GameStartInfo -Ctx $ctx
        $psi.UseShellExecute | Should Be $true
        $psi.WorkingDirectory | Should Be $ctx.BuildDir
        $psi.FileName | Should Be $ctx.ExePath
        $psi.Arguments | Should BeExactly (ConvertTo-DOA4ArgumentString @('--user_data_root=' + (Join-Path $ctx.Root 'userdata')))
    }
    It 'an upstream build without the setting asks for it' {
        $ctx = New-TGameCtx 'p3'
        { Get-DOA4PlayArguments -Ctx $ctx } | Should Throw 'NEED_GAME_FILES'
    }
    It 'Change game files on an upstream build stores the launcher setting' {
        $ctx = New-TGameCtx 'c1'
        # The real check refuses this fixture (wrong hash), so nothing is written:
        { Set-DOA4GameFiles -Ctx $ctx -Folder $xexDir } | Should Throw 'not the supported version'
        Test-Path -LiteralPath $ctx.SettingsPath | Should Be $false
    }
    # Opt-in, as above: DOA4_TEST_GAME_DIR is a full path with backslashes, such as C:\Games\DOA4.
    $realXex = $env:DOA4_TEST_GAME_DIR
    if (-not $realXex) {
        It 'Change game files (real game folder; set DOA4_TEST_GAME_DIR to run)' -Skip { }
    } else {
        It 'Change game files (real game folder, read only): upstream -> settings.json' {
            $ctx = New-TGameCtx 'c2'
            Set-DOA4GameFiles -Ctx $ctx -Folder $realXex | Should Be 'settings'
            (Get-DOA4Settings $ctx.SettingsPath).gameFiles | Should Be $realXex
            Test-Path -LiteralPath $ctx.TomlPath | Should Be $false
        }
        It 'Change game files (real game folder, read only): first-run-setup build -> toml beside the exe, forward slashes' {
            $ctx = New-TGameCtx 'c3' -WithSetup
            Write-TFile $ctx.TomlPath "fullscreen = true`n"
            Set-DOA4GameFiles -Ctx $ctx -Folder $realXex | Should Be 'toml'
            # The toml holds the same folder in forward-slash form (no backslash survives).
            $saved = (Read-TToml $ctx.TomlPath).doa4_game_files
            $saved | Should BeExactly ($realXex.Replace('\', '/'))
            $saved.Contains('\') | Should Be $false
            Test-Path -LiteralPath $ctx.SettingsPath | Should Be $false
        }
    }
}

Describe 'Invoke-DOA4Build' {
    $cfg = New-TestConfig
    It 'a failed build throws with the log path and records nothing' {
        $ctx = New-DOA4Context -Root (Join-Path $TestDrive 'b1') -Config $cfg -Cmd @{ cmd = (New-FakeCmd -Exit 1).Invoke; git = { throw 'no git expected' } }
        { Invoke-DOA4Build -Ctx $ctx -VsDevCmd 'C:\vs\VsDevCmd.bat' } | Should Throw 'The build failed (exit 1)'
        Test-Path -LiteralPath $ctx.SettingsPath | Should Be $false
    }
    It 'exit 0 without the exe is a failure, not a success' {
        $ctx = New-DOA4Context -Root (Join-Path $TestDrive 'b2') -Config $cfg -Cmd @{ cmd = (New-FakeCmd -Exit 0).Invoke; git = { throw 'no git expected' } }
        { Invoke-DOA4Build -Ctx $ctx -VsDevCmd 'C:\vs\VsDevCmd.bat' } | Should Throw 'is missing'
        Test-Path -LiteralPath $ctx.SettingsPath | Should Be $false
    }
    It 'configures when CMakeCache.txt OR build.ninja is missing (an interrupted configure leaves the cache)' {
        $s = Get-DOA4BuildScript
        $s | Should Match 'if not exist "out\\build\\win-amd64-release\\CMakeCache.txt" set "DOA4_CONFIGURE=1"'
        $s | Should Match 'if not exist "out\\build\\win-amd64-release\\build.ninja" set "DOA4_CONFIGURE=1"'
        ([regex]::Matches($s, '[^\x00-\x7F]')).Count | Should Be 0
    }
}

Describe 'Get-DOA4BuildStatus' {
    $cfg = New-TestConfig
    $head = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
    $git = New-FakeCommand 'git' @(@{ Match = 'rev-parse HEAD$'; Output = @($head) })
    $ctx = New-DOA4Context -Root (Join-Path $TestDrive 's') -Config $cfg -Cmd @{ git = $git.Invoke }
    [void](New-Item -ItemType Directory -Force -Path $ctx.GamePath, $ctx.SdkPath)
    It 'not built without the exe' { Get-DOA4BuildStatus -Ctx $ctx | Should Be 'not built' }
    It 'out of date when a HEAD differs from the last build' {
        Write-TFile $ctx.ExePath 'exe'
        Save-DOA4Settings -Path $ctx.SettingsPath -Settings @{ gameFiles = ''; lastBuild = @{ game = $head; sdk = 'bbbb'; time = 'x' } }
        Get-DOA4BuildStatus -Ctx $ctx | Should Be 'out of date'
    }
    It 'built when both HEADs match' {
        Save-DOA4Settings -Path $ctx.SettingsPath -Settings @{ gameFiles = ''; lastBuild = @{ game = $head; sdk = $head; time = 'x' } }
        Get-DOA4BuildStatus -Ctx $ctx | Should Be 'built'
    }
}

Describe 'ConvertTo-DOA4ArgumentString (judged by a real argv parser)' {
    if (-not $script:Py) { return }
    It 'round-trips spaces, quotes, trailing backslashes and non-ANSI letters' {
        $in = @('plain', 'with space', 'quote"inside', 'C:\trailing dir\', 'C:\nospace\', '', ('x' + [char]0x65E5 + 'y'), '--override "--add a --wait"')
        $r = Invoke-DOA4Process -FilePath 'py.exe' -Arguments (@('-3', '-I', '-c', 'import sys, json; print(json.dumps(sys.argv[1:]))') + $in)
        $out = ($r.Output -join '') | ConvertFrom-Json
        $out.Count | Should Be $in.Count
        for ($i = 0; $i -lt $in.Count; $i++) { $out[$i] | Should BeExactly $in[$i] }
    }
}
