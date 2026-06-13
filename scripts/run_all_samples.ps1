# =============================================================================
# CHROMODYNAMIC — run_all_samples.ps1
# Smoke-tests every hello_*.exe in headless mode with a watchdog timeout.
#
# Usage:
#   pwsh scripts/run_all_samples.ps1                          # default Debug
#   pwsh scripts/run_all_samples.ps1 -Config Release
#   pwsh scripts/run_all_samples.ps1 -BuildDir build/ci-gcc
#   pwsh scripts/run_all_samples.ps1 -TimeoutSec 8 -Frames 5
#
# Exit codes:
#   0 — every sample exited 0 within the watchdog
#   1 — at least one sample failed (non-zero exit, crash, or timeout)
#   2 — script-level error (no build dir, no samples found)
# =============================================================================
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/ninja-base',
    [string]$Config = 'Debug',
    [int]$TimeoutSec = 10,
    [int]$Frames = 3,
    [string]$LogDir = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
Set-Location $repoRoot

# Resolve sample directory. Multi-config (Ninja MC) uses bin/<Config>/, single
# config drops them straight into bin/. Try both.
$multiConfigBin = Join-Path $BuildDir "bin/$Config"
$singleBin      = Join-Path $BuildDir "bin"
if (Test-Path $multiConfigBin) {
    $sampleDir = $multiConfigBin
} elseif (Test-Path $singleBin) {
    $sampleDir = $singleBin
} else {
    Write-Error "Sample binary directory not found. Tried: $multiConfigBin, $singleBin"
    exit 2
}

if ($LogDir -eq '') {
    $LogDir = Join-Path $BuildDir 'smoke-logs'
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

# Discover samples. We assume the convention `hello_*.exe`.
$samples = Get-ChildItem -Path $sampleDir -Filter 'hello_*.exe' | Sort-Object Name
if ($samples.Count -eq 0) {
    Write-Error "No hello_*.exe found under $sampleDir"
    exit 2
}

# Samples that need a content argument (a path) — without it they exit cleanly
# but as a no-op. We pass nothing here; if a path is missing, the sample prints
# usage and returns non-zero, and that's the right signal: regression caught.
$argsForSample = @{
}

# Some samples take long because they cook on first run.
# hello_asset_pipeline runs 6 sections including BC7 cook — give it extra time.
$extraTimeout = @{
    'hello_asset_pipeline.exe' = 60
}

$results = @()
$startWall = Get-Date

foreach ($exe in $samples) {
    $name = $exe.Name
    $log  = Join-Path $LogDir ($name -replace '\.exe$','.log')
    $tmo  = $TimeoutSec
    if ($extraTimeout.ContainsKey($name)) { $tmo = $extraTimeout[$name] }

    # Build argv: --headless N + per-sample extras
    $argv = @('--headless', "$Frames")
    if ($argsForSample.ContainsKey($name)) {
        $argv += $argsForSample[$name]
    }

    Write-Host -NoNewline ("  [{0,-32}] ... " -f $name)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    # Use raw .NET Process directly: Start-Process -PassThru on PS 5.1 with
    # I/O redirection leaves ExitCode unreliable after a bounded WaitForExit.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe.FullName
    # PS 5.1's ProcessStartInfo has no ArgumentList; build a single quoted
    # string. Each argv token is double-quoted with embedded quotes escaped.
    $quoted = $argv | ForEach-Object { '"' + ($_ -replace '"','`"') + '"' }
    $psi.Arguments = ($quoted -join ' ')
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow         = $true

    $proc = $null
    try {
        $proc = [System.Diagnostics.Process]::Start($psi)
        # Drain stdout/stderr on background threads — synchronous reads
        # would deadlock when output exceeds the 4KB pipe buffer.
        $outTask = $proc.StandardOutput.ReadToEndAsync()
        $errTask = $proc.StandardError.ReadToEndAsync()
        $exited  = $proc.WaitForExit($tmo * 1000)
        $sw.Stop()

        if (-not $exited) {
            try { $proc.Kill() } catch {}
            $proc.WaitForExit() | Out-Null
            $outTask.Wait()
            $errTask.Wait()
            Set-Content -Path $log         -Value $outTask.Result -Encoding utf8
            Set-Content -Path ($log+'.err') -Value $errTask.Result -Encoding utf8
            Write-Host ("TIMEOUT ({0}s)" -f $tmo) -ForegroundColor Yellow
            $results += [pscustomobject]@{
                Sample = $name; Status = 'TIMEOUT'; ExitCode = $null; Seconds = $sw.Elapsed.TotalSeconds
            }
            continue
        }

        # Process exited within the deadline. Finish draining the streams
        # and capture exit code (now stable).
        $outTask.Wait()
        $errTask.Wait()
        Set-Content -Path $log         -Value $outTask.Result -Encoding utf8
        Set-Content -Path ($log+'.err') -Value $errTask.Result -Encoding utf8
        $code = $proc.ExitCode

        if ($code -eq 0) {
            Write-Host ("OK ({0:N2}s)" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
            $results += [pscustomobject]@{
                Sample = $name; Status = 'OK'; ExitCode = 0; Seconds = $sw.Elapsed.TotalSeconds
            }
        } else {
            Write-Host ("FAIL exit={0}" -f $code) -ForegroundColor Red
            $results += [pscustomobject]@{
                Sample = $name; Status = 'FAIL'; ExitCode = $code; Seconds = $sw.Elapsed.TotalSeconds
            }
        }
    } catch {
        $sw.Stop()
        Write-Host ("ERROR: {0}" -f $_.Exception.Message) -ForegroundColor Red
        $results += [pscustomobject]@{
            Sample = $name; Status = 'ERROR'; ExitCode = $null; Seconds = $sw.Elapsed.TotalSeconds
        }
    } finally {
        if ($null -ne $proc) { $proc.Dispose() }
    }
}

$wall = (Get-Date) - $startWall
$pass  = ($results | Where-Object { $_.Status -eq 'OK' }).Count
$fail  = ($results | Where-Object { $_.Status -ne 'OK' }).Count
$total = $results.Count

Write-Host ''
Write-Host ('=== smoke-test summary ============================================')
Write-Host ('  build dir : {0}' -f (Resolve-Path $BuildDir))
Write-Host ('  binaries  : {0}' -f $sampleDir)
Write-Host ('  log dir   : {0}' -f (Resolve-Path $LogDir))
Write-Host ('  frames    : {0}' -f $Frames)
Write-Host ('  timeout   : {0}s (per sample)' -f $TimeoutSec)
Write-Host ('  wall time : {0:N2}s' -f $wall.TotalSeconds)
$summaryColor = 'Red'
if ($fail -eq 0) { $summaryColor = 'Green' }
Write-Host ('  result    : {0}/{1} passed, {2} failed' -f $pass, $total, $fail) -ForegroundColor $summaryColor
Write-Host '==================================================================='

if ($fail -gt 0) {
    Write-Host ''
    Write-Host 'Failed samples:' -ForegroundColor Red
    $results | Where-Object { $_.Status -ne 'OK' } | ForEach-Object {
        Write-Host ('  - {0,-32} {1,-8} exit={2}  log={3}' -f `
            $_.Sample, $_.Status, $_.ExitCode, (Join-Path $LogDir ($_.Sample -replace '\.exe$','.log')))
    }
    exit 1
}

exit 0
