. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'I4: child processes die with the launcher; one launcher per DOA4 folder' {
    It 'a child started through Invoke-DOA4Process is gone once the launcher process is killed' {
        $child = Join-Path $TestDrive 'child.ps1'
        Set-Content -LiteralPath $child -Encoding ASCII -Value @(
            ("Import-Module '{0}' -DisableNameChecking" -f (Join-Path $script:Src 'launcher\DOA4Setup.psm1')),
            "Invoke-DOA4Process -FilePath (Join-Path `$env:SystemRoot 'System32\PING.EXE') -Arguments @('-n', '40', '127.0.0.1') | Out-Null")
        $p = Start-Process -FilePath $script:PsExe -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $child + '"')) -PassThru -WindowStyle Hidden
        $kids = @()
        $deadline = (Get-Date).AddSeconds(20)
        while ($kids.Count -eq 0 -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 300
            $kids = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" | Where-Object { $_.Name -eq 'PING.EXE' })
        }
        $kids.Count | Should Be 1
        Stop-Process -Id $p.Id -Force
        $deadline = (Get-Date).AddSeconds(10)
        while ((Get-Process -Id $kids[0].ProcessId -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
        $alive = [bool](Get-Process -Id $kids[0].ProcessId -ErrorAction SilentlyContinue)
        Stop-Process -Id $kids[0].ProcessId -Force -ErrorAction SilentlyContinue
        $alive | Should Be $false
    }
    It 'a second lock on the same DOA4 root is refused while the first is held; another root is free' {
        $root = Join-Path $TestDrive 'lock\DOA4'
        $marker = Join-Path $TestDrive 'lock\held.txt'
        $holder = Join-Path $TestDrive 'holder.ps1'
        Set-Content -LiteralPath $holder -Encoding ASCII -Value @(
            ("Import-Module '{0}' -DisableNameChecking" -f (Join-Path $script:Src 'launcher\DOA4Setup.psm1')),
            ("`$m = Enter-DOA4RootLock -Root '{0}'" -f $root),
            ("if (`$m) {{ Set-Content -LiteralPath '{0}' -Value held }}" -f $marker),
            'Start-Sleep -Seconds 30')
        [void](New-Item -ItemType Directory -Force -Path (Split-Path $marker))
        $p = Start-Process -FilePath $script:PsExe -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $holder + '"')) -PassThru -WindowStyle Hidden
        try {
            $deadline = (Get-Date).AddSeconds(20)
            while (-not (Test-Path -LiteralPath $marker) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
            Test-Path -LiteralPath $marker | Should Be $true
            $null -eq (Enter-DOA4RootLock -Root $root) | Should Be $true
            $null -eq (Enter-DOA4RootLock -Root ($root + '\')) | Should Be $true
            $other = Enter-DOA4RootLock -Root (Join-Path $TestDrive 'lock\DOA4-other')
            $null -ne $other | Should Be $true
            $other.ReleaseMutex(); $other.Dispose()
            # The launcher itself says so and exits 1.
            $pkg = Join-Path $TestDrive 'lockpkg'
            & (Join-Path $script:Src 'make_zip.ps1') -Destination $pkg | Out-Null
            $p2 = Start-Process -FilePath $script:PsExe -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
                ("Import-Module '{0}' -DisableNameChecking; `$m = Enter-DOA4RootLock -Root '{1}'; Set-Content -LiteralPath '{2}' -Value held2; Start-Sleep -Seconds 30" -f (Join-Path $script:Src 'launcher\DOA4Setup.psm1'), (Join-Path $pkg 'DOA4'), ($marker + '2'))) -PassThru -WindowStyle Hidden
            try {
                $deadline = (Get-Date).AddSeconds(20)
                while (-not (Test-Path -LiteralPath ($marker + '2')) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
                $r = Invoke-DOA4Process -FilePath $script:PsExe -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $pkg 'DOA4\launcher\DOA4Launcher.ps1'), '-Action', 'Update')
                $r.ExitCode | Should Be 1
                ($r.Output -join "`n") | Should Match 'already running for this folder'
            } finally { Stop-Process -Id $p2.Id -Force -ErrorAction SilentlyContinue }
        } finally {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
    It 'setup.bat without arguments starts the window hidden, detached from its console; with arguments it stays in the console' {
        $b = [IO.File]::ReadAllText((Join-Path $script:Src 'setup.bat'))
        $b | Should Match '(?m)^if not "%~1"=="" goto '
        $b | Should Match '(?m)^start "" powershell\.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0launcher\\DOA4Launcher\.ps1"\s*$'
        $b | Should Match '(?m)^powershell\.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launcher\\DOA4Launcher\.ps1" %\*\s*$'
    }
    It 'setup.bat -DryRun works from a folder whose path has parentheses' {
        $out = Join-Path $TestDrive 'Program Files (x86)'
        & (Join-Path $script:Src 'make_zip.ps1') -Destination $out | Out-Null
        $bat = Join-Path $out 'DOA4\setup.bat'
        $r = Invoke-DOA4Process -FilePath (Join-Path $env:SystemRoot 'System32\cmd.exe') -ArgumentString ('/d /s /c ""{0}" -DryRun"' -f $bat)
        $r.ExitCode | Should Be 0
        ($r.Output -join "`n") | Should Match 'Dry run: nothing is installed'
    }
}

Disable-TFileProtocol
