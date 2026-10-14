param(
    [string]$Preset = "ninja-debug",
    [string]$Target = "",
    [string]$TestRegex = "",
    [int]$MaxLines = 40
)

$ErrorActionPreference = "Continue"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

Write-Output "=== [1/2] BUILD (Preset: $Preset, Target: $(if ($Target) { $Target } else { 'ALL' })) ==="
if ($Target -ne "") {
    $buildOut = & cmake --build --preset $Preset --target $Target 2>&1
} else {
    $buildOut = & cmake --build --preset $Preset 2>&1
}
$buildCode = $LASTEXITCODE

if ($buildCode -ne 0) {
    Write-Output "STATUS: BUILD_FAILED (ExitCode: $buildCode, Elapsed: $($sw.Elapsed.TotalSeconds.ToString('F1'))s)"
    $buildOut | Select-String -Pattern "error:|fatal error|FAILED:" -Context 1,2 | Select-Object -First $MaxLines
    exit $buildCode
}

$buildOut | Select-Object -Last 8
Write-Output "STATUS: BUILD_OK ($($sw.Elapsed.TotalSeconds.ToString('F1'))s)"

Write-Output "=== [2/2] CTEST (Preset: $Preset, Regex: $(if ($TestRegex) { $TestRegex } else { 'ALL' })) ==="
if ($TestRegex -ne "") {
    $testOut = & ctest --preset $Preset -R $TestRegex --output-on-failure 2>&1
} else {
    $testOut = & ctest --preset $Preset --output-on-failure 2>&1
}
$testCode = $LASTEXITCODE
$sw.Stop()

$testOut | Select-Object -Last $MaxLines
if ($testCode -ne 0) {
    Write-Output "STATUS: TEST_FAILED (ExitCode: $testCode, TotalElapsed: $($sw.Elapsed.TotalSeconds.ToString('F1'))s)"
    exit $testCode
}

Write-Output "STATUS: WAVE_VERIFIED_OK (TotalElapsed: $($sw.Elapsed.TotalSeconds.ToString('F1'))s)"
exit 0
