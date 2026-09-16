<#
  scripts/ci/select-msvc-toolset.ps1 — pick the MSVC toolset and runtime for a Windows build
  SPDX-License-Identifier: GPL-3.0-only
  Part of HobbyCAD (ayourk/hobbycad)

  Prefers the Visual Studio 2022 toolset MSVC 14.44 (v143) for as long as the
  runner image carries it (Aaron, 2026-09-15). When it is gone, falls back to
  the newest toolset installed and the runtime that matches it, and says so
  with a warning. vcpkg ignores the developer environment and picks its own
  toolset unless the triplet names one, so the choice is also written into a
  copy of the overlay triplets.

  Usage:
    select-msvc-toolset.ps1 -Arch x64|arm64 [-Preferred 14.44]
                            [-TripletSource DIR -TripletOut DIR]
                            [-MinRuntime 14.44]

  -MinRuntime is the toolset a separate build job used: the runtime chosen
  here must be at least that new, because a program needs a Visual C++
  runtime the same as or later than the toolset that built it.

  Step outputs (and printed): mode (pinned or fallback), toolset (full
  version, for msvc-dev-cmd), toolset_short (major.minor), vsversion (the
  Visual Studio year), vspath, platform_toolset (v143, v145), crt (runtime
  DLL folder), triplets (the pinned copy, when -TripletOut is given).
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Arch,
    [string]$Preferred = '14.44',
    [string]$TripletSource = '',
    [string]$TripletOut = '',
    [string]$MinRuntime = ''
)
$ErrorActionPreference = 'Stop'

function Set-Output([string]$Name, [string]$Value) {
    if ($env:GITHUB_OUTPUT) { [IO.File]::AppendAllText($env:GITHUB_OUTPUT, "$Name=$Value`n") }
    Write-Host "$Name=$Value"
}
function ConvertTo-Version([string]$Text) {
    $v = $null
    if ([version]::TryParse($Text, [ref]$v)) { return $v }
    return $null
}
function Get-MajorMinor([version]$V) { return [version]::new($V.Major, $V.Minor) }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: no Visual Studio on this machine' }
$instances = & $vswhere -all -products * -format json | ConvertFrom-Json

# Every Visual Studio instance and MSVC toolset pair that can build for $Arch.
$pairs = @()
foreach ($instance in $instances) {
    $tools = Join-Path $instance.installationPath 'VC\Tools\MSVC'
    if (-not (Test-Path $tools)) { continue }
    foreach ($dir in Get-ChildItem -Path $tools -Directory) {
        $version = ConvertTo-Version $dir.Name
        if (-not $version) { continue }
        if (-not (Test-Path (Join-Path $dir.FullName "lib\$Arch"))) { continue }
        $pairs += [pscustomobject]@{
            Path    = $instance.installationPath
            Year    = [string]$instance.catalog.productLineVersion
            Version = $version
            Name    = $dir.Name
        }
    }
}
if ($pairs.Count -eq 0) { throw "no MSVC toolset that builds for $Arch in any Visual Studio instance" }

$want = ConvertTo-Version $Preferred
$pinned = $pairs | Where-Object { $want -and (Get-MajorMinor $_.Version) -eq (Get-MajorMinor $want) } |
    Sort-Object Version -Descending | Select-Object -First 1
if ($pinned) {
    $pick = $pinned
    $mode = 'pinned'
} else {
    $pick = $pairs | Sort-Object Version -Descending | Select-Object -First 1
    $mode = 'fallback'
    Write-Host "::warning::MSVC $Preferred is not on this runner image; building with the newest toolset, $($pick.Name), and its runtime instead"
}

# The platform toolset that owns this compiler version.
$minor = $pick.Version.Minor
$platform = if ($minor -ge 50) { 'v145' } elseif ($minor -ge 30) { 'v143' } elseif ($minor -ge 20) { 'v142' } else { 'v141' }
$crtName = 'Microsoft.VC' + $platform.Substring(1) + '.CRT'

# The runtime must be at least as new as this toolset and any separate build's.
$floor = Get-MajorMinor $pick.Version
$minRt = ConvertTo-Version $MinRuntime
if ($minRt -and (Get-MajorMinor $minRt) -gt $floor) { $floor = Get-MajorMinor $minRt }

$candidates = @()
foreach ($root in @($pick.Path) + ($pairs | ForEach-Object { $_.Path } | Where-Object { $_ -ne $pick.Path } | Select-Object -Unique)) {
    $redist = Join-Path $root 'VC\Redist\MSVC'
    if (-not (Test-Path $redist)) { continue }
    foreach ($dir in Get-ChildItem -Path $redist -Directory) {
        $version = ConvertTo-Version $dir.Name
        if (-not $version -or (Get-MajorMinor $version) -lt $floor) { continue }
        foreach ($crt in Get-ChildItem -Path (Join-Path $dir.FullName $Arch) -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue) {
            $candidates += [pscustomobject]@{
                Path    = $crt.FullName
                Version = $version
                Exact   = ($crt.Name -eq $crtName)
                Own     = ($root -eq $pick.Path)
            }
        }
    }
}
# Prefer the folder named for the chosen toolset in its own instance, then the newest.
$runtime = $candidates | Sort-Object @{ Expression = 'Exact'; Descending = $true }, @{ Expression = 'Own'; Descending = $true }, @{ Expression = 'Version'; Descending = $true } |
    Select-Object -First 1
if (-not $runtime) { throw "no Visual C++ runtime for $Arch at version $floor or newer (toolset $($pick.Name))" }

Set-Output 'mode' $mode
Set-Output 'toolset' $pick.Name
Set-Output 'toolset_short' ('{0}.{1}' -f $pick.Version.Major, $pick.Version.Minor)
Set-Output 'vsversion' $pick.Year
Set-Output 'vspath' $pick.Path
Set-Output 'platform_toolset' $platform
Set-Output 'crt' $runtime.Path

if ($TripletOut) {
    if (-not $TripletSource) { throw '-TripletOut needs -TripletSource' }
    if (Test-Path $TripletOut) { Remove-Item -Recurse -Force $TripletOut }
    Copy-Item -Recurse -Path $TripletSource -Destination $TripletOut
    $vsPathCMake = $pick.Path.Replace('\', '\\')
    $pin = @(
        '',
        "# Added by scripts/ci/select-msvc-toolset.ps1 ($mode): vcpkg picks its own",
        '# toolset unless the triplet names one.',
        "set(VCPKG_VISUAL_STUDIO_PATH `"$vsPathCMake`")",
        "set(VCPKG_PLATFORM_TOOLSET $platform)",
        "set(VCPKG_PLATFORM_TOOLSET_VERSION $($pick.Name))"
    ) -join "`n"
    foreach ($triplet in Get-ChildItem -Path $TripletOut -Filter '*windows-static*.cmake') {
        [IO.File]::AppendAllText($triplet.FullName, $pin + "`n")
    }
    Set-Output 'triplets' ((Resolve-Path $TripletOut).Path)
}
