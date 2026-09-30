# Builds (incrementally) and starts Expense Tracker Plus Plus. Never launches a stale build.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not (Test-Path "build\CMakeCache.txt")) {
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { Write-Host "Configure FAILED - not starting the app." -ForegroundColor Red; exit 1 }
}
cmake --build build -j
if ($LASTEXITCODE -ne 0) { Write-Host "Build FAILED - not starting the app." -ForegroundColor Red; exit 1 }

Write-Host "==> Starting Expense Tracker Plus Plus..." -ForegroundColor Green
Start-Process "build\bin\Expense_Tracker_Plus_Plus.exe"
