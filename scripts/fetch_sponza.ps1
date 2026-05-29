# =============================================================================
# fetch_sponza.ps1
# Download Khronos glTF-Sample-Models 2.0 Sponza into assets/samples/Sponza/
#
# Usage (from repo root):
#   powershell -ExecutionPolicy Bypass -File scripts/fetch_sponza.ps1
#
# The script is idempotent: already-present files are skipped.
# =============================================================================

$BaseUrl  = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/main/2.0/Sponza/glTF"
$DestDir  = Join-Path $PSScriptRoot "..\assets\samples\Sponza"
$DestDir  = [System.IO.Path]::GetFullPath($DestDir)

$Files = @(
    "10381718147657362067.jpg",
    "10388182081421875623.jpg",
    "11474523244911310074.jpg",
    "11490520546946913238.jpg",
    "11872827283454512094.jpg",
    "11968150294050148237.jpg",
    "1219024358953944284.jpg",
    "12501374198249454378.jpg",
    "13196865903111448057.jpg",
    "13824894030729245199.jpg",
    "13982482287905699490.jpg",
    "14118779221266351425.jpg",
    "14170708867020035030.jpg",
    "14267839433702832875.jpg",
    "14650633544276105767.jpg",
    "15295713303328085182.jpg",
    "15722799267630235092.jpg",
    "16275776544635328252.png",
    "16299174074766089871.jpg",
    "16885566240357350108.jpg",
    "17556969131407844942.jpg",
    "17876391417123941155.jpg",
    "2051777328469649772.jpg",
    "2185409758123873465.jpg",
    "2299742237651021498.jpg",
    "2374361008830720677.jpg",
    "2411100444841994089.jpg",
    "2775690330959970771.jpg",
    "2969916736137545357.jpg",
    "332936164838540657.jpg",
    "3371964815757888145.jpg",
    "3455394979645218238.jpg",
    "3628158980083700836.jpg",
    "3827035219084910048.jpg",
    "4477655471536070370.jpg",
    "4601176305987539675.jpg",
    "466164707995436622.jpg",
    "4675343432951571524.jpg",
    "4871783166746854860.jpg",
    "4910669866631290573.jpg",
    "4975155472559461469.jpg",
    "5061699253647017043.png",
    "5792855332885324923.jpg",
    "5823059166183034438.jpg",
    "6047387724914829168.jpg",
    "6151467286084645207.jpg",
    "6593109234861095314.jpg",
    "6667038893015345571.jpg",
    "6772804448157695701.jpg",
    "7056944414013900257.jpg",
    "715093869573992647.jpg",
    "7268504077753552595.jpg",
    "7441062115984513793.jpg",
    "755318871556304029.jpg",
    "759203620573749278.jpg",
    "7645212358685992005.jpg",
    "7815564343179553343.jpg",
    "8006627369776289000.png",
    "8051790464816141987.jpg",
    "8114461559286000061.jpg",
    "8481240838833932244.jpg",
    "8503262930880235456.jpg",
    "8747919177698443163.jpg",
    "8750083169368950601.jpg",
    "8773302468495022225.jpg",
    "8783994986360286082.jpg",
    "9288698199695299068.jpg",
    "9916269861720640319.jpg",
    "Sponza.bin",
    "Sponza.gltf",
    "white.png"
)

if (-not (Test-Path $DestDir)) {
    New-Item -ItemType Directory -Path $DestDir | Out-Null
}

$Total   = $Files.Count
$Done    = 0
$Skipped = 0
$Failed  = 0

foreach ($File in $Files) {
    $Dest = Join-Path $DestDir $File
    if (Test-Path $Dest) {
        $Skipped++
        continue
    }
    $Url = "$BaseUrl/$File"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing -ErrorAction Stop
        $Done++
        Write-Host "  OK  $File"
    } catch {
        $Failed++
        Write-Warning "FAIL $File : $_"
    }
}

Write-Host ""
Write-Host "fetch_sponza: $Done downloaded, $Skipped skipped, $Failed failed (total=$Total)"
if ($Failed -gt 0) {
    Write-Warning "Some files failed. Re-run the script to retry."
    exit 1
}
Write-Host "Sponza assets ready at: $DestDir"
