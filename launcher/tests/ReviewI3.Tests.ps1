. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'I3: Update follows the configured repository URLs, or refuses' {
    It 'P7: config.ps1 names another game repository -> refused, both URLs named, nothing fetched' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'p7')
        $fork = Join-Path $TestDrive 'p7\fork-game'
        Invoke-TGit $TestDrive @('clone', '-q', '--', $t.Origins.Game, $fork) | Out-Null
        Write-TFile (Join-Path $fork 'src\fork_only.cpp') "// fork`n"
        Invoke-TGit $fork @('add', '-A') | Out-Null
        Invoke-TGit $fork @('commit', '-q', '-m', 'fork commit') | Out-Null
        $forkHead = @(Invoke-TGit $fork @('rev-parse', 'HEAD'))[0]
        $game = Join-Path $t.Root 'doa4-recomp'
        $head = @(Invoke-TGit $game @('rev-parse', 'HEAD'))[0]
        $cfg2 = $t.Config.Clone(); $cfg2.GameRepoUrl = $fork
        $r = Invoke-DOA4Update -Ctx (New-DOA4Context -Root $t.Root -Config $cfg2 -Cmd (New-FakeTable $t.Tools -Cmd $t.Build -RealGit) -Action 'update')
        $r.Ok | Should Be $false
        $r.Message | Should Match ([regex]::Escape($fork))
        $r.Message | Should Match ([regex]::Escape($t.Origins.Game))
        $r.Message | Should Match 'new DOA4 folder'
        @(Invoke-TGit $game @('rev-parse', 'HEAD'))[0] | Should Be $head
        (Invoke-DOA4Process -FilePath 'git.exe' -Arguments @('-C', $game, 'cat-file', '-e', $forkHead)).ExitCode | Should Not Be 0
        $t.Build.State.Calls.Count | Should Be 1
    }
    It 'the same for the SDK URL' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'p7s')
        $cfg2 = $t.Config.Clone(); $cfg2.SdkRepoUrl = 'https://github.com/someone/rexglue-sdk-fork'
        $r = Invoke-DOA4Update -Ctx (New-DOA4Context -Root $t.Root -Config $cfg2 -Cmd (New-FakeTable $t.Tools -Cmd $t.Build -RealGit) -Action 'update')
        $r.Ok | Should Be $false
        $r.Message | Should Match 'rexglue-sdk-fork'
    }
    It 'the same URL written differently (.git, case, slashes) is not a change' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'p7n')
        $cfg2 = $t.Config.Clone(); $cfg2.GameRepoUrl = $t.Origins.Game.Replace('\', '/').ToUpperInvariant() + '.git'
        $r = Invoke-DOA4Update -Ctx (New-DOA4Context -Root $t.Root -Config $cfg2 -Cmd (New-FakeTable $t.Tools -Cmd $t.Build -RealGit) -Action 'update')
        $r.Ok | Should Be $true
    }
}

Disable-TFileProtocol
