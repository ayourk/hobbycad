# =====================================================================
#  tools/windows/setup-env.ps1 — Windows development environment setup
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

function Get-CommandVersion($cmd) {
    try {
        $out = & $cmd --version 2>&1 |
            Select-Object -First 1
        return $out.ToString().Trim()
    } catch {
        return $null
    }
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
function Add-ToUserPath($dir) {
    $currentPath = [Environment]::GetEnvironmentVariable(
        "Path", "User"
    )
    $entries = $currentPath -split ";"
    if ($entries | Where-Object { $_ -eq $dir }) {
        Write-Info "$dir is already in user PATH."
        return
    }
    Write-Info "Writing to: HKCU\Environment\Path"
    [Environment]::SetEnvironmentVariable(
        "Path", "$currentPath;$dir", "User"
    )
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
#  1. MSYS2 BASE INSTALL
# ===================================================================

Write-Header "1/7  MSYS2"

$msys2Exe = Join-Path $Msys2Root "msys2.exe"

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
            Write-Info "Running MSYS2 installer..."
            Write-Info "Install to: $Msys2Root (the default)"
            Start-Process -FilePath $installer -Wait
            $testExe = Join-Path $Msys2Root "msys2.exe"
            if (Test-Path $testExe) {
                Write-Ok "MSYS2 installed."
                Write-Info "Running initial system update..."
                Write-Info "(Window may close once — normal.)"
                $cmd = "pacman -Syu --noconfirm"
                Invoke-Msys2Command $Msys2Root $cmd
                # Second pass for held-back core packages
                Invoke-Msys2Command $Msys2Root $cmd
                Write-Ok "MSYS2 base system updated."
            } else {
                Write-Fail "Installer did not create $Msys2Root."
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
    "mingw-w64-ucrt-x86_64-toolchain",
    "mingw-w64-ucrt-x86_64-cmake",
    "mingw-w64-ucrt-x86_64-ninja",
    "mingw-w64-ucrt-x86_64-python",
    "mingw-w64-ucrt-x86_64-python-pip"
)

$msys2Present = Test-Path (Join-Path $Msys2Root "msys2.exe")

if ($msys2Present) {
    $gppPath = Join-Path $ucrt64Bin "g++.exe"
    $cmkPath = Join-Path $ucrt64Bin "cmake.exe"
    $ninPath = Join-Path $ucrt64Bin "ninja.exe"
    $pyPath  = Join-Path $ucrt64Bin "python.exe"

    $missing = @()
    if (-not (Test-Path $gppPath)) { $missing += "g++" }
    if (-not (Test-Path $cmkPath)) { $missing += "cmake" }
    if (-not (Test-Path $ninPath)) { $missing += "ninja" }
    if (-not (Test-Path $pyPath))  { $missing += "python" }

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
    Write-Info "MSYS2 not installed — skipping package check."
    $allOk = $false
}

# ===================================================================
#  3. PATH
# ===================================================================

Write-Header "3/7  PATH — UCRT64 binaries"

$gppPath = Join-Path $ucrt64Bin "g++.exe"

if (Test-Command "g++") {
    Write-Ok "g++ is already in PATH."
} elseif (Test-Path $gppPath) {
    Write-Warn "$ucrt64Bin is not in your PATH."
    Write-Info "Required so CMake, Ninja, and GCC are available"
    Write-Info "from any terminal (PowerShell, cmd, etc.)."
    Write-Host ""
    $prompt = "Add $ucrt64Bin to user PATH? (writes to registry)"
    if (Confirm-Action $prompt) {
        Add-ToUserPath $ucrt64Bin
    } else {
        Write-Info "To add manually:"
        Write-Info "  Settings > System > About >"
        Write-Info "    Advanced system settings >"
        Write-Info "    Environment Variables >"
        Write-Info "    User variables > Path > Edit >"
        Write-Info "    New > $ucrt64Bin"
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

$gitPath = Join-Path $Msys2Root "usr\bin\git.exe"

$verifyTools = @(
    @{ Name = "g++";    Path = (Join-Path $ucrt64Bin "g++.exe")
       Required = $true },
    @{ Name = "cmake";  Path = (Join-Path $ucrt64Bin "cmake.exe")
       Required = $true },
    @{ Name = "ninja";  Path = (Join-Path $ucrt64Bin "ninja.exe")
       Required = $false },
    @{ Name = "git";    Path = $gitPath
       Required = $true },
    @{ Name = "python"; Path = (Join-Path $ucrt64Bin "python.exe")
       Required = $false }
)

foreach ($tool in $verifyTools) {
    if (Test-Command $tool.Name) {
        $ver = Get-CommandVersion $tool.Name
        Write-Ok "$($tool.Name) : $ver"
    } elseif (Test-Path $tool.Path) {
        $name = $tool.Name
        Write-Warn "$name at $($tool.Path) but not in PATH."
        Write-Info "(Open new terminal after PATH changes.)"
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
if (Test-Command "cmake") {
    $cmakeVer = Get-CommandVersion "cmake"
    $verMatch = [regex]::Match($cmakeVer, '(\d+)\.(\d+)')
    if ($verMatch.Success) {
        $major = [int]$verMatch.Groups[1].Value
        $minor = [int]$verMatch.Groups[2].Value
        if ($major -lt 3 -or
            ($major -eq 3 -and $minor -lt 22)) {
            Write-Warn "CMake 3.22+ required (found $major.$minor)."
            Write-Info "Update MSYS2: pacman -Syu"
            $allOk = $false
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

    $canGit = Test-Command "git"
    if (-not $canGit) {
        $msys2Git = Join-Path $Msys2Root "usr\bin\git.exe"
        $canGit = Test-Path $msys2Git
    }

    if ($canGit) {
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

            if (Test-Path (Join-Path $targetDir ".git")) {
                Write-Ok "Repository already exists at $targetDir"
                $clonePath = $targetDir
            } else {
                Write-Info "Cloning to $targetDir..."
                git clone $RepoUrl $targetDir
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
        } else {
            Write-Info "Clone manually when ready:"
            Write-Info "  git clone $RepoUrl"
        }
    } else {
        Write-Warn "Git not available yet — cannot clone."
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
        $prompt = "Set VCPKG_ROOT=$VcpkgRoot? (writes to registry)"
        if (Confirm-Action $prompt) {
            Write-Info "Writing to: HKCU\Environment\VCPKG_ROOT"
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

    $canGit = Test-Command "git"
    if (-not $canGit) {
        $msys2Git = Join-Path $Msys2Root "usr\bin\git.exe"
        $canGit = Test-Path $msys2Git
    }

    if ($canGit) {
        $prompt = "Clone and bootstrap vcpkg to ${VcpkgRoot}?"
        if (Confirm-Action $prompt) {
            Write-Info "Cloning vcpkg..."
            git clone https://github.com/microsoft/vcpkg.git `
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
    Write-Info "       cmake -B build -G Ninja \"
    Write-Info "         -DCMAKE_TOOLCHAIN_FILE=$tcf \"
    Write-Info "         -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic \"
    Write-Info "         -DCMAKE_BUILD_TYPE=Debug"
    Write-Info "       cmake --build build"
    $step++

    Write-Info ""
    Write-Info "  $step. Run:"
    Write-Info "       .\build\hobbycad.exe"
} else {
    Write-Fail "Some items need attention — see above."
    Write-Host ""
    Write-Info "Fix the issues, then run this script again."
}

Write-Host ""
