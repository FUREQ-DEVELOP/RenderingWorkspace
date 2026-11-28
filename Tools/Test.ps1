param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$nativeOutput = Join-Path $projectRoot "out/vs2022/x64/$Configuration"
$testExe = Join-Path $nativeOutput 'ATMERA.KernelTests.exe'
$testDll = Join-Path $nativeOutput 'ATMERA.TestPlugin.dll'
$secondDll = Join-Path $nativeOutput 'ATMERA.SecondPlugin.dll'
if (-not (Test-Path -LiteralPath $testExe) -or -not (Test-Path -LiteralPath $testDll) -or -not (Test-Path -LiteralPath $secondDll)) {
    throw "Build ATMERA.sln ($Configuration|x64) before running tests."
}

& $testExe $testDll $secondDll
if ($LASTEXITCODE -ne 0) { throw "Kernel tests failed with exit code $LASTEXITCODE." }

$studioExe = Join-Path $projectRoot "Studio/ATMERA.Studio/bin/$Configuration/net9.0-windows/ATMERA.Studio.exe"
if (-not (Test-Path -LiteralPath $studioExe)) { throw "Studio was not built." }
$studioProcess = Start-Process -FilePath $studioExe -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru
try {
    if (-not $studioProcess.WaitForExit(15000)) {
        $studioProcess.Kill()
        throw 'Studio startup test timed out.'
    }
    if ($studioProcess.ExitCode -ne 0) { throw "Studio startup test failed with exit code $($studioProcess.ExitCode)." }
} finally {
    $studioProcess.Dispose()
}

$triangleExe = Join-Path $nativeOutput 'ATMERA.Triangle.exe'
if (-not (Test-Path -LiteralPath $triangleExe)) { throw "Triangle sample was not built." }
foreach ($backend in @('d3d11', 'd3d12')) {
    & $triangleExe --backend $backend --smoke
    if ($LASTEXITCODE -ne 0) { throw "$backend graphics test failed with exit code $LASTEXITCODE." }
}

$consumerExe = Join-Path $nativeOutput 'ATMERA.ExternalConsumer.exe'
if (-not (Test-Path -LiteralPath $consumerExe)) { throw 'External consumer sample was not built.' }
& $consumerExe --graph-test
if ($LASTEXITCODE -ne 0) { throw "Render graph scheduling test failed with exit code $LASTEXITCODE." }
foreach ($backend in @('d3d11', 'd3d12')) {
    foreach ($mode in @('--headless', '--window')) {
        & $consumerExe --backend $backend $mode
        if ($LASTEXITCODE -ne 0) { throw "$backend external consumer $mode test failed with exit code $LASTEXITCODE." }
    }
}

& (Join-Path $PSScriptRoot 'TestSearchModules.ps1')
& (Join-Path $PSScriptRoot 'TestDownloadModule.ps1')
& (Join-Path $PSScriptRoot 'TestRepositoryInstall.ps1') -Configuration $Configuration

Write-Host 'Native lifecycle, Studio, graphics, and external consumer tests passed.'
