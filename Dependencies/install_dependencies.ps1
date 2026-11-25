$ErrorActionPreference = 'Stop'

$SolutionRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$DepsRoot = Join-Path $SolutionRoot '.deps'
$VcpkgRoot = Join-Path $DepsRoot 'vcpkg'
$ImGuiRoot = Join-Path $DepsRoot 'imgui'
$InstallRoot = Join-Path $DepsRoot 'vcpkg_installed'
$TripletRoot = Join-Path $InstallRoot 'x64-windows'
$ManifestPath = Join-Path $PSScriptRoot 'vcpkg.json'
$StampPath = Join-Path $InstallRoot 'aries-dependencies.txt'
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$Baseline = $Manifest.'builtin-baseline'
$ImGuiVersion = 'v1.92.8'
if ($Baseline -notmatch '^[0-9a-f]{40}$') { throw 'vcpkg.json must contain a pinned builtin-baseline.' }
$ManifestHash = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash
$ExpectedStamp = "aries-dependencies-v3-$ManifestHash-$ImGuiVersion"
$RequiredFiles = @(
    (Join-Path $TripletRoot 'lib\sqlite3.lib'),
    (Join-Path $TripletRoot 'lib\SDL3.lib'),
    (Join-Path $TripletRoot 'debug\lib\sqlite3.lib'),
    (Join-Path $TripletRoot 'debug\lib\SDL3.lib'),
    (Join-Path $TripletRoot 'include\sqlite3.h'),
    (Join-Path $TripletRoot 'include\SDL3\SDL.h'),
    (Join-Path $TripletRoot 'bin\sqlite3.dll'),
    (Join-Path $TripletRoot 'bin\SDL3.dll'),
    (Join-Path $TripletRoot 'debug\bin\sqlite3.dll'),
    (Join-Path $TripletRoot 'debug\bin\SDL3.dll'),
    (Join-Path $TripletRoot 'share\sqlite3\copyright'),
    (Join-Path $TripletRoot 'share\sdl3\copyright'),
    (Join-Path $ImGuiRoot 'imgui.h'),
    (Join-Path $ImGuiRoot 'imgui.cpp'),
    (Join-Path $ImGuiRoot 'imgui_draw.cpp'),
    (Join-Path $ImGuiRoot 'imgui_tables.cpp'),
    (Join-Path $ImGuiRoot 'imgui_widgets.cpp'),
    (Join-Path $ImGuiRoot 'backends\imgui_impl_sdl3.cpp'),
    (Join-Path $ImGuiRoot 'backends\imgui_impl_sdlrenderer3.cpp'),
    (Join-Path $ImGuiRoot 'LICENSE.txt')
)

function Invoke-AriesGit {
    & $script:GitPath @args
    if ($LASTEXITCODE -ne 0) { throw "Git failed with exit code $LASTEXITCODE" }
}

if (Test-Path -LiteralPath $StampPath) {
    $Missing = @($RequiredFiles | Where-Object { -not (Test-Path -LiteralPath $_) })
    if ((Get-Content -LiteralPath $StampPath -Raw).Trim() -eq $ExpectedStamp -and $Missing.Count -eq 0) {
        Write-Host 'Aries Reborn dependencies are already installed.'
        exit 0
    }
}

$Git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($null -eq $Git) { throw 'Install Git for Windows, then rebuild the solution.' }
$script:GitPath = $Git.Source
New-Item -ItemType Directory -Path $DepsRoot -Force | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $VcpkgRoot '.git'))) {
    Invoke-AriesGit init $VcpkgRoot
}
Invoke-AriesGit -C $VcpkgRoot fetch --depth 1 https://github.com/microsoft/vcpkg.git $Baseline
Invoke-AriesGit -C $VcpkgRoot checkout --detach FETCH_HEAD
& (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed with exit code $LASTEXITCODE" }

if (-not (Test-Path -LiteralPath (Join-Path $ImGuiRoot '.git'))) {
    Invoke-AriesGit init $ImGuiRoot
}
Invoke-AriesGit -C $ImGuiRoot fetch --depth 1 https://github.com/ocornut/imgui.git "refs/tags/$ImGuiVersion"
Invoke-AriesGit -C $ImGuiRoot checkout --detach FETCH_HEAD

& (Join-Path $VcpkgRoot 'vcpkg.exe') install --triplet=x64-windows "--x-manifest-root=$PSScriptRoot" "--x-install-root=$InstallRoot" --disable-metrics
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed with exit code $LASTEXITCODE" }
foreach ($Required in $RequiredFiles) {
    if (-not (Test-Path -LiteralPath $Required)) { throw "Dependency file was not installed: $Required" }
}
Set-Content -LiteralPath $StampPath -Value $ExpectedStamp -Encoding ascii
Write-Host 'Dependencies installed. Use cleanup_cache.bat to remove the temporary vcpkg tree if desired.'
