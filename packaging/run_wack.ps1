# Runs the Windows App Certification Kit (WACK) on the signed TEST copy of the package: a preview of the
# checks Microsoft runs before it publishes an app. Must be run from an ADMINISTRATOR PowerShell.
#   powershell -ExecutionPolicy Bypass -File packaging\run_wack.ps1
# It takes about 5-15 minutes and starts/stops the app by itself. Do not use the PC's mouse and keyboard in the
# app meanwhile. The report is written to packaging\out\wack_report.xml and summarised at the end.
$ErrorActionPreference = "Stop"
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "Run this from an administrator PowerShell (right-click > Run as administrator)." }
if (Get-Process Expense_Tracker_Plus_Plus -ErrorAction SilentlyContinue) { throw "Close Expense Tracker Plus Plus first." }

$out = Join-Path $PSScriptRoot "out"
$pkg = Get-ChildItem $out -Filter "ExpenseTrackerPlusPlus_*_x64_test.msix" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $pkg) { throw "No signed test package found. Run packaging\test_install.ps1 first." }
$report = Join-Path $out "wack_report.xml"
if (Test-Path $report) { Remove-Item $report -Force }

# WACK installs the package itself, so remove the test install first (your data folder is NOT touched).
Get-AppxPackage -Name NipunChugh.ExpenseTrackerPlusPlus | Remove-AppxPackage

$appcert = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\App Certification Kit\appcert.exe"
& $appcert reset
& $appcert test -appxpackagepath $pkg.FullName -reportoutputpath $report

if (-not (Test-Path $report)) { throw "WACK did not produce a report." }
Write-Host "`n=== WACK RESULT ===" -ForegroundColor Cyan
try {
    [xml]$x = Get-Content $report -Raw
    Write-Host ("Overall result: " + $x.REPORT.OVERALL_RESULT)
    $x.SelectNodes("//TEST") | ForEach-Object {
        $r = $_.SelectSingleNode("RESULT"); $res = if ($r) { $r.InnerText.Trim() } else { "?" }
        "{0,-8} {1}" -f $res, $_.GetAttribute("NAME")
    } | Sort-Object
} catch { Write-Host "Could not summarise the report ($($_.Exception.Message)). Open the file below instead." }
Write-Host "`nFull report: $report"
