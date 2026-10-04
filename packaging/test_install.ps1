# LOCAL TESTING ONLY. Makes a short-lived self-signed test certificate, signs a COPY of the package
# with it, and exports the certificate so Windows can be told to trust it. Never upload the signed copy:
# upload the original unsigned .msix from build_msix.ps1 (the Store signs that one).
#
#   1. powershell -ExecutionPolicy Bypass -File packaging\test_install.ps1          (makes + signs the test copy)
#   2. In an ADMINISTRATOR PowerShell:  Import-Certificate -FilePath <path to ETPP_test.cer> -CertStoreLocation Cert:\LocalMachine\TrustedPeople
#   3. powershell -ExecutionPolicy Bypass -File packaging\test_install.ps1 -Install  (installs the test copy)
#   4. When finished:                   powershell -ExecutionPolicy Bypass -File packaging\test_install.ps1 -Remove
param([switch]$Install, [switch]$Remove)
$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "out"
$publisher = ([xml](Get-Content (Join-Path $PSScriptRoot "AppxManifest.xml") -Raw)).Package.Identity.Publisher
$identity  = ([xml](Get-Content (Join-Path $PSScriptRoot "AppxManifest.xml") -Raw)).Package.Identity.Name
$friendly  = "ETPP local test certificate (delete after testing)"
$cerPath   = Join-Path $out "ETPP_test.cer"

if ($Remove) {
    Get-AppxPackage -Name $identity | Remove-AppxPackage
    Get-ChildItem Cert:\CurrentUser\My | Where-Object FriendlyName -eq $friendly | Remove-Item
    Get-ChildItem Cert:\LocalMachine\TrustedPeople -ErrorAction SilentlyContinue | Where-Object FriendlyName -eq $friendly |
        Remove-Item -ErrorAction SilentlyContinue
    Write-Host "Removed the test app (its data too) and the test certificate from the current-user store."
    Write-Host "If the certificate is still in Local Machine > Trusted People, remove it from an administrator PowerShell:"
    Write-Host "  Get-ChildItem Cert:\LocalMachine\TrustedPeople | Where-Object FriendlyName -eq '$friendly' | Remove-Item"
    return
}

$msix = Get-ChildItem $out -Filter "ExpenseTrackerPlusPlus_*_x64.msix" | Where-Object Name -notlike "*_test*" |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $msix) { throw "No package found. Run packaging\build_msix.ps1 first." }
$testMsix = Join-Path $out ($msix.BaseName + "_test.msix")

if ($Install) {
    if (-not (Test-Path $testMsix)) { throw "Make the signed test copy first (run without -Install)." }
    Add-AppxPackage -Path $testMsix
    Write-Host "Installed. Start it from the Start menu (search for the app name)." -ForegroundColor Green
    return
}

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.FriendlyName -eq $friendly -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature -FriendlyName $friendly `
        -CertStoreLocation "Cert:\CurrentUser\My" -NotAfter (Get-Date).AddDays(30) `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
}
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

$signtool = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin") -Recurse -Filter signtool.exe |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { throw "signtool.exe not found (Windows SDK)." }
Copy-Item $msix.FullName $testMsix -Force
& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint $testMsix
if ($LASTEXITCODE -ne 0) { throw "signtool failed (exit code $LASTEXITCODE)" }

Write-Host "`nSigned test copy: $testMsix" -ForegroundColor Green
Write-Host "Certificate to trust: $cerPath"
Write-Host "Next, in an ADMINISTRATOR PowerShell run:"
Write-Host "  Import-Certificate -FilePath '$cerPath' -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
