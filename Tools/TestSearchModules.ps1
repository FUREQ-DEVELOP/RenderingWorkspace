$ErrorActionPreference = 'Stop'

function gh {
    $global:LASTEXITCODE = 0
    '[{"name":"Bloom module","tagName":"module-atmera.bloom-v1.2.3","isDraft":false,"isPrerelease":false,"publishedAt":"2026-01-01T00:00:00Z"},{"name":"SDK","tagName":"v1.0.0","isDraft":false,"isPrerelease":false,"publishedAt":"2026-01-01T00:00:00Z"},{"name":"Draft module","tagName":"module-atmera.draft-v1.0.0","isDraft":true,"isPrerelease":false,"publishedAt":"2026-01-01T00:00:00Z"}]'
}

$results = & (Join-Path $PSScriptRoot 'SearchModules.ps1') -Query bloom | ConvertFrom-Json
if (@($results).Count -ne 1 -or $results.id -ne 'atmera.bloom' -or
    $results.version -ne '1.2.3' -or $results.tag -ne 'module-atmera.bloom-v1.2.3') {
    throw 'Module release search did not filter and parse results correctly.'
}

$empty = & (Join-Path $PSScriptRoot 'SearchModules.ps1') -Query missing
if ($empty -ne '[]') { throw 'Empty search did not return an empty JSON array.' }

Write-Host 'Module release search tests passed.'
