$ErrorActionPreference = 'Stop'
$SolutionRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$VcpkgRoot = Join-Path $SolutionRoot '.deps\vcpkg'
if (Test-Path -LiteralPath $VcpkgRoot) {
    Remove-Item -LiteralPath $VcpkgRoot -Recurse -Force
    Write-Host 'Removed the temporary private vcpkg tree.'
}
else {
    Write-Host 'No temporary vcpkg tree is present.'
}
