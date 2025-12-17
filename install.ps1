#!/usr/bin/env pwsh
# Dawn installer for Windows
# Usage: irm https://raw.githubusercontent.com/andrewmd5/dawn/main/install.ps1 | iex

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repo = "andrewmd5/dawn"
$installDir = "$env:USERPROFILE\.dawn\bin"

function Write-Status($icon, $msg) { Write-Host "$icon " -NoNewline; Write-Host $msg }
function Write-Err($msg) { Write-Host "`u{274C} " -NoNewline; Write-Host $msg -ForegroundColor Red; exit 1 }

function Initialize-Environment {
    if ($PSVersionTable.PSVersion.Major -lt 5) {
        Write-Err "PowerShell 5 or later is required. https://aka.ms/pscore6"
    }
    try {
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    } catch {
        Write-Err "Unable to set TLS 1.2. Upgrade to .NET Framework 4.5+ and PowerShell v3+"
    }
}

function Get-Architecture {
    try {
        $a = [System.Reflection.Assembly]::LoadWithPartialName("System.Runtime.InteropServices.RuntimeInformation")
        $t = $a.GetType("System.Runtime.InteropServices.RuntimeInformation")
        $p = $t.GetProperty("OSArchitecture")
        switch ($p.GetValue($null).ToString()) {
            "X64"   { return "x64" }
            "Arm64" { return "arm64" }
            default { Write-Err "Unsupported architecture: $_" }
        }
    } catch {
        if ([System.Environment]::Is64BitOperatingSystem) { return "x64" }
        Write-Err "32-bit Windows is not supported"
    }
}

# Initialize
Write-Host ""
Write-Status "`u{1F50D}" "Checking environment..."
Initialize-Environment
$arch = Get-Architecture

# Fetch release
Write-Status "`u{1F310}" "Fetching latest release..."
try {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest"
} catch {
    if ($_.Exception.Response.StatusCode -eq 404) {
        Write-Err "No releases found. Check https://github.com/$repo/releases"
    }
    Write-Err "Failed to fetch release info: $($_.Exception.Message)"
}

$version = $release.tag_name
$asset = $release.assets | Where-Object { $_.name -eq "dawn-windows-$arch.zip" }
if (-not $asset) { Write-Err "No Windows $arch build available for $version" }

# Download
Write-Status "`u{1F4E6}" "Downloading dawn $version for Windows $arch..."
$zipPath = "$env:TEMP\dawn-windows-$arch.zip"
try {
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath
} catch {
    Write-Err "Download failed: $($_.Exception.Message)"
}

# Install
Write-Status "`u{1F527}" "Installing..."
try {
    New-Item -ItemType Directory -Force -Path $installDir | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $installDir -Force
} catch {
    Write-Err "Installation failed: $($_.Exception.Message)"
} finally {
    Remove-Item $zipPath -ErrorAction SilentlyContinue
}

if (-not (Test-Path "$installDir\dawn.exe")) {
    Write-Err "Installation failed: dawn.exe not found"
}

# Update PATH
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$installDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$installDir", "User")
    Write-Status "`u{1F4DD}" "Added to PATH: $installDir"
}
$env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")

# Done
Write-Host ""
Write-Status "`u{2705}" "dawn $version installed successfully!"
Write-Host ""
Write-Host "  Run " -NoNewline
Write-Host "dawn" -ForegroundColor Cyan -NoNewline
Write-Host " to get started."
Write-Host ""
