. (Join-Path $PSScriptRoot 'TestHelpers.ps1')

Describe 'Get-DOA4ToolStatus' {
    $cfg = New-TestConfig

    It 'reports every tool present, with the VsDevCmd path derived from vswhere' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'vs1')
        $s = Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg
        @($s | Where-Object { -not $_.Present }).Count | Should Be 0
        (Get-DOA4Tool $s 'llvm').Version | Should Be '23.1.4'
        (Get-DOA4Tool $s 'git').Version | Should Be '2.51.0.windows.1'
        (Get-DOA4Tool $s 'vs').VsDevCmd | Should Be (Join-Path $tools.VsPath 'Common7\Tools\VsDevCmd.bat')
        $tools.Vswhere.State.Calls[0] | Should Be '-products * -version [18,19) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath'
    }

    foreach ($key in @('winget', 'git', 'llvm', 'vs')) {
        It "reports $key missing and only $key" {
            $tools = New-FakeTools -VsRoot (Join-Path $TestDrive "vs-$key") -Missing @($key)
            $s = Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg
            (Get-DOA4Tool $s $key).Present | Should Be $false
            @($s | Where-Object { -not $_.Present } | ForEach-Object { $_.Key }) -join ',' | Should Be $key
        }
    }

    It 'treats LLVM of another major version as missing and says which one was found' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'vs2') -ClangVersion '18.1.8'
        $llvm = Get-DOA4Tool (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) 'llvm'
        $llvm.Present | Should Be $false
        $llvm.Detail | Should Be 'found clang 18.1.8; the build needs LLVM 23'
    }

    It 'accepts another LLVM 23 minor version' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'vs3') -ClangVersion '23.0.2'
        (Get-DOA4Tool (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) 'llvm').Present | Should Be $true
    }

    It 'treats Visual Studio 2022 only (no 18.x with C++) as missing and names the version found' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'vs4') -Missing @('vs') -OtherVsVersion '17.14.36310.24'
        $vs = Get-DOA4Tool (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) 'vs'
        $vs.Present | Should Be $false
        $vs.Detail | Should Match '^found Visual Studio 17\.14\.36310\.24; the build needs version 18'
    }

    It 'treats a Visual Studio path without VsDevCmd.bat as missing' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'vs5')
        Remove-Item -LiteralPath (Join-Path $tools.VsPath 'Common7\Tools\VsDevCmd.bat')
        $vs = Get-DOA4Tool (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) 'vs'
        $vs.Present | Should Be $false
        $vs.Detail | Should Match 'but not Common7\\Tools\\VsDevCmd.bat'
    }
}

Describe 'Get-DOA4MissingToolPlan and Install-DOA4Tools' {
    $cfg = New-TestConfig

    It 'plans nothing when all tools are present' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'p1')
        $plan = Get-DOA4MissingToolPlan -Status (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) -Config $cfg
        $plan.Items.Count | Should Be 0
        $plan.Blocked | Should BeNullOrEmpty
    }

    It 'plans the exact winget command for each missing tool' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'p2') -Missing @('git', 'llvm', 'vs')
        $plan = Get-DOA4MissingToolPlan -Status (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) -Config $cfg
        @($plan.Items | ForEach-Object { $_.CommandLine }) -join "`n" | Should BeExactly (@(
                'winget install --id Git.Git -e --source winget --accept-package-agreements --accept-source-agreements',
                'winget install --id LLVM.LLVM -e --source winget --accept-package-agreements --accept-source-agreements --version 23.1.4',
                'winget install --id Microsoft.VisualStudio.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait"'
            ) -join "`n")
        # The override reaches winget as ONE argument.
        $vsItem = $plan.Items | Where-Object { $_.Key -eq 'vs' }
        $vsItem.Arguments[-1] | Should BeExactly '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait'
    }

    It 'plans an LLVM install when the wrong LLVM is present' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'p3') -ClangVersion '17.0.1'
        $plan = Get-DOA4MissingToolPlan -Status (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) -Config $cfg
        @($plan.Items | ForEach-Object { $_.Key }) -join ',' | Should Be 'llvm'
    }

    It 'blocks with the Microsoft Store link when winget is missing' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'p4') -Missing @('winget', 'git')
        $plan = Get-DOA4MissingToolPlan -Status (Get-DOA4ToolStatus -Cmd (New-FakeTable $tools) -Config $cfg) -Config $cfg
        $plan.Blocked | Should Match 'apps\.microsoft\.com/detail/9NBLGGH4NNS1'
    }

    It 'DryRun prints the commands and never calls winget install' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'p5') -Missing @('git', 'vs')
        $t = New-FakeTable $tools
        $plan = Get-DOA4MissingToolPlan -Status (Get-DOA4ToolStatus -Cmd $t -Config $cfg) -Config $cfg
        $printed = New-Object System.Collections.ArrayList
        Install-DOA4Tools -Cmd $t -Plan $plan -DryRun -OnLine { param($l) [void]$printed.Add($l) }.GetNewClosure() | Out-Null
        $printed.Count | Should Be 2
        $printed[0] | Should Match '^winget install --id Git\.Git '
        @($tools.Winget.State.Calls | Where-Object { $_ -like 'install*' }).Count | Should Be 0
    }

    It 'without DryRun runs winget once per missing tool with the planned arguments' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'p6') -Missing @('llvm')
        $tools.Winget.State.Rules += @{ Match = '^install '; Exit = 0 }
        $t = New-FakeTable $tools
        $plan = Get-DOA4MissingToolPlan -Status (Get-DOA4ToolStatus -Cmd $t -Config $cfg) -Config $cfg
        Install-DOA4Tools -Cmd $t -Plan $plan | Out-Null
        @($tools.Winget.State.Calls | Where-Object { $_ -like 'install*' }) -join '|' | Should BeExactly 'install --id LLVM.LLVM -e --source winget --accept-package-agreements --accept-source-agreements --version 23.1.4'
    }
}

