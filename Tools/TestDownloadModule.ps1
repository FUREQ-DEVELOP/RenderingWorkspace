$ErrorActionPreference = 'Stop'
$projectRoot = Join-Path ([IO.Path]::GetTempPath()) ('atmera-download-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $projectRoot | Out-Null
$global:atmeraTestBadHash = $false

function gh {
    $global:LASTEXITCODE = 0
    if ($args[1] -eq 'view') {
        $version = $args[2] -replace '^module-atmera\.test-v', ''
        $name = "atmera.test-$version-win-x64.dll"
        return (@{ tagName = $args[2]; isDraft = $false; assets = @(@{ name = $name }, @{ name = "$name.sha256" }) } | ConvertTo-Json -Depth 4 -Compress)
    }
    if ($args[1] -eq 'download') {
        $name = $args[[array]::IndexOf($args, '--pattern') + 1]
        $directory = $args[[array]::IndexOf($args, '--dir') + 1]
        $bytes = [byte[]](1, 2, 3, 4)
        if ($name.EndsWith('.sha256')) {
            $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
            if ($global:atmeraTestBadHash) { $hash = '0' * 64 }
            [IO.File]::WriteAllText((Join-Path $directory $name), $hash)
        } else {
            [IO.File]::WriteAllBytes((Join-Path $directory $name), $bytes)
        }
        return
    }
    throw 'Unexpected GitHub CLI call.'
}

try {
    $tool = Join-Path $PSScriptRoot 'DownloadModule.ps1'
    $downloaded = & $tool -ProjectRoot $projectRoot -ModuleId 'atmera.test' -Version '1.2.3'
    if (-not (Test-Path -LiteralPath $downloaded -PathType Leaf)) {
        throw 'Verified DLL was not saved in the project.'
    }
    $global:atmeraTestBadHash = $true
    $failed = $false
    try { & $tool -ProjectRoot $projectRoot -ModuleId 'atmera.test' -Version '1.2.4' | Out-Null }
    catch {
        $failed = $_.Exception.Message -match 'hash mismatch'
        if (-not $failed) { throw }
    }
    $leftBehind = Test-Path -LiteralPath (Join-Path $projectRoot 'Downloads/atmera.test/1.2.4')
    if (-not $failed -or $leftBehind) {
        throw 'Hash mismatch left a downloaded version behind.'
    }
    if (Test-Path -LiteralPath (Join-Path $projectRoot 'Modules')) {
        throw 'Download unexpectedly installed a module.'
    }
    Write-Host 'Module download and hash validation tests passed.'
} finally {
    Remove-Variable -Name atmeraTestBadHash -Scope Global -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $projectRoot -Recurse -Force
}
