# =====================================================================
#  tools/windows/setup-env.ps1 -- Windows development environment setup
# =====================================================================
#
#  Checks for required tools and offers to install anything missing.
#  Handles MSYS2 download/install, UCRT64 packages, PATH config,
#  repository clone, and vcpkg bootstrap.
#
#  Can be run from inside a git clone or downloaded standalone.
#  When run standalone, it offers to clone the repository after
#  the toolchain is set up.
#
#  The recommended toolchain is MSYS2 with MinGW-w64 (GCC).  For
#  Visual Studio / MSVC, see docs/dev_environment_setup.txt Section
#  14.1 Option B and set up manually.
#
#  Run with:
#    powershell -ExecutionPolicy Bypass -File tools\windows\setup-env.ps1
#
#  AI Attribution: This script was primarily generated with the
#                  assistance of Claude AI (Anthropic).  Human review
#                  and editorial direction by the project author.
#
#  SPDX-License-Identifier: GPL-3.0-only
#
# =====================================================================

<#
.SYNOPSIS
    HobbyCAD Windows development environment setup.

.DESCRIPTION
    See docs/dev_environment_setup.txt Sections 13-18 for full details.
    Can be run from inside a git clone or downloaded standalone.

.PARAMETER VcpkgRoot
    Where to clone/find vcpkg.  Default: C:\vcpkg

.PARAMETER Msys2Root
    Where to find/install MSYS2.  Default: C:\msys64

.PARAMETER RepoUrl
    HobbyCAD git repository URL.
    Default: https://github.com/ayourk/hobbycad.git

.PARAMETER CloneDir
    Where to clone HobbyCAD (standalone mode only).
    Prompted interactively if not specified.

.PARAMETER SkipVcpkg
    Skip vcpkg setup entirely.

.PARAMETER Uninstall
    Roll back changes made by this script:
    removes the UCRT64 bin directory from the user PATH,
    deletes the VCPKG_ROOT and HOBBYCAD_REPO user environment
    variables, runs the MSYS2 uninstaller if found, and offers
    to delete the vcpkg and HobbyCAD clone directories.

.PARAMETER NonInteractive
    Answer yes to all prompts (unattended install).
#>

