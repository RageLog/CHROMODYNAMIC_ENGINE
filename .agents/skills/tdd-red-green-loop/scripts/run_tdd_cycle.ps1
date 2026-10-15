param(
    [Parameter(Mandatory=$true)]
    [string]$Target,
    [string]$Filter = "*",
    [string]$Preset = "ninja-base",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Continue"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
if ($Preset -eq "debug" -and (Test-Path "build/ninja-base")) {
    $Preset = "ninja-base"
}
$buildDir = "build/$Preset"

if (-not (Test-Path $buildDir)) {
    Write-Output "[TDD] Configuring CMake preset '$Preset'..."
    cmake --preset $Preset | Out-Null
}

Write-Output "[TDD] Building single target '$Target' ($Config)..."
$buildOut = cmake --build $buildDir --config $Config --target $Target 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Output "[TDD] BUILD FAILED for '$Target' (Red):"
    $buildOut | Select-String -Pattern "error[:\s]|FAILED:" | Select-Object -First 15 | ForEach-Object { Write-Output $_.Line }
    Write-Output "VERDICT: RED (COMPILE_ERROR)"
    exit 1
}

Write-Output "[TDD] Running ctest for '$Target' (filter='$Filter')..."
$ctestArgs = @("--test-dir", $buildDir, "-C", $Config, "-R", $Target, "--output-on-failure")
if ($Filter -ne "*") {
    $ctestArgs += @("--verbose")
}
$testOut = & ctest @ctestArgs 2>&1
$sw.Stop()
$elapsed = [math]::Round($sw.Elapsed.TotalSeconds, 2)

if ($LASTEXITCODE -ne 0) {
    Write-Output "[TDD] TEST FAILED in ${elapsed}s (Red):"
    $testOut | Select-Object -Last 25 | ForEach-Object { Write-Output $_ }
    Write-Output "VERDICT: RED (TEST_FAILURE)"
    exit 1
}

$summary = $testOut | Select-String -Pattern "tests passed|Total Test time"
foreach ($line in $summary) { Write-Output $line.Line }
Write-Output "VERDICT: GREEN (${elapsed}s)"
exit 0
