. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

# Findings of the final review (I1-I5, M4, M5), each reproduced before its fix. P1-P7 are
# the reviewer's probes, kept as tests.

$script:Src = Join-Path $PSScriptRoot '..'
$script:PsExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

Describe 'I1: the DOA4 folder must have a plain ASCII path (the SDK reads argv in the ANSI code page)' {
    $cfg = New-TestConfig
    foreach ($case in @(@{ Name = 'e-acute (fits the code page, still mangled)'; Root = 'DOA4 ' + [char]0x00E9 }, @{ Name = 'a CJK letter'; Root = 'DOA4 ' + [char]0x65E5 })) {
        It "Install refuses a root with $($case.Name) before cloning anything" {
            $base = Join-Path $TestDrive ('i1-' + [Guid]::NewGuid().ToString('N').Substring(0, 6))
            $t = New-TInstalled -Base $base -RootName $case.Root
            $t.Install.Ok | Should Be $false
            $t.Install.Step | Should Be 'Folder'
            $t.Install.Message | Should Match 'Move the DOA4 folder to a path with plain letters'
            Test-Path -LiteralPath (Join-Path $t.Root 'doa4-recomp') | Should Be $false
            Test-Path -LiteralPath (Join-Path $t.Root 'userdata') | Should Be $false
        }
    }
    It 'Update refuses it too (before anything else)' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'i1u-vs')
        $ctx = New-DOA4Context -Root (Join-Path $TestDrive ('i1u\DOA4 ' + [char]0x00E9)) -Config $cfg -Cmd (New-FakeTable $tools)
        $r = Invoke-DOA4Update -Ctx $ctx
        $r.Step | Should Be 'Folder'
        $r.Message | Should Match 'Move the DOA4 folder'
    }
    It 'Play refuses it too' {
        $ctx = New-DOA4Context -Root (Join-Path $TestDrive ('i1p\DOA4 ' + [char]0x00E9)) -Config $cfg -Cmd @{}
        Write-TFile $ctx.ExePath 'exe'
        Write-TFile (Join-Path $ctx.GamePath 'src\doa4_setup_dialog.cpp') '// setup'
        { Start-DOA4Game -Ctx $ctx } | Should Throw 'Move the DOA4 folder'
    }
    It 'an ASCII root with spaces, brackets and signs is accepted' {
        Test-DOA4AsciiPath 'C:\My Games (x86)\DOA4 [copy] #2' | Should Be $true
        Test-DOA4AsciiPath ('C:\' + [char]0x00E9) | Should Be $false
        Test-DOA4AsciiPath "C:\tab`there" | Should Be $false
    }
    It 'an upstream build refuses a non-ASCII game folder, even one that fits the code page' {
        $dir = Join-Path $TestDrive ('i1g\game ' + [char]0x00E9)
        Write-TFile (Join-Path $dir 'default.xex') 'x'
        $sha = (Get-FileHash -LiteralPath (Join-Path $dir 'default.xex') -Algorithm SHA1).Hash.ToLowerInvariant()
        (Test-DOA4GameFolder -Path $dir -ExpectedSha1 $sha -CodePage 1252).Verdict | Should Be 'Ok'
        (Test-DOA4GameFolder -Path $dir -ExpectedSha1 $sha -CodePage 1252 -AsciiOnly).Verdict | Should Be 'PathUnsupported'
        $ctx = New-DOA4Context -Root (Join-Path $TestDrive 'i1g\DOA4') -Config $cfg -Cmd @{}
        Write-TFile (Join-Path $ctx.GamePath 'CMakeLists.txt') 'x'
        [void](New-Item -ItemType Directory -Force -Path $ctx.BuildDir)
        { Set-DOA4GameFiles -Ctx $ctx -Folder $dir } | Should Throw 'plain letters and digits'
    }
}

Disable-TFileProtocol
