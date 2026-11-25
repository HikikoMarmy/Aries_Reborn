param(
    [Parameter(Mandatory = $true)][string]$TripletRoot,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [Parameter(Mandatory = $true)][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$ImGuiRoot
)

$ErrorActionPreference = 'Stop'
$BinDir = if ($Configuration -ieq 'Debug') { Join-Path $TripletRoot 'debug\bin' } else { Join-Path $TripletRoot 'bin' }
$LicenseDir = Join-Path $OutputDir 'licenses'
$SolutionRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
New-Item -ItemType Directory -Path $OutputDir,$LicenseDir -Force | Out-Null
foreach ($Name in @('SDL3.dll', 'sqlite3.dll')) {
    Copy-Item -LiteralPath (Join-Path $BinDir $Name) -Destination (Join-Path $OutputDir $Name) -Force
}
$Notices = @(
    @{ Source = (Join-Path $ImGuiRoot 'LICENSE.txt'); Name = 'Dear-ImGui.txt' },
    @{ Source = (Join-Path $TripletRoot 'share\sdl3\copyright'); Name = 'SDL3.txt' },
    @{ Source = (Join-Path $TripletRoot 'share\sqlite3\copyright'); Name = 'SQLite3.txt' },
    @{ Source = (Join-Path $SolutionRoot 'LICENSE'); Name = 'Aries-Reborn-GPL-3.0.txt' }
)
foreach ($Notice in $Notices) {
    Copy-Item -LiteralPath $Notice.Source -Destination (Join-Path $LicenseDir $Notice.Name) -Force
}
Write-Host 'Aries runtime and license notices deployed.'
