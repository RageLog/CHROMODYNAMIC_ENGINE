# Auto-locates each tinted primitive in the rendered viewport and
# reports its measured RGB vs the expected reference colour. Works by
# searching for dominant-channel pixel clusters rather than relying
# on hard-coded sample coords (which drift if the camera moves).
#
# Usage:
#   powershell -File sample_pixels.ps1 <png_path>

param(
    [Parameter(Mandatory=$true)][string]$Png
)

Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Image]::FromFile($Png)
"Image: $($bmp.Width) x $($bmp.Height)"
""

# Lock the bitmap once for fast pixel access (GetPixel is otherwise
# very slow when scanning a million pixels).
$rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                      [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$ptr = $data.Scan0
$len = $stride * $bmp.Height
$bytes = New-Object byte[] $len
[System.Runtime.InteropServices.Marshal]::Copy($ptr, $bytes, 0, $len)
$bmp.UnlockBits($data)

$W = $bmp.Width; $H = $bmp.Height

function Sample-Region($name, $expected, $predicate) {
    $sumR=0.0; $sumG=0.0; $sumB=0.0; $count=0
    $minX=$W; $minY=$H; $maxX=0; $maxY=0
    # Skip top 8% (toolbar) and right 25% (Inspector panel) and
    # left 12% (Outliner/Scene) so we don't catch UI chrome.
    $x0 = [int]($W * 0.18); $x1 = [int]($W * 0.72)
    $y0 = [int]($H * 0.55); $y1 = [int]($H * 0.92)
    for ($y = $y0; $y -lt $y1; $y++) {
        $row = $y * $stride
        for ($x = $x0; $x -lt $x1; $x++) {
            $i = $row + $x * 4
            $b = $bytes[$i] / 255.0
            $g = $bytes[$i + 1] / 255.0
            $r = $bytes[$i + 2] / 255.0
            if (& $predicate $r $g $b) {
                $sumR += $r; $sumG += $g; $sumB += $b; $count++
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($count -lt 50) {
        "  ${name}: NOT FOUND (only $count pixels match predicate)"
        ""
        return
    }
    $r = $sumR / $count; $g = $sumG / $count; $b = $sumB / $count
    $luma = 0.299 * $r + 0.587 * $g + 0.114 * $b
    $wash = 0.0
    for ($i = 0; $i -lt 3; $i++) {
        $samp = @($r,$g,$b)[$i]
        if ($expected[$i] -lt 0.05) { $wash = [math]::Max($wash, $samp) }
    }
    $sat = [math]::Max([math]::Max($r,$g), $b) - [math]::Min([math]::Min($r,$g), $b)
    $err = [math]::Sqrt(([math]::Pow($r-$expected[0],2) + [math]::Pow($g-$expected[1],2) + [math]::Pow($b-$expected[2],2)) / 3.0)
    "  $name  (n=$count, bbox $minX..$maxX x $minY..$maxY)"
    "    measured RGB: ({0:F2}, {1:F2}, {2:F2})  luma {3:F2}" -f $r, $g, $b, $luma
    "    expected RGB: ({0:F2}, {1:F2}, {2:F2})" -f $expected[0], $expected[1], $expected[2]
    "    RMSE: {0:F3}   white-wash: {1:F2}   saturation: {2:F2}" -f $err, $wash, $sat
    ""
}

# Red (cube): R dominant by 1.7x over G & B
Sample-Region "Cube     (red)"     @(1.0, 0.0, 0.0) { param($r,$g,$b) ($r -gt 0.25) -and ($r -gt 1.7 * ($g + 0.02)) -and ($r -gt 1.7 * ($b + 0.02)) }
# Green (sphere): G dominant
Sample-Region "Sphere   (green)"   @(0.0, 1.0, 0.0) { param($r,$g,$b) ($g -gt 0.25) -and ($g -gt 1.7 * ($r + 0.02)) -and ($g -gt 1.7 * ($b + 0.02)) }
# Blue (cone): B dominant
Sample-Region "Cone     (blue)"    @(0.0, 0.0, 1.0) { param($r,$g,$b) ($b -gt 0.25) -and ($b -gt 1.7 * ($r + 0.02)) -and ($b -gt 1.7 * ($g + 0.02)) }
# Yellow (cylinder): R + G high, B low
Sample-Region "Cylinder (yellow)"  @(1.0, 1.0, 0.0) { param($r,$g,$b) ($r -gt 0.30) -and ($g -gt 0.30) -and ($b -lt 0.20) -and ([math]::Abs($r-$g) -lt 0.15) }
# Magenta (torus): R + B high, G low
Sample-Region "Torus    (magenta)" @(1.0, 0.0, 1.0) { param($r,$g,$b) ($r -gt 0.30) -and ($b -gt 0.30) -and ($g -lt 0.20) -and ([math]::Abs($r-$b) -lt 0.15) }

$bmp.Dispose()
