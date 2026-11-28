param(
    [string]$Query = '',
    [int]$Limit = 100
)

$ErrorActionPreference = 'Stop'
if ($Limit -lt 1 -or $Limit -gt 1000) {
    throw 'Limit must be between 1 and 1000.'
}

$repository = 'FUREQ-DEVELOP/RenderingLibrary'
$releases = & gh release list --repo $repository --limit $Limit --json name,tagName,isDraft,isPrerelease,publishedAt
if ($LASTEXITCODE -ne 0) {
    throw "Could not list releases from $repository. Check gh auth status."
}

$matches = @(
    $releases | ConvertFrom-Json | Where-Object {
        -not $_.isDraft -and $_.tagName -match '^module-(?<id>[A-Za-z0-9._-]+)-v(?<version>[0-9]+\.[0-9]+\.[0-9]+)$' -and
        ($Query.Length -eq 0 -or $Matches.id.Contains($Query, [StringComparison]::OrdinalIgnoreCase) -or
            ([string]$_.name).Contains($Query, [StringComparison]::OrdinalIgnoreCase))
    } | ForEach-Object {
        [pscustomobject]@{
            id = [regex]::Match($_.tagName, '^module-(?<id>[A-Za-z0-9._-]+)-v(?<version>[0-9]+\.[0-9]+\.[0-9]+)$').Groups['id'].Value
            version = [regex]::Match($_.tagName, '^module-(?<id>[A-Za-z0-9._-]+)-v(?<version>[0-9]+\.[0-9]+\.[0-9]+)$').Groups['version'].Value
            tag = $_.tagName
            prerelease = $_.isPrerelease
            publishedAt = $_.publishedAt
        }
    }
)
ConvertTo-Json -InputObject $matches -Depth 3 -Compress
