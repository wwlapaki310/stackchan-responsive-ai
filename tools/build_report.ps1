# Builds the submission report PDF from docs/REPORT.md (pandoc -> HTML -> Edge headless print).
#   powershell -File tools/build_report.ps1 -Account <omnicampus account> [-VideoUrl <url>]
# Output: submission/説明資料_PAI最終課題_<Account>.pdf (git-ignored)
param(
  [Parameter(Mandatory = $true)][string]$Account,
  [string]$VideoUrl = '（YouTubeのURLをここに入れる）'
)
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$out = Join-Path $root 'submission'
New-Item -ItemType Directory -Force $out | Out-Null
$pandoc = (Get-Command pandoc -ErrorAction SilentlyContinue).Source
if (-not $pandoc) { $pandoc = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter pandoc.exe | Select-Object -First 1).FullName }
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$md = Join-Path $out 'report.md'
(Get-Content (Join-Path $root 'docs\REPORT.md') -Raw -Encoding UTF8).Replace('{{VIDEO_URL}}', $VideoUrl) | Set-Content $md -Encoding UTF8
$css = Join-Path $out 'report.css'
@'
body { font-family: "Yu Gothic", "Meiryo", sans-serif; font-size: 10.5pt; line-height: 1.55; margin: 0; }
h1 { font-size: 15pt; margin: 0 0 6pt 0; }
h2 { break-after: avoid; font-size: 12pt; border-bottom: 1px solid #888; margin: 12pt 0 4pt 0; }
table { border-collapse: collapse; width: 100%; font-size: 9.5pt; }
td, th { border: 1px solid #999; padding: 2pt 5pt; }
img { max-width: 100%; }
header h1.title { display: none; }
ul, ol { margin: 2pt 0; padding-left: 18pt; }
p { margin: 3pt 0; }
'@ | Set-Content $css -Encoding UTF8
$html = Join-Path $out 'report.html'
Push-Location (Join-Path $root 'docs')
& $pandoc $md -s --metadata title=' ' -c $css -o $html --resource-path=. --embed-resources 2>&1 | Out-Null
Pop-Location
$pdf = Join-Path $out ("説明資料_PAI最終課題_{0}.pdf" -f $Account)
& $edge --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="$pdf" ("file:///" + ($html -replace '\\', '/')) 2>&1 | Out-Null
Write-Output ("wrote {0}" -f $pdf)
