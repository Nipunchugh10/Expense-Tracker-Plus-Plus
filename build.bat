@echo off
REM Builds Expense Tracker Plus Plus and its tests with CMake + MinGW.
setlocal
cd /d "%~dp0"

cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :failed
cmake --build build -j
if errorlevel 1 goto :failed

echo.
echo Build succeeded: build\bin\Expense_Tracker_Plus_Plus.exe
echo Run the tests with: ctest --test-dir build --output-on-failure
exit /b 0

:failed
echo.
echo Build FAILED.
exit /b 1
