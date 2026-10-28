. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

$script:Src = Join-Path $PSScriptRoot '..'

Describe 'Launcher scripts parse' {
    foreach ($f in @('launcher\DOA4Launcher.ps1', 'launcher\DOA4Setup.psm1', 'launcher\config.ps1', 'make_zip.ps1')) {
        It "$f has no syntax errors" {
            $errors = $null
            [void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $script:Src $f), [ref]$null, [ref]$errors)
            @($errors).Count | Should Be 0
        }
    }
    It 'config.ps1 is data: one hashtable with every key the module reads' {
        $c = & (Join-Path $script:Src 'launcher\config.ps1')
        $c -is [hashtable] | Should Be $true
        foreach ($k in (New-TestConfig).Keys) { $c.ContainsKey($k) | Should Be $true }
        $c.GameRepoUrl | Should Be 'https://github.com/parkerallan/doa4-recomp'
        $c.GameRef | Should Be 'main'
        # The port's code calls SDK changes that upstream e8ce24f does not have, so the default
        # SDK is the fork branch that carries them.
        $c.SdkRepoUrl | Should Be 'https://github.com/Dwreckgg/rexglue-sdk'
        $c.SdkRef | Should Be 'doa4-recomp'
    }
}

Describe 'Launcher source files' {
    It 'are ASCII only (Windows PowerShell 5.1 reads BOM-less files in the ANSI code page)' {
        $files = @(Get-ChildItem -LiteralPath $script:Src -Recurse -File -Include *.ps1, *.psm1, *.bat | Where-Object { $_.FullName -notlike '*\tests\*' })
        $files.Count | Should Be 5
        $bad = @($files | Where-Object { @([IO.File]::ReadAllBytes($_.FullName) | Where-Object { $_ -gt 127 }).Count -gt 0 } | ForEach-Object { $_.Name })
        $bad -join ',' | Should Be ''
    }
}

Describe 'make_zip.ps1' {
    $out = Join-Path $TestDrive 'pkg out'
    $zip = Join-Path $TestDrive 'DOA4-launcher.zip'
    & (Join-Path $script:Src 'make_zip.ps1') -Destination $out -ZipPath $zip | Out-Null

    It 'writes exactly the four files of the download tree' {
        $doa4 = Join-Path $out 'DOA4'
        $full = [IO.Path]::GetFullPath($doa4)
        $list = @(Get-ChildItem -LiteralPath $doa4 -Recurse -File | ForEach-Object { $_.FullName.Substring($full.Length + 1) } | Sort-Object)
        $list -join ',' | Should Be 'launcher\config.ps1,launcher\DOA4Launcher.ps1,launcher\DOA4Setup.psm1,setup.bat'
    }
    It 'gives setup.bat CRLF line endings' {
        $b = [IO.File]::ReadAllText((Join-Path $out 'DOA4\setup.bat'))
        $b | Should Match '\r\n$'
        ([regex]::Matches($b, '(?<!\r)\n')).Count | Should Be 0
    }
    It 'zips only those four files, under DOA4/' {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $z = [IO.Compression.ZipFile]::OpenRead($zip)
        try { $names = @($z.Entries | ForEach-Object { $_.FullName } | Sort-Object) } finally { $z.Dispose() }
        $names -join ',' | Should Be 'DOA4/launcher/config.ps1,DOA4/launcher/DOA4Launcher.ps1,DOA4/launcher/DOA4Setup.psm1,DOA4/setup.bat'
    }
    It 'refreshing an installed folder touches only the four files' {
        $mine = Join-Path $out 'DOA4\launcher\settings.json'
        Write-TFile $mine '{"gameFiles":"D:/x"}'
        Write-TFile (Join-Path $out 'DOA4\doa4-recomp\keep.txt') 'keep'
        & (Join-Path $script:Src 'make_zip.ps1') -Destination $out | Out-Null
        [IO.File]::ReadAllText($mine) | Should Be '{"gameFiles":"D:/x"}'
        Test-Path -LiteralPath (Join-Path $out 'DOA4\doa4-recomp\keep.txt') | Should Be $true
    }
}

Describe 'The launcher''s non-interactive mode (a real powershell.exe on the packaged files)' {
    # ASCII: Install, Update and Play refuse a non-ASCII DOA4 path (review I1, tested there).
    $out = Join-Path $TestDrive 'cli test'
    & (Join-Path $script:Src 'make_zip.ps1') -Destination $out | Out-Null
    $launcher = Join-Path $out 'DOA4\launcher\DOA4Launcher.ps1'
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

    It '-Action Status says not built and exits 0' {
        $r = Invoke-DOA4Process -FilePath $ps -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcher, '-Action', 'Status')
        $r.ExitCode | Should Be 0
        ($r.Output -join "`n") | Should Match 'status: not built'
    }
    It '-Action Update before Install exits 1 with the message' {
        $r = Invoke-DOA4Process -FilePath $ps -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $launcher, '-Action', 'Update')
        $r.ExitCode | Should Be 1
        ($r.Output -join "`n") | Should Match 'Not installed yet: run Install first\.'
    }
    It 'setup.bat -DryRun -AssumeMissing git prints the git winget command and changes nothing' {
        $bat = Join-Path $out 'DOA4\setup.bat'
        $r = Invoke-DOA4Process -FilePath (Join-Path $env:SystemRoot 'System32\cmd.exe') -ArgumentString ('/d /s /c ""{0}" -DryRun -AssumeMissing git"' -f $bat)
        $r.ExitCode | Should Be 0
        $text = $r.Output -join "`n"
        $text | Should Match 'winget install --id Git\.Git -e --source winget --accept-package-agreements --accept-source-agreements'
        Test-Path -LiteralPath (Join-Path $out 'DOA4\doa4-recomp') | Should Be $false
    }
}