param(
    [string]$VcpkgRoot = "C:\vcpkg",
    [string]$Msys2Root = "C:\msys64",
    [string]$RepoUrl =
        "https://github.com/ayourk/hobbycad.git",
    [string]$CloneDir = "",
    [switch]$SkipVcpkg,
    [switch]$Uninstall,
    [switch]$NonInteractive,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

# --- Helpers -----------------------------------------------------------

function Write-Header($msg) {
    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  $msg" -ForegroundColor Cyan
    Write-Host ("=" * 60) -ForegroundColor Cyan
}

function Write-Ok($msg)   {
    Write-Host "  [OK]   $msg" -ForegroundColor Green
}

function Write-Warn($msg) {
    Write-Host "  [WARN] $msg" -ForegroundColor Yellow
}

function Write-Fail($msg) {
    Write-Host "  [FAIL] $msg" -ForegroundColor Red
}

function Write-Info($msg) {
    Write-Host "  [INFO] $msg" -ForegroundColor Gray
}

function Test-Command($cmd) {
    $null = Get-Command $cmd -ErrorAction SilentlyContinue
    return $?
}

# Returns version string from --version output.  Accepts a bare
# command name (looked up via PATH) or a full path to an exe.
function Get-CommandVersion($cmd) {
    try {
        $out = & $cmd --version 2>&1 |
            Select-Object -First 1
        return $out.ToString().Trim()
    } catch {
        return $null
    }
}

# Returns the full path to a tool.  Checks PATH first, then
# falls back to a known filesystem path.  Returns $null if
# neither works.
function Resolve-Exe($name, $knownPath) {
    $found = Get-Command $name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    if ($knownPath -and (Test-Path $knownPath)) {
        return $knownPath
    }
    return $null
}

function Confirm-Action($prompt) {
    if ($NonInteractive) { return $true }
    $answer = Read-Host "  $prompt (Y/n)"
    return (
        $answer -eq '' -or
        $answer -eq 'y' -or
        $answer -eq 'Y'
    )
}

function Get-TempDownload($url, $filename) {
    $dest = Join-Path $env:TEMP $filename
    Write-Info "Downloading $filename..."
    try {
        $protocol = [Net.SecurityProtocolType]::Tls12
        [Net.ServicePointManager]::SecurityProtocol = $protocol
        Invoke-WebRequest -Uri $url -OutFile $dest `
            -UseBasicParsing
        return $dest
    } catch {
        Write-Fail "Download failed: $_"
        return $null
    }
}

# Adds a directory to the persistent user PATH (registry) and
# updates the current session so tools are available immediately.
# Uses Set-ItemProperty to preserve the REG_EXPAND_SZ value kind
# so that %USERPROFILE% and similar variables are not expanded.
function Add-ToUserPath($dir) {
    $regKey = 'HKCU:\Environment'
    $rawPath = (Get-Item $regKey).GetValue(
        'Path', '', 'DoNotExpandEnvironmentNames'
    )
    $entries = $rawPath -split ";" |
        Where-Object { $_ -ne "" }
    if ($entries | Where-Object { $_ -eq $dir }) {
        Write-Info "$dir is already in user PATH."
        return
    }
    $newPath = ($rawPath.TrimEnd(";") + ";$dir;")
    Set-ItemProperty -Path $regKey -Name 'Path' `
        -Value $newPath `
        -Type ExpandString
    # Update the running session so subsequent checks see it
    $env:Path = "$env:Path;$dir"
    Write-Ok "Added $dir to user PATH (registry + session)."
    Write-Info "New terminal windows will inherit this."
}

function Invoke-Msys2Command($Msys2Root, $command) {
    $shell = Join-Path $Msys2Root "msys2_shell.cmd"
    $args  = "-ucrt64 -defterm -no-start"
    $args += " -c `"$command`""
    Start-Process -FilePath $shell `
        -ArgumentList $args `
        -Wait -NoNewWindow
}

# Query the GitHub API for the latest stable MSYS2 installer.
# Falls back to repo.msys2.org if the API call fails.
function Get-Msys2InstallerUrl {
    try {
        $protocol = [Net.SecurityProtocolType]::Tls12
        [Net.ServicePointManager]::SecurityProtocol = $protocol
        $headers = @{
            "Accept" = "application/vnd.github+json"
        }
        $apiUrl = "https://api.github.com/repos" +
            "/msys2/msys2-installer/releases"
        $releases = Invoke-RestMethod `
            -Uri $apiUrl `
            -Headers $headers -UseBasicParsing

        # Find newest non-prerelease, non-nightly release
        foreach ($rel in $releases) {
            if ($rel.prerelease) { continue }
            if ($rel.tag_name -match "nightly") { continue }

            foreach ($asset in $rel.assets) {
                $pattern = "^msys2-x86_64-\d+\.exe$"
                if ($asset.name -match $pattern) {
                    return @{
                        Url  = $asset.browser_download_url
                        Name = $asset.name
                        Tag  = $rel.tag_name
                    }
                }
            }
        }
    } catch {
        Write-Warn "GitHub API request failed: $_"
    }

    # Fallback: direct link (redirects to latest stable)
    $fallbackUrl = "https://repo.msys2.org" +
        "/distrib/x86_64/msys2-x86_64-latest.exe"
    return @{
        Url  = $fallbackUrl
        Name = "msys2-x86_64-latest.exe"
        Tag  = "latest"
    }
}

# Searches the Add/Remove Programs registry keys for an MSYS2
# installation.  Returns a hashtable with:
#   Path      - install directory (or $null)
#   UninstCmd - uninstall command string (or $null)
#   RegKey    - the registry key object (or $null)
function Find-Msys2Install {
    $regPaths = @(
        'HKCU:\Software\Microsoft\Windows\' +
            'CurrentVersion\Uninstall\MSYS2*',
        'HKLM:\Software\Microsoft\Windows\' +
            'CurrentVersion\Uninstall\MSYS2*',
        'HKLM:\Software\WOW6432Node\Microsoft\' +
            'Windows\CurrentVersion\Uninstall\MSYS2*'
    )
    foreach ($rp in $regPaths) {
        $key = Get-Item $rp -ErrorAction SilentlyContinue
        if (-not $key) { continue }

        $cmd = $key.GetValue('UninstallString', $null)
        $loc = $key.GetValue('InstallLocation', $null)

        # Fall back: extract dir from UninstallString
        # e.g. "C:\msys64\uninstall.exe" -> "C:\msys64"
        if (-not $loc -and $cmd) {
            if ($cmd -match '^"?(.+)\\[^\\]+$') {
                $loc = $Matches[1].TrimStart('"')
            }
        }

        return @{
            Path      = $loc
            UninstCmd = $cmd
            RegKey    = $key
        }
    }
    return @{ Path = $null; UninstCmd = $null; RegKey = $null }
}

# --- Detect standalone vs. in-repo ------------------------------------
#
# If this script lives at tools/windows/setup-env.ps1 inside a
# HobbyCAD clone, the repo root is two directories up and will
# contain CMakeLists.txt.  If the script was downloaded on its
# own, that path won't exist.

$repoRoot  = ""
$isInRepo  = $false
$clonePath = ""

$candidateRoot = (Resolve-Path (
    Join-Path $PSScriptRoot "..\.."
) -ErrorAction SilentlyContinue)
if ($candidateRoot) {
    $candidateCML = Join-Path $candidateRoot "CMakeLists.txt"
    if (Test-Path $candidateCML) {
        $content = Get-Content $candidateCML -Raw `
            -ErrorAction SilentlyContinue
        if ($content -and $content -match "hobbycad") {
            $isInRepo = $true
            $repoRoot = $candidateRoot.Path
        }
    }
}

# --- Banner ------------------------------------------------------------

if ($Help) {
    Get-Help $MyInvocation.MyCommand.Path -Detailed
    exit 0
}

Write-Host ""
Write-Host "  HobbyCAD Windows Environment Setup" `
    -ForegroundColor White
Write-Host "  -----------------------------------" `
    -ForegroundColor DarkGray
Write-Host "  MSYS2 root: $Msys2Root" `
    -ForegroundColor DarkGray
Write-Host "  vcpkg root: $VcpkgRoot" `
    -ForegroundColor DarkGray
if ($isInRepo) {
    Write-Host "  Repo root:  $repoRoot" `
        -ForegroundColor DarkGray
} else {
    Write-Host "  Mode:       standalone (not inside a clone)" `
        -ForegroundColor DarkGray
}
Write-Host ""

$allOk     = $true
$needPath  = $false
$ucrt64Bin = Join-Path $Msys2Root "ucrt64\bin"

# ===================================================================
#  UNINSTALL MODE
# ===================================================================
#
# Rolls back changes this script can make:
#   1. Removes MSYS2 ucrt64\bin from HKCU\Environment\Path
#   2. Removes HKCU\Environment\VCPKG_ROOT
#   3. Removes HKCU\Environment\HOBBYCAD_REPO
#   4. Runs the MSYS2 uninstaller (if found)
#   5. Offers to delete vcpkg and HobbyCAD clone directories

if ($Uninstall) {
    Write-Header "Uninstall -- rolling back changes"

    $changed = $false

    # --- Discover MSYS2 location ---------------------------------------
    # If the user installed MSYS2 somewhere other than $Msys2Root,
    # check the Add/Remove Programs registry keys for the actual
    # install path and update our variables accordingly.

    $msys2Info = Find-Msys2Install

    if ($msys2Info.Path -and (Test-Path $msys2Info.Path)) {
        if ($msys2Info.Path -ne $Msys2Root) {
            Write-Info ("MSYS2 install found at: " +
                $msys2Info.Path + " (via registry)")
            $Msys2Root = $msys2Info.Path
            $ucrt64Bin = Join-Path $Msys2Root "ucrt64\bin"
        }
    }

    # --- PATH ----------------------------------------------------------

    Write-Host ""
    Write-Info "Checking HKCU\Environment\Path..."

    $rawPath = (Get-Item 'HKCU:\Environment').GetValue(
        'Path', '', 'DoNotExpandEnvironmentNames'
    )
    # Build a list of MSYS2 ucrt64 paths to remove:
    #   - the $ucrt64Bin derived from -Msys2Root / discovery
    #   - any entry matching the common *msys*\ucrt64\bin pattern
    # This catches entries from non-default install locations
    # even if the registry lookup did not find them.
    $entries = $rawPath -split ";" |
        Where-Object { $_ -ne "" }
    $msysEntries = $entries | Where-Object {
        ($_ -eq $ucrt64Bin) -or
        ($_ -like '*msys*\ucrt64\bin') -or
        ($_ -like '*msys*ucrt64*bin*')
    }
    $filtered = $entries | Where-Object {
        $_ -notin $msysEntries
    }

    if ($msysEntries) {
        # Rejoin with trailing semicolon
        $newPath = ($filtered -join ";")
        if ($newPath -ne "" -and -not $newPath.EndsWith(";")) {
            $newPath += ";"
        }
        $label = ($msysEntries -join ", ")
        Write-Warn "Found MSYS2 in user PATH: $label"
        Write-Info "Only MSYS2 entries will be removed."
        Write-Info "Old value:"
        Write-Info "  $rawPath"
        Write-Info "New value:"
        Write-Info "  $newPath"
        Write-Host ""

        if (Confirm-Action "Remove MSYS2 entries from PATH?") {
            Set-ItemProperty `
                -Path 'HKCU:\Environment' `
                -Name 'Path' `
                -Value $newPath `
                -Type ExpandString
            Write-Ok "Removed from user PATH."
            $changed = $true
        } else {
            Write-Info "Skipped."
        }
    } else {
        Write-Ok "No MSYS2 entries in user PATH. Nothing to do."
    }

    # --- VCPKG_ROOT ----------------------------------------------------

    Write-Host ""
    Write-Info "Checking HKCU\Environment\VCPKG_ROOT..."

    $envRoot = [Environment]::GetEnvironmentVariable(
        "VCPKG_ROOT", "User"
    )
    if ($envRoot) {
        Write-Warn "VCPKG_ROOT is set: $envRoot"
        Write-Info "Old value: $envRoot"
        Write-Info "New value: (removed)"
        Write-Host ""

        if (Confirm-Action "Remove VCPKG_ROOT?") {
            [Environment]::SetEnvironmentVariable(
                "VCPKG_ROOT", $null, "User"
            )
            Write-Ok "VCPKG_ROOT removed."
            $changed = $true
        } else {
            Write-Info "Skipped."
        }
    } else {
        Write-Ok "VCPKG_ROOT is not set. Nothing to do."
    }

    # --- HOBBYCAD_REPO ------------------------------------------------
    #  Also clean up the old HOBBYCAD_CLONE name if present.

    Write-Host ""
    Write-Info "Checking HKCU\Environment\HOBBYCAD_REPO..."

    # Migrate old name silently during uninstall
    $oldClone = [Environment]::GetEnvironmentVariable(
        "HOBBYCAD_CLONE", "User"
    )
    if ($oldClone) {
        [Environment]::SetEnvironmentVariable(
            "HOBBYCAD_CLONE", $null, "User"
        )
        Write-Info "Removed old HOBBYCAD_CLONE entry."
        $changed = $true
    }

    $savedClone = [Environment]::GetEnvironmentVariable(
        "HOBBYCAD_REPO", "User"
    )

    if ($savedClone) {
        Write-Warn "HOBBYCAD_REPO is set: $savedClone"

        if (Test-Path $savedClone) {
            if (Confirm-Action "Delete $savedClone?") {
                Remove-Item -Recurse -Force $savedClone `
                    -ErrorAction SilentlyContinue
                if (-not (Test-Path $savedClone)) {
                    Write-Ok "Deleted $savedClone"
                    $changed = $true
                } else {
                    Write-Fail "Could not fully remove."
                    Write-Info "Delete manually."
                }
            } else {
                Write-Info "Skipped folder deletion."
            }
        } else {
            Write-Info "Directory already gone."
        }

        # Remove the registry entry
        if (Confirm-Action "Remove HOBBYCAD_REPO from registry?") {
            [Environment]::SetEnvironmentVariable(
                "HOBBYCAD_REPO", $null, "User"
            )
            Write-Ok "HOBBYCAD_REPO removed."
            $changed = $true
        } else {
            Write-Info "Skipped."
        }
    } else {
        Write-Ok "HOBBYCAD_REPO is not set. Nothing to do."
    }

    # --- MSYS2 ---------------------------------------------------------

    Write-Host ""
    Write-Info "Checking for MSYS2..."

    if (Test-Path $Msys2Root) {
        # Remove the base MSYS2 git package if present.
        $baseGit = Join-Path $Msys2Root "usr\bin\git.exe"
        if (Test-Path $baseGit) {
            Write-Info "Removing base MSYS2 git package..."
            $rmCmd = "pacman -Rns --noconfirm git"
            Invoke-Msys2Command $Msys2Root $rmCmd
            if (-not (Test-Path $baseGit)) {
                Write-Ok "Base git package removed."
            }
        }

        $uninstExe = Join-Path $Msys2Root "uninstall.exe"

        # Qt Installer Framework CLI uninstall:
        #   uninstall.exe pr --confirm-command
        # Falls back to registry command or GUI if needed.
        if (Test-Path $uninstExe) {
            Write-Warn "MSYS2 found at $Msys2Root"
            Write-Info "Uninstaller: $uninstExe"
            Write-Host ""
            if (Confirm-Action "Run MSYS2 uninstaller?") {
                Write-Info "Uninstalling MSYS2 (CLI mode)..."
                Start-Process -FilePath $uninstExe `
                    -ArgumentList "pr", "--confirm-command" `
                    -Wait
                Write-Ok "MSYS2 uninstaller finished."
                $changed = $true
            } else {
                Write-Info "Skipped."
            }
        } elseif ($msys2Info.UninstCmd) {
            Write-Warn "MSYS2 found at $Msys2Root"
            Write-Info ("Uninstall command (from registry):")
            Write-Info ("  " + $msys2Info.UninstCmd)
            Write-Host ""
            if (Confirm-Action "Run MSYS2 uninstaller?") {
                Write-Info "Launching MSYS2 uninstaller..."
                $cmd = $msys2Info.UninstCmd
                Start-Process -FilePath cmd.exe `
                    -ArgumentList "/c `"$cmd`"" `
                    -Wait
                Write-Ok "MSYS2 uninstaller finished."
                $changed = $true
            } else {
                Write-Info "Skipped."
            }
        } else {
            Write-Warn "MSYS2 found at $Msys2Root"
            Write-Info "No uninstaller found."
        }

        # Check if the directory still exists after uninstaller
        if (Test-Path $Msys2Root) {
            # Re-check registry -- if the uninstall entry is still
            # there, the user likely canceled the uninstaller, so
            # we should not offer to delete the folder.
            $recheck = Find-Msys2Install

            if ($recheck.RegKey) {
                Write-Warn "$Msys2Root still exists."
                Write-Info "MSYS2 is still registered in Add/Remove"
                Write-Info "Programs (uninstaller may have been"
                Write-Info "canceled). Skipping folder deletion."
            } else {
                Write-Warn "$Msys2Root still exists (leftover files)."
                if (Confirm-Action "Delete $Msys2Root?") {
                    Remove-Item -Recurse -Force $Msys2Root `
                        -ErrorAction SilentlyContinue
                    if (-not (Test-Path $Msys2Root)) {
                        Write-Ok "Deleted $Msys2Root"
                        $changed = $true
                    } else {
                        Write-Fail "Could not fully remove."
                        Write-Info "Some files may be locked."
                        Write-Info "Close any MSYS2 terminals"
                        Write-Info "and retry, or delete manually."
                    }
                } else {
                    Write-Info "Skipped."
                }
            }
        }
    } else {
        Write-Ok "MSYS2 not found at $Msys2Root. Nothing to do."
    }

    # --- vcpkg ---------------------------------------------------------

    Write-Host ""
    Write-Info "Checking for vcpkg..."

    if (Test-Path $VcpkgRoot) {
        Write-Warn "vcpkg found at $VcpkgRoot"
        if (Confirm-Action "Delete $VcpkgRoot?") {
            Remove-Item -Recurse -Force $VcpkgRoot `
                -ErrorAction SilentlyContinue
            if (-not (Test-Path $VcpkgRoot)) {
                Write-Ok "Deleted $VcpkgRoot"
                $changed = $true
            } else {
                Write-Fail "Could not fully remove $VcpkgRoot."
                Write-Info "Some files may be locked. Close any"
                Write-Info "terminals using vcpkg and retry, or"
                Write-Info "delete manually."
            }
        } else {
            Write-Info "Skipped."
        }
    } else {
        Write-Ok "vcpkg not found at $VcpkgRoot. Nothing to do."
    }

    # --- Summary -------------------------------------------------------

    Write-Host ""
    if ($changed) {
        Write-Ok "Uninstall complete."
        Write-Info "Open a new terminal for changes to take effect."
    } else {
        Write-Info "No changes were made."
    }
    Write-Host ""
    exit 0
}

# ===================================================================
#  MIGRATE: HOBBYCAD_CLONE -> HOBBYCAD_REPO
# ===================================================================
#  Older versions of this script used HOBBYCAD_CLONE.  Detect and
#  migrate to HOBBYCAD_REPO automatically.

$oldCloneVal = [Environment]::GetEnvironmentVariable(
    "HOBBYCAD_CLONE", "User"
)
if ($oldCloneVal) {
    Write-Info ("Migrating HOBBYCAD_CLONE -> HOBBYCAD_REPO" +
        " ($oldCloneVal)")
    [Environment]::SetEnvironmentVariable(
        "HOBBYCAD_REPO", $oldCloneVal, "User"
    )
    [Environment]::SetEnvironmentVariable(
        "HOBBYCAD_CLONE", $null, "User"
    )
    $env:HOBBYCAD_REPO  = $oldCloneVal
    $env:HOBBYCAD_CLONE = $null
    Write-Ok "Migrated HOBBYCAD_CLONE to HOBBYCAD_REPO."
}

# ===================================================================
#  1. MSYS2 BASE INSTALL
# ===================================================================

Write-Header "1/7  MSYS2"

$msys2Exe = Join-Path $Msys2Root "msys2.exe"

# If MSYS2 is not at the default location, check the
# registry in case it was installed elsewhere.
if (-not (Test-Path $msys2Exe)) {
    $detected = Find-Msys2Install
    if ($detected.Path -and (Test-Path $detected.Path)) {
        Write-Info ("MSYS2 found at: " +
            $detected.Path + " (via registry)")
        $Msys2Root = $detected.Path
        $ucrt64Bin = Join-Path $Msys2Root "ucrt64\bin"
        $msys2Exe  = Join-Path $Msys2Root "msys2.exe"
    }
}

if (Test-Path $msys2Exe) {
    Write-Ok "MSYS2 found at $Msys2Root"
} else {
    Write-Fail "MSYS2 not found at $Msys2Root"

    if (Confirm-Action "Download and install MSYS2?") {
        Write-Info "Querying GitHub for latest stable release..."
        $installerInfo = Get-Msys2InstallerUrl
        Write-Info ("Release: " + $installerInfo.Tag +
            "  File: " + $installerInfo.Name)

        $installer = Get-TempDownload `
            $installerInfo.Url $installerInfo.Name
        if ($installer) {
            # The MSYS2 installer uses Qt Installer Framework.
            # CLI mode: installs without launching a shell at the
            # end (no "Run MSYS2" checkbox on finish page).
            # We run pacman -Syu ourselves after install anyway.
            $rootForward = $Msys2Root -replace '\\', '/'
            Write-Info "Installing MSYS2 to $Msys2Root ..."
            $installerArgs = @(
                "in",
                "--confirm-command",
                "--accept-messages",
                "--root", $rootForward
            )
            Start-Process -FilePath $installer `
                -ArgumentList $installerArgs -Wait

            # Verify installation at the specified root
            $testExe = Join-Path $Msys2Root "msys2.exe"
            if (-not (Test-Path $testExe)) {
                # Fallback -- check registry in case --root
                # was ignored or the installer placed it elsewhere
                $detected = Find-Msys2Install
                if ($detected.Path -and
                    (Test-Path $detected.Path)) {
                    Write-Info ("MSYS2 installed to: " +
                        $detected.Path +
                        " (different from requested)")
                    $Msys2Root = $detected.Path
                    $ucrt64Bin = Join-Path $Msys2Root `
                        "ucrt64\bin"
                    $testExe = Join-Path $Msys2Root `
                        "msys2.exe"
                }
            }

            if (Test-Path $testExe) {
                Write-Ok "MSYS2 installed."
                Write-Info "Running initial system update..."
                $cmd = "pacman -Syu --noconfirm"
                Invoke-Msys2Command $Msys2Root $cmd
                # Second pass for held-back core packages
                Invoke-Msys2Command $Msys2Root $cmd
                Write-Ok "MSYS2 base system updated."
            } else {
                Write-Fail "MSYS2 not found after install."
                Write-Info "Expected at $Msys2Root"
                Write-Info "Install manually: https://www.msys2.org/"
                $allOk = $false
            }
        } else {
            $allOk = $false
        }
    } else {
        Write-Info "Install manually: https://www.msys2.org/"
        $allOk = $false
    }
}

# ===================================================================
#  2. MSYS2 UCRT64 PACKAGES
# ===================================================================

Write-Header "2/7  MSYS2 Packages (UCRT64 toolchain)"

$msys2Packages = @(
    "base-devel",
    "mingw-w64-ucrt-x86_64-git",
    "mingw-w64-ucrt-x86_64-toolchain",
    "mingw-w64-ucrt-x86_64-cmake",
    "mingw-w64-ucrt-x86_64-ninja",
    "mingw-w64-ucrt-x86_64-python",
    "mingw-w64-ucrt-x86_64-python-pip",
    "mingw-w64-ucrt-x86_64-qt6-base",
    "mingw-w64-ucrt-x86_64-qt6-tools",
    "mingw-w64-ucrt-x86_64-opencascade"
)

$msys2Present = Test-Path (Join-Path $Msys2Root "msys2.exe")

if ($msys2Present) {
    $gppPath = Join-Path $ucrt64Bin "g++.exe"
    $cmkPath = Join-Path $ucrt64Bin "cmake.exe"
    $ninPath = Join-Path $ucrt64Bin "ninja.exe"
    $pyPath  = Join-Path $ucrt64Bin "python.exe"
    $gitPath = Join-Path $ucrt64Bin "git.exe"
    $qt6Path = Join-Path $Msys2Root "ucrt64\lib\cmake\Qt6\Qt6Config.cmake"
    $occtPath = Join-Path $Msys2Root "ucrt64\lib\cmake\opencascade\OpenCASCADEConfig.cmake"

    $missing = @()
    if (-not (Test-Path $gppPath)) { $missing += "g++" }
    if (-not (Test-Path $cmkPath)) { $missing += "cmake" }
    if (-not (Test-Path $ninPath)) { $missing += "ninja" }
    if (-not (Test-Path $pyPath))  { $missing += "python" }
    if (-not (Test-Path $gitPath)) { $missing += "git" }
    if (-not (Test-Path $qt6Path)) { $missing += "qt6" }
    if (-not (Test-Path $occtPath)) { $missing += "opencascade" }

    if ($missing.Count -eq 0) {
        Write-Ok "Toolchain packages installed."
        $gccVer = & $gppPath --version 2>&1 |
            Select-Object -First 1
        Write-Info "  g++ : $gccVer"
    } else {
        $list = $missing -join ", "
        Write-Warn "Missing from UCRT64: $list"

        if (Confirm-Action "Install UCRT64 packages now?") {
            $pkgList = $msys2Packages -join " "
            Write-Info "Running: pacman -S --needed ..."
            $cmd = "pacman -S --needed --noconfirm $pkgList"
            Invoke-Msys2Command $Msys2Root $cmd

            # Remove the base MSYS2 git package if present.
            # It lacks working HTTPS support; the UCRT64 git
            # package (just installed above) replaces it.
            $baseGit = Join-Path $Msys2Root "usr\bin\git.exe"
            if (Test-Path $baseGit) {
                Write-Info "Removing base MSYS2 git package..."
                $rmCmd = "pacman -Rns --noconfirm git"
                Invoke-Msys2Command $Msys2Root $rmCmd
                if (Test-Path $baseGit) {
                    Write-Warn "Base git still present (may be" +
                        " a dependency). Not critical."
                } else {
                    Write-Ok "Base git package removed."
                }
            }

            # Re-check
            $stillMissing = @()
            if (-not (Test-Path $gppPath)) {
                $stillMissing += "g++"
            }
            if (-not (Test-Path $cmkPath)) {
                $stillMissing += "cmake"
            }
            if (-not (Test-Path $ninPath)) {
                $stillMissing += "ninja"
            }
            if (-not (Test-Path $gitPath)) {
                $stillMissing += "git"
            }
            if (-not (Test-Path $qt6Path)) {
                $stillMissing += "qt6"
            }
            if (-not (Test-Path $occtPath)) {
                $stillMissing += "opencascade"
            }

            if ($stillMissing.Count -eq 0) {
                Write-Ok "All packages installed."
            } else {
                $list = $stillMissing -join ", "
                Write-Fail "Still missing: $list"
                Write-Info "Open MSYS2 UCRT64 shell and run:"
                Write-Info "  pacman -S --needed $pkgList"
                $allOk = $false
            }
        } else {
            $pkgList = $msys2Packages -join " "
            Write-Info "Open MSYS2 UCRT64 shell and run:"
            Write-Info "  pacman -S --needed $pkgList"
            $allOk = $false
        }
    }
} else {
    Write-Info "MSYS2 not installed -- skipping package check."
    $allOk = $false
}

# ===================================================================
#  3. PATH
# ===================================================================

Write-Header "3/7  PATH -- UCRT64 binaries"

# Re-check in case MSYS2 location was updated by step 1
# or the user already has it at a non-default path.
if (-not (Test-Path $ucrt64Bin)) {
    $detected = Find-Msys2Install
    if ($detected.Path -and
        (Test-Path (Join-Path $detected.Path "ucrt64\bin"))) {
        $Msys2Root = $detected.Path
        $ucrt64Bin = Join-Path $Msys2Root "ucrt64\bin"
    }
}

# Read the raw (unexpanded) user PATH from the registry and
# check whether any MSYS2 ucrt64 entry is already present.
$rawUserPath = (Get-Item 'HKCU:\Environment').GetValue(
    'Path', '', 'DoNotExpandEnvironmentNames'
)
$pathEntries = $rawUserPath -split ";" |
    Where-Object { $_ -ne "" }
$msysInPath = $pathEntries | Where-Object {
    ($_ -eq $ucrt64Bin) -or
    ($_ -like '*msys*ucrt64*bin*')
} | Select-Object -First 1

if ($msysInPath) {
    Write-Ok "MSYS2 ucrt64 already in PATH: $msysInPath"
} elseif (Test-Path $ucrt64Bin) {
    Write-Warn "$ucrt64Bin is not in your user PATH."
    Write-Info "Required so CMake, Ninja, and GCC are available"
    Write-Info "from any terminal (PowerShell, cmd, etc.)."
    Write-Host ""
    Write-Info "This modifies your per-user PATH environment"
    Write-Info "variable stored in the Windows registry at"
    Write-Info "HKCU\Environment\Path.  It does NOT change the"
    Write-Info "system-wide PATH (HKLM).  The new value takes"
    Write-Info "effect in any terminal opened after the change."
    Write-Info "To undo later, run this script with -Uninstall."
    Write-Host ""
    Write-Info "Registry key: HKCU\Environment\Path"
    Write-Info "Old value:"
    Write-Info "  $rawUserPath"
    Write-Info "New value:"
    Write-Info "  $rawUserPath;$ucrt64Bin;"
    Write-Host ""
    $prompt = "Add $ucrt64Bin to user PATH?"
    if (Confirm-Action $prompt) {
        Add-ToUserPath $ucrt64Bin
    } else {
        Write-Info "To add manually:"
        Write-Info '  Settings > System > About >'
        Write-Info '    Advanced system settings >'
        Write-Info '    Environment Variables >'
        Write-Info '    User variables > Path > Edit >'
        Write-Info ("    New > " + $ucrt64Bin)
        $needPath = $true
    }
} else {
    Write-Info "Install MSYS2 and packages first (steps 1-2)."
    $allOk = $false
}

# ===================================================================
#  4. TOOL VERIFICATION
# ===================================================================

Write-Header "4/7  Tool Verification"

$gitKnown = Join-Path $ucrt64Bin "git.exe"

$verifyTools = @(
    @{ Name = "g++";    Known = (Join-Path $ucrt64Bin "g++.exe")
       Required = $true },
    @{ Name = "cmake";  Known = (Join-Path $ucrt64Bin "cmake.exe")
       Required = $true },
    @{ Name = "ninja";  Known = (Join-Path $ucrt64Bin "ninja.exe")
       Required = $false },
    @{ Name = "git";    Known = $gitKnown
       Required = $true },
    @{ Name = "python"; Known = (Join-Path $ucrt64Bin "python.exe")
       Required = $false }
)

# Resolved full paths -- used by later steps so they work
# even if MSYS2 is not in the session PATH.
$gitExe   = $null
$cmakeExe = $null

foreach ($tool in $verifyTools) {
    $exe = Resolve-Exe $tool.Name $tool.Known
    if ($exe) {
        $ver = Get-CommandVersion $exe
        $inPath = Test-Command $tool.Name
        if ($inPath) {
            Write-Ok "$($tool.Name) : $ver"
        } else {
            Write-Ok "$($tool.Name) : $ver (via $exe)"
            Write-Info "(Not in PATH -- using full path.)"
        }
        # Store for later steps
        if ($tool.Name -eq "git")   { $gitExe   = $exe }
        if ($tool.Name -eq "cmake") { $cmakeExe = $exe }
    } else {
        if ($tool.Required) {
            Write-Fail "$($tool.Name) not found."
            $allOk = $false
        } else {
            Write-Warn "$($tool.Name) not found (optional)."
        }
    }
}

# CMake minimum version check
if ($cmakeExe) {
    $cmakeVer = Get-CommandVersion $cmakeExe
    if ($cmakeVer) {
        $verMatch = [regex]::Match($cmakeVer, '(\d+)\.(\d+)')
        if ($verMatch.Success) {
            $major = [int]$verMatch.Groups[1].Value
            $minor = [int]$verMatch.Groups[2].Value
            if ($major -lt 3 -or
                ($major -eq 3 -and $minor -lt 20)) {
                Write-Warn ("CMake 3.20+ required" +
                    " (found $major.$minor).")
                Write-Info "Update MSYS2: pacman -Syu"
                $allOk = $false
            }
        }
    }
}

# ===================================================================
#  5. REPOSITORY
# ===================================================================

Write-Header "5/7  HobbyCAD Repository"

if ($isInRepo) {
    Write-Ok "Running from inside a clone at $repoRoot"
    $clonePath = $repoRoot
} else {
    Write-Info "Script is running standalone (not inside a clone)."

    if ($gitExe) {
        if (Confirm-Action "Clone the HobbyCAD repository?") {
            # Determine target directory
            if ($CloneDir -ne "") {
                $targetParent = $CloneDir
            } elseif ($NonInteractive) {
                $targetParent = (Get-Location).Path
            } else {
                $default = (Get-Location).Path
                $answer = Read-Host (
                    "  Clone to? [$default\hobbycad]"
                )
                if ($answer -eq '') {
                    $targetParent = $default
                } else {
                    $targetParent = $answer
                }
            }

            $targetDir = Join-Path $targetParent "hobbycad"

            # Verify the parent directory exists; offer to
            # create it (and any missing intermediate dirs).
            if (-not (Test-Path $targetParent)) {
                if (Confirm-Action (
                    "$targetParent does not exist. Create it?")) {
                    New-Item -ItemType Directory -Force `
                        -Path $targetParent | Out-Null
                    if (Test-Path $targetParent) {
                        Write-Ok "Created $targetParent"
                    } else {
                        Write-Fail "Could not create $targetParent"
                        $allOk = $false
                    }
                } else {
                    Write-Info "Skipped clone."
                    $allOk = $false
                }
            }

            if (-not (Test-Path $targetParent)) {
                # Creation failed or was skipped -- skip clone
            } elseif (Test-Path (Join-Path $targetDir ".git")) {
                Write-Ok "Repository already exists at $targetDir"
                $clonePath = $targetDir
            } else {
                Write-Info "Cloning to $targetDir..."
                & $gitExe clone $RepoUrl $targetDir
                if ($LASTEXITCODE -eq 0) {
                    Write-Ok "Cloned to $targetDir"
                    $clonePath = $targetDir
                } else {
                    Write-Fail "git clone failed."
                    Write-Info "Clone manually:"
                    Write-Info "  git clone $RepoUrl"
                    $allOk = $false
                }
            }

            # Persist the clone path so -Uninstall can find it
            if ($clonePath -ne "") {
                [Environment]::SetEnvironmentVariable(
                    "HOBBYCAD_REPO", $clonePath, "User"
                )
                $env:HOBBYCAD_REPO = $clonePath
                Write-Info ("Saved clone path to " +
                    "HKCU\Environment\HOBBYCAD_REPO")
            }
        } else {
            Write-Info "Clone manually when ready:"
            Write-Info "  git clone $RepoUrl"
        }
    } else {
        Write-Warn "Git not available yet -- cannot clone."
        Write-Info "Install MSYS2 and packages (steps 1-2),"
        Write-Info "then re-run this script."
        $allOk = $false
    }
}

# ===================================================================
#  6. VCPKG
# ===================================================================

Write-Header "6/7  vcpkg"

if ($SkipVcpkg) {
    Write-Info "Skipped (-SkipVcpkg flag set)."
} elseif (Test-Path "$VcpkgRoot\vcpkg.exe") {
    Write-Ok "vcpkg found at $VcpkgRoot"

    # Check VCPKG_ROOT env var
    $envRoot = [Environment]::GetEnvironmentVariable(
        "VCPKG_ROOT", "User"
    )
    if (-not $envRoot) {
        $envRoot = [Environment]::GetEnvironmentVariable(
            "VCPKG_ROOT", "Machine"
        )
    }
    if ($envRoot) {
        Write-Ok "VCPKG_ROOT is set: $envRoot"
    } else {
        Write-Warn "VCPKG_ROOT environment variable is not set."
        Write-Host ""
        Write-Info "Registry key: HKCU\Environment\VCPKG_ROOT"
        Write-Info "Old value:    (not set)"
        Write-Info "New value:    $VcpkgRoot"
        Write-Host ""
        $prompt = "Set VCPKG_ROOT=$VcpkgRoot?"
        if (Confirm-Action $prompt) {
            [Environment]::SetEnvironmentVariable(
                "VCPKG_ROOT", $VcpkgRoot, "User"
            )
            $env:VCPKG_ROOT = $VcpkgRoot
            Write-Ok "VCPKG_ROOT set."
        } else {
            Write-Info "Set manually, or run:"
            $setCmd = "[Environment]::SetEnvironmentVariable(" +
                "'VCPKG_ROOT', '$VcpkgRoot', 'User')"
            Write-Info "  $setCmd"
        }
    }
} else {
    Write-Warn "vcpkg not found at $VcpkgRoot"

    if ($gitExe) {
        $prompt = "Clone and bootstrap vcpkg to ${VcpkgRoot}?"
        if (Confirm-Action $prompt) {
            Write-Info "Cloning vcpkg..."
            & $gitExe clone `
                https://github.com/microsoft/vcpkg.git `
                $VcpkgRoot
            if ($LASTEXITCODE -eq 0) {
                Write-Info "Bootstrapping vcpkg..."
                Push-Location $VcpkgRoot
                & .\bootstrap-vcpkg.bat -disableMetrics
                Pop-Location
                $vcpkgExe = "$VcpkgRoot\vcpkg.exe"
                if (Test-Path $vcpkgExe) {
                    Write-Ok "vcpkg installed at $VcpkgRoot"
                    Write-Info "Writing: HKCU\Environment\VCPKG_ROOT"
                    [Environment]::SetEnvironmentVariable(
                        "VCPKG_ROOT", $VcpkgRoot, "User"
                    )
                    $env:VCPKG_ROOT = $VcpkgRoot
                    Write-Ok "VCPKG_ROOT set."
                } else {
                    Write-Fail "vcpkg bootstrap failed."
                    $allOk = $false
                }
            } else {
                Write-Fail "git clone failed."
                $allOk = $false
            }
        } else {
            Write-Info "To install later:"
            $cloneUrl = "https://github.com/microsoft/vcpkg.git"
            Write-Info "  git clone $cloneUrl $VcpkgRoot"
            Write-Info "  cd $VcpkgRoot && .\bootstrap-vcpkg.bat"
            $allOk = $false
        }
    } else {
        Write-Fail "vcpkg not found and Git is unavailable."
        Write-Info "Install Git first (step 2), then re-run."
        $allOk = $false
    }
}

# ===================================================================
#  7. SUMMARY
# ===================================================================

Write-Header "7/7  Summary"

if ($needPath) {
    Write-Warn "PATH was not updated."
    Write-Warn "Add $ucrt64Bin to your user PATH,"
    Write-Warn "then re-run this script to verify."
    Write-Host ""
}

$tcf = "$VcpkgRoot/scripts/buildsystems/vcpkg.cmake"

if ($allOk) {
    Write-Ok "Environment is ready."
    Write-Host ""
    Write-Info "Next steps:"

    # Step numbering adjusts based on whether clone happened
    $step = 1

    if ($clonePath -eq "") {
        Write-Info "  $step. Clone the repo:"
        Write-Info "       git clone $RepoUrl hobbycad"
        Write-Info "       cd hobbycad"
        $step++
    } else {
        Write-Info "  Repository: $clonePath"
    }

    Write-Info ""
    Write-Info "  $step. Verify dependencies:"
    if ($clonePath -ne "") {
        Write-Info "       cd $clonePath\devtest"
    } else {
        Write-Info "       cd devtest"
    }
    Write-Info "       cmake -B build \"
    Write-Info "         -DCMAKE_TOOLCHAIN_FILE=$tcf"
    Write-Info "       cmake --build build"
    Write-Info "       .\build\depcheck.exe"
    $step++

    Write-Info ""
    Write-Info "  $step. Build HobbyCAD:"
    if ($clonePath -ne "") {
        Write-Info "       cd $clonePath"
    } else {
        Write-Info "       cd .."
    }
    Write-Info "       cmake --preset msys2-debug"
    Write-Info "       cmake --build --preset msys2-debug"
    Write-Info ""
    Write-Info "     Or use the build script:"
    Write-Info "       .\tools\windows\build-dev.bat"
    $step++

    Write-Info ""
    Write-Info "  $step. Run:"
    Write-Info "       .\build\src\hobbycad\hobbycad.exe"
} else {
    Write-Fail "Some items need attention -- see above."
    Write-Host ""
    Write-Info "Fix the issues, then run this script again."
}

Write-Host ""
