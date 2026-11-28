param(
    [Parameter(Mandatory)][string]$ProjectRoot,
    [Parameter(Mandatory)][string]$ModuleId,
    [Parameter(Mandatory)][string]$Version,
    [ValidateSet('Download', 'Install', 'Update')][string]$Action = 'Download',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
if ($ModuleId -notmatch '^[A-Za-z0-9._-]+$' -or $ModuleId -in @('.', '..')) {
    throw 'Invalid module ID.'
}
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$' -or $Version.Length -gt 64) {
    throw 'Version must be numeric major.minor.patch.'
}

$repository = 'FUREQ-DEVELOP/RenderingLibrary'
$tag = "module-$ModuleId-v$Version"
$dllName = "$ModuleId-$Version-win-x64.dll"
$hashName = "$dllName.sha256"
$releaseJson = & gh release view $tag --repo $repository --json tagName,isDraft,assets
if ($LASTEXITCODE -ne 0) { throw "Release $tag was not found in $repository." }
$release = $releaseJson | ConvertFrom-Json
if ($release.tagName -cne $tag -or $release.isDraft) { throw 'Release tag is invalid or draft.' }
foreach ($name in @($dllName, $hashName)) {
    if (@($release.assets | Where-Object { $_.name -ceq $name }).Count -ne 1) {
        throw "Release asset is missing or duplicated: $name"
    }
}

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Project root must be a directory.' }
$downloads = Join-Path $root 'Downloads'
$moduleDirectory = Join-Path $downloads $ModuleId
foreach ($path in @($downloads, $moduleDirectory)) {
    if (Test-Path -LiteralPath $path) {
        if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Download path redirects outside the project: $path"
        }
    } else {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

$destination = Join-Path $moduleDirectory $Version
if (Test-Path -LiteralPath $destination) { throw "Download already exists: $destination" }
$stage = Join-Path $moduleDirectory ('.' + [guid]::NewGuid().ToString('N') + '.partial')
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    foreach ($name in @($dllName, $hashName)) {
        & gh release download $tag --repo $repository --pattern $name --dir $stage
        if ($LASTEXITCODE -ne 0) { throw "Failed to download release asset: $name" }
        if (-not (Test-Path -LiteralPath (Join-Path $stage $name) -PathType Leaf)) {
            throw "Downloaded asset is missing: $name"
        }
    }
    $expected = (Get-Content -LiteralPath (Join-Path $stage $hashName) -Raw).Trim()
    if ($expected -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid SHA-256 sidecar.' }
    $actual = (Get-FileHash -LiteralPath (Join-Path $stage $dllName) -Algorithm SHA256).Hash
    if ($actual -cne $expected.ToUpperInvariant()) { throw 'Downloaded DLL hash mismatch.' }
    Move-Item -LiteralPath $stage -Destination $destination
    $dll = Join-Path $destination $dllName
    if ($Action -ne 'Download') {
        $tool = Join-Path $PSScriptRoot "../out/vs2022/x64/$Configuration/ATMERA.ModuleTool.exe"
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
            throw "Build ATMERA.sln ($Configuration|x64) before installing modules."
        }
        & $tool $Action.ToLowerInvariant() $root $dll $ModuleId $Version | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Project-local module $Action failed with exit code $LASTEXITCODE." }
    }
    Write-Output $dll
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
