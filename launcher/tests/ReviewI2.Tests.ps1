. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'I2: a clone interrupted during checkout is not reused' {
    It 'P5: game repo with HEAD but no checkout (killed during checkout) -> unfinished clone' {
        Enable-TFileProtocol
        $base = Join-Path $TestDrive 'p5'
        $o = New-TOrigins -Base $base
        $root = Join-Path $base 'DOA4'
        [void](New-Item -ItemType Directory -Force -Path $root)
        Invoke-TGit $root @('clone', '-q', '--no-checkout', '--branch', 'main', '--', $o.Game, (Join-Path $root 'doa4-recomp')) | Out-Null
        $cfg = New-TestConfig
        $cfg.GameRepoUrl = $o.Game; $cfg.GameRef = 'main'
        $cfg.SdkRepoUrl = $o.Sdk; $cfg.SdkRef = (@(Invoke-TGit $o.Sdk @('rev-parse', 'HEAD'))[0]).Substring(0, 7)
        $tools = New-FakeTools -VsRoot (Join-Path $base 'vs')
        $build = New-FakeCmd
        $r = Invoke-DOA4Install -Ctx (New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools -Cmd $build -RealGit))
        $r.Ok | Should Be $false
        $r.Step | Should Be 'Code'
        $r.Message | Should Match 'unfinished clone'
        $build.State.Calls.Count | Should Be 0
    }
    It 'P6: SDK (pinned commit) with HEAD but no checkout -> not built on' {
        Enable-TFileProtocol
        $base = Join-Path $TestDrive 'p6'
        $o = New-TOrigins -Base $base
        $pinned = @(Invoke-TGit $o.Sdk @('rev-parse', 'HEAD'))[0]
        Write-TFile (Join-Path $o.Sdk 'later.txt') "later`n"
        Invoke-TGit $o.Sdk @('add', '-A') | Out-Null
        Invoke-TGit $o.Sdk @('commit', '-q', '-m', 'later') | Out-Null
        $root = Join-Path $base 'DOA4'
        [void](New-Item -ItemType Directory -Force -Path $root)
        Invoke-TGit $root @('clone', '-q', '--no-checkout', '--', $o.Sdk, (Join-Path $root 'rexglue-sdk')) | Out-Null
        $cfg = New-TestConfig
        $cfg.GameRepoUrl = $o.Game; $cfg.GameRef = 'main'
        $cfg.SdkRepoUrl = $o.Sdk; $cfg.SdkRef = $pinned.Substring(0, 7)
        $tools = New-FakeTools -VsRoot (Join-Path $base 'vs')
        $build = New-FakeCmd
        $r = Invoke-DOA4Install -Ctx (New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools -Cmd $build -RealGit))
        $r.Message | Should Match 'rexglue-sdk.*unfinished clone'
        $build.State.Calls.Count | Should Be 0
    }
    It 'P4: "Clone succeeded, but checkout failed" -> the re-run says unfinished clone' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'p4') -BeforeInstall {
            param($o)
            # A path Windows cannot create: git for Windows refuses it at checkout.
            $tmp = Join-Path $o.Game 'blob.tmp'
            [IO.File]::WriteAllText($tmp, "x`n")
            $sha = @(Invoke-TGit $o.Game @('hash-object', '-w', '--', $tmp) | Where-Object { $_ -match '^[0-9a-f]{40}$' })[0].Trim()
            Remove-Item -LiteralPath $tmp
            # Staging it needs core.protectNTFS off in the origin; the clone keeps the default,
            # so its checkout fails ("Clone succeeded, but checkout failed").
            Invoke-TGit $o.Game @('-c', 'core.protectNTFS=false', 'update-index', '--add', '--cacheinfo', "100644,$sha,src/bad:name.cpp") | Out-Null
            Invoke-TGit $o.Game @('commit', '-q', '-m', 'bad path') | Out-Null
        }
        $t.Install.Ok | Should Be $false
        Test-Path -LiteralPath (Join-Path $t.Root 'doa4-recomp\.git') | Should Be $true
        $r2 = Invoke-DOA4Install -Ctx (New-TCtx $t 'install')
        $r2.Ok | Should Be $false
        $r2.Message | Should Match 'unfinished clone'
    }
    It 'a finished clone with a hand edit is not reused either, and the file is named' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'i2e')
        [IO.File]::AppendAllText((Join-Path $t.Root 'doa4-recomp\src\main.cpp'), "// mine`r`n")
        $r2 = Invoke-DOA4Install -Ctx (New-TCtx $t 'install')
        $r2.Ok | Should Be $false
        $r2.Message | Should Match 'src/main\.cpp'
    }
}

Disable-TFileProtocol
