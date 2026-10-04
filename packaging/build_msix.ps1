# Builds the Microsoft Store package (.msix) from a built executable.
# The Store signs the package for you, so the file this makes is UNSIGNED and is what you upload.
# (To install it on your own PC for testing it must be signed with a test certificate; see test_install.ps1.)
#
# Usage (after building the app in Release mode):
#   powershell -ExecutionPolicy Bypass -File packaging\build_msix.ps1
#   powershell -ExecutionPolicy Bypass -File packaging\build_msix.ps1 -DisplayName "Exact reserved name"
#
# Needs makeappx.exe from the Windows SDK (winget install Microsoft.WindowsSDK.10.0.26100).
param(
    [string]$Exe = "",
    [string]$DisplayName = "Expense Tracker Plus Plus",   # MUST equal the name reserved in Partner Center
    [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe)    { $Exe = Join-Path $root "build\bin\Expense_Tracker_Plus_Plus.exe" }
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot "out" }
if (-not (Test-Path $Exe)) { throw "Executable not found: $Exe  (build the app first: build.bat)" }

# Version comes from the single source of truth: include\AppInfo.h  ->  "2.0.1" becomes "2.0.1.0".
$info = Get-Content (Join-Path $root "include\AppInfo.h") -Raw
if ($info -notmatch '#define\s+ETP_VERSION_STRING\s+"(\d+)\.(\d+)\.(\d+)"') { throw "Cannot read ETP_VERSION_STRING from include\AppInfo.h" }
$version = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"   # the Store requires the 4th part to be 0
Write-Host "Version $version, display name '$DisplayName'"

# Find makeappx.exe (newest Windows SDK first).
$makeappx = Get-Command makeappx.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if (-not $makeappx) {
    $kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $makeappx = Get-ChildItem $kits -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $makeappx) { throw "makeappx.exe not found. Install the Windows SDK: winget install Microsoft.WindowsSDK.10.0.26100" }
Write-Host "Using $makeappx"

# Stage exactly what goes into the package.
$stage = Join-Path $OutDir "stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $Exe (Join-Path $stage "Expense_Tracker_Plus_Plus.exe")
Copy-Item (Join-Path $root "LICENSE") $stage
Copy-Item (Join-Path $root "THIRD_PARTY_NOTICES.txt") $stage
Copy-Item (Join-Path $PSScriptRoot "Assets") (Join-Path $stage "Assets") -Recurse

$manifest = (Get-Content (Join-Path $PSScriptRoot "AppxManifest.xml") -Raw).Replace("{{VERSION}}", $version).Replace("{{DISPLAYNAME}}", [System.Security.SecurityElement]::Escape($DisplayName))
[xml]$check = $manifest   # fails here if the manifest is not well-formed
[System.IO.File]::WriteAllText((Join-Path $stage "AppxManifest.xml"), $manifest, (New-Object System.Text.UTF8Encoding $false))

$msix = Join-Path $OutDir "ExpenseTrackerPlusPlus_${version}_x64.msix"
if (Test-Path $msix) { Remove-Item $msix -Force }
& $makeappx pack /d $stage /p $msix /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed (exit code $LASTEXITCODE)" }

Write-Host "`nBuilt $msix" -ForegroundColor Green
Write-Host "SHA-256: $((Get-FileHash $msix -Algorithm SHA256).Hash)"
Write-Host "Upload this file in Partner Center > your app > Packages. It is unsigned on purpose: the Store signs it."
