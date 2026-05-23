# =============================================================================
# CHROMODYNAMIC — run_golden.ps1
# Phase 11 Track A driver.
#
# Two modes:
#   capture  → re-records every wired sample's golden reference PNG into
#              tests/golden/<sample>.png. Use once after a deliberate
#              visual change has been reviewed by a human.
#   compare  → diffs every wired sample against its committed golden;
#              fails the run on any mismatch above tolerance. This is
#              the CI gate the rollback-class bugs (#1, #2, #5) would
#              have tripped if it had existed before the marathon.
#
# Usage:
#   pwsh scripts/run_golden.ps1 -Mode compare
#   pwsh scripts/run_golden.ps1 -Mode capture -BuildDir build/ninja-base
#
# Exit codes:
#   0 — every sample passed (compare) or wrote (capture)
#   1 — script-level error (missing binary, missing reference, …)
#   2 — at least one sample's compare exceeded the tolerance
# =============================================================================
[CmdletBinding()]
param(
    [ValidateSet('capture','compare')]
    [string]$Mode = 'compare',
    [string]$BuildDir = 'build/ninja-base',
    [string]$Config = 'Debug',
    [int]$Tolerance = 8,
    [int]$TimeoutSec = 15,
    [string]$GoldenDir = 'tests/golden',
    [string]$DiffDir = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
Set-Location $repoRoot

# Wired samples — keep in sync with the per-sample main.cpp's that
# include GoldenCapture.hpp. Track A Part 1 wired the 5 with shader-
# pipeline regression risk; remaining windowed samples wire in
# follow-up waves.
$samples = @(
    'hello_triangle',
    'hello_cube',
    'hello_anim',
    'hello_pbr',
    'hello_skybox'
)

$multiConfigBin = Join-Path $BuildDir "bin/$Config"
$singleBin      = Join-Path $BuildDir 'bin'
if (Test-Path $multiConfigBin) {
    $sampleDir = $multiConfigBin
} elseif (Test-Path $singleBin) {
    $sampleDir = $singleBin
} else {
    Write-Error "Sample binary directory not found. Tried: $multiConfigBin, $singleBin"
    exit 1
}

if (-not (Test-Path $GoldenDir)) {
    if ($Mode -eq 'compare') {
        Write-Error "Golden reference directory missing: $GoldenDir. Run --Mode capture first."
        exit 1
    }
    New-Item -ItemType Directory -Path $GoldenDir | Out-Null
}

if ($DiffDir -ne '' -and -not (Test-Path $DiffDir)) {
    New-Item -ItemType Directory -Path $DiffDir | Out-Null
}

$pass  = 0
$fail  = 0
$total = 0
$failed_list = @()

foreach ($name in $samples) {
    $exe = Join-Path $sampleDir "$name.exe"
    if (-not (Test-Path $exe)) {
        Write-Error "Sample binary missing: $exe"
        exit 1
    }
    $total++
    $golden = Join-Path $GoldenDir "$name.png"

    # Build the argv. capture: --golden-capture <path>; compare: --golden-compare <path>.
    if ($Mode -eq 'capture') {
        $argv = @('--golden-capture', $golden, '--golden-tolerance', "$Tolerance")
    } else {
        if (-not (Test-Path $golden)) {
            Write-Host -ForegroundColor Yellow ("  [SKIP] {0,-22} no reference at {1}" -f $name, $golden)
            $fail++
            $failed_list += "$name (no reference)"
            continue
        }
        $argv = @('--golden-compare', $golden, '--golden-tolerance', "$Tolerance")
        if ($DiffDir -ne '') {
            $diff = Join-Path $DiffDir "$name.diff.png"
            $argv += @('--golden-diff-out', $diff)
        }
    }

    Write-Host -NoNewline ("  [{0,-22}] {1} ... " -f $name, $Mode)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $quoted = $argv | ForEach-Object { '"' + ($_ -replace '"','`"') + '"' }
    $psi.Arguments = ($quoted -join ' ')
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow         = $true

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = [System.Diagnostics.Process]::Start($psi)
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    $exited  = $proc.WaitForExit($TimeoutSec * 1000)
    $sw.Stop()
    if (-not $exited) {
        try { $proc.Kill() } catch {}
        $proc.WaitForExit() | Out-Null
        Write-Host -ForegroundColor Yellow ("TIMEOUT ({0}s)" -f $TimeoutSec)
        $fail++
        $failed_list += "$name (timeout)"
        $proc.Dispose()
        continue
    }
    $outTask.Wait()
    $errTask.Wait()
    $stdout = $outTask.Result
    $stderr = $errTask.Result
    $code = $proc.ExitCode
    $proc.Dispose()

    # Pull the golden line out so summary stays readable.
    $line = ($stdout -split "`n" | Where-Object { $_ -match '\[golden\]' } | Select-Object -First 1).Trim()

    if ($code -eq 0) {
        Write-Host -ForegroundColor Green ("OK ({0:N1}s)" -f $sw.Elapsed.TotalSeconds)
        if ($line) { Write-Host "    $line" }
        $pass++
    } elseif ($code -eq 2 -and $Mode -eq 'compare') {
        Write-Host -ForegroundColor Red 'MISMATCH'
        if ($line) { Write-Host "    $line" }
        $fail++
        $failed_list += "$name (mismatch)"
    } else {
        Write-Host -ForegroundColor Red ("FAIL exit={0}" -f $code)
        if ($stderr -ne '') { Write-Host "    stderr: $($stderr.TrimEnd())" }
        $fail++
        $failed_list += "$name (rc=$code)"
    }
}

Write-Host ''
Write-Host ('=== golden {0} summary ============================================' -f $Mode)
Write-Host ('  build dir   : {0}' -f (Resolve-Path $BuildDir))
Write-Host ('  golden dir  : {0}' -f (Resolve-Path $GoldenDir))
Write-Host ('  tolerance   : {0} / 255 per channel' -f $Tolerance)
$summaryColor = 'Red'
if ($fail -eq 0) { $summaryColor = 'Green' }
Write-Host ('  result      : {0}/{1} passed, {2} failed' -f $pass, $total, $fail) -ForegroundColor $summaryColor
Write-Host '===================================================================='

if ($fail -gt 0) {
    Write-Host -ForegroundColor Red 'Failed:'
    foreach ($f in $failed_list) {
        Write-Host -ForegroundColor Red "  - $f"
    }
    exit 2
}
exit 0
