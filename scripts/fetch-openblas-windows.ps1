# Downloads the pinned OpenBLAS x64 (LP64) release for the native Windows build
# and checks it against recorded SHA-256 hashes (docs/adr/0022 section 4).
#
#   scripts\fetch-openblas-windows.ps1 [-Destination <dir>]
#
# Prints the unpacked directory, which CMake takes as MENOS_OPENBLAS_ROOT. Does
# nothing if a verified copy is already there.
param(
    [string]$Destination = (Join-Path $PSScriptRoot "..\build\_deps")
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Version = "0.3.34"
$ZipName = "OpenBLAS-$Version-x64.zip"
$ZipUrl = "https://github.com/OpenMathLib/OpenBLAS/releases/download/v$Version/$ZipName"
$ZipSha256 = "E9CB6134541F36C27346D5FC5995652F060FBA227CEBBBABCBDA5A5A44D7C76B"
$LicenseUrl = "https://raw.githubusercontent.com/OpenMathLib/OpenBLAS/v$Version/LICENSE"
$LicenseSha256 = "190B5A9C8D9723FE958AD33916BD7346D96FAB3C5EA90832BB02D854F620FCFF"

function Get-Verified([string]$Url, [string]$Path, [string]$Sha256) {
    if (-not (Test-Path $Path) -or (Get-FileHash $Path -Algorithm SHA256).Hash -ne $Sha256) {
        Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
        $actual = (Get-FileHash $Path -Algorithm SHA256).Hash
        if ($actual -ne $Sha256) {
            Remove-Item $Path -Force
            throw "SHA-256 mismatch for ${Url}: expected $Sha256, got $actual"
        }
    }
}

New-Item -ItemType Directory -Force $Destination | Out-Null
$Destination = (Resolve-Path $Destination).Path
$Root = Join-Path $Destination "openblas-$Version"
$Zip = Join-Path $Destination $ZipName

Get-Verified $ZipUrl $Zip $ZipSha256
if (-not (Test-Path (Join-Path $Root "bin\libopenblas.dll"))) {
    Expand-Archive $Zip -DestinationPath $Root -Force
}
Get-Verified $LicenseUrl (Join-Path $Root "LICENSE") $LicenseSha256

Write-Output $Root
