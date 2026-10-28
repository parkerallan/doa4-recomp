. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'M5: CMakeLists.txt is replaced, never rewritten in place' {
    # A hard link shares the file's bytes: an in-place write shows through it, a swap does not.
    It 'Add swaps the file in' {
        $p = Join-Path $TestDrive 'm5a\CMakeLists.txt'
        Write-TFile $p $script:TestCMakeText
        $peer = Join-Path $TestDrive 'm5a\peer.txt'
        [void](New-Item -ItemType HardLink -Path $peer -Value $p)
        Add-DOA4CMakeIncludeFix -Path $p | Should Be 'added'
        [IO.File]::ReadAllText($p).Contains('rexglue-sdk/include') | Should Be $true
        [IO.File]::ReadAllText($peer) | Should BeExactly $script:TestCMakeText
    }
    It 'Remove swaps the file in' {
        $o = New-TOrigins -Base (Join-Path $TestDrive 'm5r')
        $clone = Join-Path $TestDrive 'm5r\clone'
        Invoke-TGit $TestDrive @('clone', '-q', $o.Game, $clone) | Out-Null
        $cm = Join-Path $clone 'CMakeLists.txt'
        Add-DOA4CMakeIncludeFix -Path $cm | Out-Null
        $withFix = [IO.File]::ReadAllText($cm)
        $peer = Join-Path $TestDrive 'm5r\peer.txt'
        [void](New-Item -ItemType HardLink -Path $peer -Value $cm)
        Remove-DOA4CMakeIncludeFix -Cmd (New-DOA4CommandTable) -RepoPath $clone | Should Be 'removed'
        [IO.File]::ReadAllText($peer) | Should BeExactly $withFix
        @(Get-TStatus $clone).Count | Should Be 0
    }
}

Disable-TFileProtocol
