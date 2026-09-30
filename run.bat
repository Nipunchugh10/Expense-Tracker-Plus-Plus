@echo off
REM Builds (incrementally) and starts Expense Tracker Plus Plus. Never launches a stale build.
setlocal
cd /d "%~dp0"

if not exist "build\CMakeCache.txt" (
    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto :failed
)
cmake --build build -j
if errorlevel 1 goto :failed

echo Starting Expense Tracker Plus Plus...
start "" "build\bin\Expense_Tracker_Plus_Plus.exe"
exit /b 0

:failed
echo Build FAILED - not starting the app.
exit /b 1
