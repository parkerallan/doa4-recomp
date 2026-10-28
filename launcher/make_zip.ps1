#Requires -Version 5.1
# Packages the launcher as the user downloads it:
#   DOA4\setup.bat
#   DOA4\launcher\DOA4Launcher.ps1, DOA4Setup.psm1, config.ps1
# -Destination <folder>  writes (or refreshes) those four files in <folder>\DOA4; nothing
#                        else in that folder is touched. config.ps1 is one of the four, so
#                        an edited config there (other repository URLs) is replaced by the
#                        default one: this is a packaging tool, not an updater.
# -ZipPath <file.zip>    also writes a zip holding only those four files, under DOA4\.
param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$ZipPath = ''
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$src = $PSScriptRoot
$files = @(
    @{ From = 'setup.bat'; To = 'setup.bat' },
    @{ From = 'launcher\DOA4Launcher.ps1'; To = 'launcher\DOA4Launcher.ps1' },
    @{ From = 'launcher\DOA4Setup.psm1'; To = 'launcher\DOA4Setup.psm1' },
    @{ From = 'launcher\config.ps1'; To = 'launcher\config.ps1' }
)

# Text with CRLF line endings, whatever the checkout's line endings are (cmd needs CRLF).
function Copy-Text([string]$from, [string]$to) {
    $text = [IO.File]::ReadAllText($from) -replace "`r?`n", "`r`n"
    $dir = Split-Path -Parent $to
    if (-not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Path $dir) }
    [IO.File]::WriteAllText($to, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Write-Package([string]$doa4) {
    foreach ($f in $files) { Copy-Text (Join-Path $src $f.From) (Join-Path $doa4 $f.To) }
}

$target = Join-Path ([IO.Path]::GetFullPath($Destination)) 'DOA4'
Write-Package $target
Write-Output "launcher files written to $target"

if ($ZipPath) {
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('doa4-launcher-zip-' + [Guid]::NewGuid().ToString('N'))
    try {
        Write-Package (Join-Path $stage 'DOA4')
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $zip = [IO.Path]::GetFullPath($ZipPath)
        if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip }
        # Entries one by one with forward slashes: ZipFile.CreateFromDirectory under Windows
        # PowerShell 5.1 writes backslashes, which other unzip tools treat as part of the name.
        $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($f in $files) {
                $name = 'DOA4/' + $f.To.Replace('\', '/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path (Join-Path $stage 'DOA4') $f.To), $name)
            }
        } finally {
            $archive.Dispose()
        }
        Write-Output "zip written to $zip"
    } finally {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
