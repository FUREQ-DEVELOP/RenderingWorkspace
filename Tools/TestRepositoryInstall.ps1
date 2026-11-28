param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')

$ErrorActionPreference = 'Stop'
$nativeOutput = Join-Path $PSScriptRoot "../out/vs2022/x64/$Configuration"
$global:atmeraRepositoryTestDll = Join-Path $nativeOutput 'ATMERA.TestPlugin.dll'
$global:atmeraRepositoryWrongDll = Join-Path $nativeOutput 'ATMERA.SecondPlugin.dll'
if (-not (Test-Path -LiteralPath $global:atmeraRepositoryTestDll) -or
    -not (Test-Path -LiteralPath $global:atmeraRepositoryWrongDll) -or
    -not (Test-Path -LiteralPath (Join-Path $nativeOutput 'ATMERA.ModuleTool.exe'))) {
    throw "Build ATMERA.sln ($Configuration|x64) before running repository install tests."
}
$projectRoot = Join-Path ([IO.Path]::GetTempPath()) ('atmera-repository-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $projectRoot | Out-Null

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
        $source = if ($name -match '1\.2\.5') { $global:atmeraRepositoryWrongDll } else { $global:atmeraRepositoryTestDll }
        if ($name.EndsWith('.sha256')) {
            [IO.File]::WriteAllText((Join-Path $directory $name), (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash)
        } else {
            Copy-Item -LiteralPath $source -Destination (Join-Path $directory $name)
        }
        return
    }
    throw 'Unexpected GitHub CLI call.'
}

try {
    $download = Join-Path $PSScriptRoot 'DownloadModule.ps1'
    & $download -ProjectRoot $projectRoot -ModuleId 'atmera.test' -Version '1.2.3' -Action Install -Configuration $Configuration | Out-Null
    $lockfile = Join-Path $projectRoot 'Modules.lock'
    if (-not (Test-Path -LiteralPath $lockfile) -or (Get-Content -LiteralPath $lockfile -Raw) -notmatch '1\.2\.3') {
        throw 'Downloaded module was not installed into the project lockfile.'
    }
    & $download -ProjectRoot $projectRoot -ModuleId 'atmera.test' -Version '1.2.4' -Action Update -Configuration $Configuration | Out-Null
    if ((Get-Content -LiteralPath $lockfile -Raw) -notmatch '1\.2\.4') {
        throw 'Downloaded module update was not selected.'
    }
    $priorLockfile = Get-Content -LiteralPath $lockfile -Raw
    $rejected = $false
    try {
        & $download -ProjectRoot $projectRoot -ModuleId 'atmera.test' -Version '1.2.5' -Action Update -Configuration $Configuration | Out-Null
    } catch { $rejected = $true }
    if (-not $rejected -or (Get-Content -LiteralPath $lockfile -Raw) -cne $priorLockfile) {
        throw 'Mismatched Plugin ABI ID changed the installed module.'
    }
    Write-Host 'Repository download, install, update, and ABI rejection tests passed.'
} finally {
    Remove-Variable -Name atmeraRepositoryTestDll, atmeraRepositoryWrongDll -Scope Global -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $projectRoot -Recurse -Force
}
