. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'I5: a hand edit to a copied file symlink is a foreign change' {
    It 'P1: Update refuses and keeps the edit' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'p1')
        $copy = Join-Path $t.Root 'rexglue-sdk\thirdparty\lib\link\a.c'
        [IO.File]::WriteAllText($copy, "int a; /* MY HAND EDIT */`r`n")
        Add-TUpstreamCommit $t
        $r = Invoke-DOA4Update -Ctx (New-TCtx $t 'update')
        $r.Ok | Should Be $false
        $r.Message | Should Match 'rexglue-sdk/thirdparty/lib/link/a\.c \(copied file differs from its link target\)'
        [IO.File]::ReadAllText($copy) | Should Match 'MY HAND EDIT'
    }
    It 'P1b control: the same edit to the copied folder is refused and kept' {
        $t = New-TInstalled -Base (Join-Path $TestDrive 'p1b')
        $copy = Join-Path $t.Root 'rexglue-sdk\thirdparty\lib\linkdir\x.h'
        [IO.File]::WriteAllText($copy, "#define X 1 /* MY HAND EDIT */`r`n")
        Add-TUpstreamCommit $t
        $r = Invoke-DOA4Update -Ctx (New-TCtx $t 'update')
        $r.Ok | Should Be $false
        [IO.File]::ReadAllText($copy) | Should Match 'MY HAND EDIT'
    }
}

Disable-TFileProtocol
