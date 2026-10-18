# Build with installed Visual Studio C++ tools and Clang; does not install tools.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer/vswhere is required. See base/docs/readme.md.'
}
$vsRoot = & $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) {
    throw 'No Visual Studio installation with x64 C++ tools was found.'
}
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
$clangDir = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin'
if (-not (Test-Path -LiteralPath (Join-Path $clangDir 'clang-cl.exe'))) {
    throw 'The selected Visual Studio installation needs C++ Clang tools for Windows.'
}

# Some VS installations include clang-cl but not the ClangCL MSBuild integration.
# Keep using Clang, with only command-local overrides of the native toolset.
$vcTargets = Get-ChildItem -LiteralPath "$vsRoot\MSBuild\Microsoft\VC" -Directory |
    Where-Object Name -Match '^v\d+$' | Sort-Object Name -Descending | Select-Object -First 1
$toolsets = @(Get-ChildItem -LiteralPath (Join-Path $vcTargets.FullName 'Platforms\x64\PlatformToolsets') -Directory)
$nativeToolset = $toolsets | Where-Object Name -Match '^v\d+$' | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
$buildArgs = @('build\ArmorPaint.vcxproj', '/m', '/v:minimal', "/p:Configuration=$Configuration", '/p:Platform=x64')
if (-not ($toolsets | Where-Object Name -eq 'ClangCL')) {
    if (-not ($toolsets | Where-Object Name -eq $nativeToolset)) {
        throw "Neither ClangCL nor the installed C++ toolset $nativeToolset is available."
    }
    Write-Host "ClangCL integration unavailable; using $nativeToolset with $clangDir\clang-cl.exe"
    $buildArgs += @("/p:PlatformToolset=$nativeToolset", '/p:CLToolExe=clang-cl.exe', "/p:CLToolPath=$clangDir")
}
if ($Rebuild) { $buildArgs += '/t:Rebuild' }

Push-Location $PSScriptRoot
try {
    $makeArgs = @()
    if ($Configuration -eq 'Debug') { $makeArgs += '--debug' }
    & ..\base\make.bat @makeArgs
    if ($LASTEXITCODE -ne 0) { throw "amake failed with exit code $LASTEXITCODE" }
    & $msbuild @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    Copy-Item -LiteralPath "build\x64\$Configuration\ArmorPaint.exe" -Destination 'build\out\ArmorPaint.exe'
    Write-Host "Built $PSScriptRoot\build\out\ArmorPaint.exe ($Configuration)"
}
finally {
    Pop-Location
}
