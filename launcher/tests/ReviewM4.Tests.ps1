. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'M4: -Action with -DryRun is an error; there is no -Quiet' {
    $pkg = Join-Path $TestDrive 'm4'
    & (Join-Path $script:Src 'make_zip.ps1') -Destination $pkg | Out-Null
    $launcher = Join-Path $pkg 'DOA4\launcher\DOA4Launcher.ps1'
    # -Action Update, not Install: before the fix the combination ran the real action, and a
    # real Install would clone upstream. Update on an uninstalled folder only reports.
    It '-Action Update -DryRun does not run the action: it exits 1 with the reason' {
        $r = Invoke-DOA4Process -FilePath $script:PsExe -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcher, '-Action', 'Update', '-DryRun')
        $r.ExitCode | Should Be 1
        ($r.Output -join "`n") | Should Match '-DryRun cannot be combined with -Action Update'
        ($r.Output -join "`n") | Should Not Match 'Not installed yet'
    }
    It 'the launcher has no -Quiet parameter' {
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$null, [ref]$null)
        @($ast.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath }) -contains 'Quiet' | Should Be $false
    }
}

Disable-TFileProtocol