Describe 'Test-DOA4Avx2' {
    It 'is true when clang reports __AVX2__ for the native CPU' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'a1')
        (Test-DOA4Avx2 -Cmd (New-FakeTable $tools)).Supported | Should Be $true
        $tools.Clang.State.Calls[0] | Should Be '-dM -E -march=native -x c -'
    }
    It 'is false, with an explanation, on a CPU without AVX2 (AVX alone is not enough)' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'a2') -NoAvx2
        $r = Test-DOA4Avx2 -Cmd (New-FakeTable $tools)
        $r.Supported | Should Be $false
        $r.Detail | Should Match 'does not support AVX2'
    }
    It 'is unknown (null) when clang cannot be run' {
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'a3') -Missing @('llvm')
        $r = Test-DOA4Avx2 -Cmd (New-FakeTable $tools)
        $null -eq $r.Supported | Should Be $true
    }
}

Describe 'Test-DOA4DiskSpace' {
    $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'd')
    It 'passes when free space is at least the need' {
        (Test-DOA4DiskSpace -Cmd (New-FakeTable $tools -Free 6GB) -Path $TestDrive -RequiredBytes 6GB).Ok | Should Be $true
    }
    It 'fails one byte short and gives both numbers' {
        $r = Test-DOA4DiskSpace -Cmd (New-FakeTable $tools -Free (6GB - 1)) -Path $TestDrive -RequiredBytes 6GB
        $r.Ok | Should Be $false
        $r.Message | Should Match '6\.0 GB free, about 6\.0 GB needed'
        $r.Free | Should Be (6GB - 1)
    }
    It 'reads real free space through the real table' {
        $r = Test-DOA4DiskSpace -Cmd (New-DOA4CommandTable) -Path $TestDrive -RequiredBytes 1
        $r.Ok | Should Be $true
        $r.Free -gt 0 | Should Be $true
    }
}

Describe 'Invoke-DOA4Install tool gate' {
    $cfg = New-TestConfig
    It 'stops at Tools without the confirmation and installs nothing' {
        $root = Join-Path $TestDrive 'gate1\DOA4'
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'gate1vs') -Missing @('git')
        $ctx = New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools)
        $r = Invoke-DOA4Install -Ctx $ctx
        $r.Ok | Should Be $false
        $r.Step | Should Be 'Tools'
        $r.Message | Should Match '^Missing tools: Git \(about 70 MB download\)'
        @($tools.Winget.State.Calls | Where-Object { $_ -like 'install*' }).Count | Should Be 0
        Test-Path (Join-Path $root 'doa4-recomp') | Should Be $false
    }
    It 'with the confirmation installs, re-detects, and goes on to the next step' {
        $root = Join-Path $TestDrive 'gate2\DOA4'
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'gate2vs') -Missing @('git')
        $flip = $tools.Git.State
        $tools.Winget.State.Rules += @{ Match = '^install --id Git\.Git'; Do = { $flip.Rules = @(@{ Match = '^--version$'; Output = @('git version 2.51.0') }) }.GetNewClosure() }
        $ctx = New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools -Free 1GB)
        $r = Invoke-DOA4Install -Ctx $ctx -ToolsApproved
        @($tools.Winget.State.Calls | Where-Object { $_ -like 'install*' }).Count | Should Be 1
        $r.Step | Should Be 'Disk space'
        $r.Message | Should Match '1\.0 GB free, about 6\.0 GB needed'
    }
    It 'stops at Tools with the winget message when winget is missing and a tool must be installed' {
        $root = Join-Path $TestDrive 'gate3\DOA4'
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'gate3vs') -Missing @('winget', 'vs')
        $r = Invoke-DOA4Install -Ctx (New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools)) -ToolsApproved
        $r.Step | Should Be 'Tools'
        $r.Message | Should Match 'Microsoft Store'
    }
    It 'does not need winget when every tool is present (goes on to Disk space)' {
        $root = Join-Path $TestDrive 'gate4\DOA4'
        $tools = New-FakeTools -VsRoot (Join-Path $TestDrive 'gate4vs') -Missing @('winget')
        $r = Invoke-DOA4Install -Ctx (New-DOA4Context -Root $root -Config $cfg -Cmd (New-FakeTable $tools -Free 1GB))
        $r.Step | Should Be 'Disk space'
    }
}
