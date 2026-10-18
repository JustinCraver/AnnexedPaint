# Focused native regressions. Each run keeps its runtime, fixtures and logs in build/tests.
[CmdletBinding()]
param([string]$Executable = (Join-Path $PSScriptRoot '..\build\out\ArmorPaint.exe'))

$ErrorActionPreference = 'Stop'
$paintRoot = Split-Path $PSScriptRoot -Parent
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$runDir = Join-Path $paintRoot ('build\tests\' + [guid]::NewGuid().ToString('N'))
$runtime = Join-Path $runDir 'runtime'
New-Item -ItemType Directory -Path "$runtime\data" -Force | Out-Null
Copy-Item -LiteralPath $Executable -Destination "$runtime\ArmorPaint.exe"
Get-ChildItem -LiteralPath "$paintRoot\build\out\data" | Where-Object Name -ne 'config.json' |
    Copy-Item -Destination "$runtime\data" -Recurse
$results = [System.Collections.Generic.List[object]]::new()

function Invoke-Check([string]$Name, [string]$Arguments, [string]$Marker = '') {
    $stdout = Join-Path $runDir "$Name.stdout.log"
    $stderr = Join-Path $runDir "$Name.stderr.log"
    $process = Start-Process -FilePath "$runtime\ArmorPaint.exe" -ArgumentList $Arguments -WorkingDirectory $runtime -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $finished = $process.WaitForExit(30000)
    if (-not $finished) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    $text = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
    $passed = $finished -and $process.ExitCode -eq 0 -and ($Marker -eq '' -or $text.Contains($Marker)) -and -not $text.Contains('IMPORT FAIL')
    $results.Add([pscustomobject]@{ name = $Name; passed = $passed; exitCode = $process.ExitCode; timedOut = -not $finished })
    Write-Host "$Name`: passed=$passed exit=$($process.ExitCode)"
}

Invoke-Check 'help' '--help' 'Usage: armorpaint'
Invoke-Check 'startup' '--background'

# One triangle with positions, normals and UVs in an embedded glTF buffer.
$vertices = [single[]]@(-0.5, -0.5, 0, 0.5, -0.5, 0, 0, 0.5, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0.5, 1)
$bytes = [byte[]]::new($vertices.Length * 4)
[Buffer]::BlockCopy($vertices, 0, $bytes, 0, $bytes.Length)
$gltf = @{
    asset = @{ version = '2.0' }; scene = 0; scenes = @(@{ nodes = @(0) }); nodes = @(@{ mesh = 0; name = 'case_triangle' })
    meshes = @(@{ primitives = @(@{ attributes = @{ POSITION = 0; NORMAL = 1; TEXCOORD_0 = 2 } }) })
    buffers = @(@{ byteLength = 96; uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($bytes) })
    bufferViews = @(@{ buffer = 0; byteOffset = 0; byteLength = 36 }, @{ buffer = 0; byteOffset = 36; byteLength = 36 }, @{ buffer = 0; byteOffset = 72; byteLength = 24 })
    accessors = @(@{ bufferView = 0; componentType = 5126; count = 3; type = 'VEC3'; min = @(-0.5, -0.5, 0); max = @(0.5, 0.5, 0) },
        @{ bufferView = 1; componentType = 5126; count = 3; type = 'VEC3' }, @{ bufferView = 2; componentType = 5126; count = 3; type = 'VEC2' })
} | ConvertTo-Json -Depth 10
$svg = '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"><rect width="4" height="4" fill="red"/></svg>'
foreach ($case in @('gltf', 'GLTF', 'svg', 'SVG', 'forward-gltf', 'forward-svg')) {
    $ext = $case -replace '^forward-', ''
    # Distinct stems also work on case-insensitive filesystems.
    $name = if ($ext -ceq $ext.ToUpperInvariant()) { 'upper' } else { 'lower' }
    $name += '-' + $ext.ToLowerInvariant()
    if ($case.StartsWith('forward-')) { $name = $case }
    $asset = Join-Path $runDir "$name.$ext"
    [IO.File]::WriteAllText($asset, $(if ($ext -ieq 'gltf') { $gltf } else { $svg }))
    $assetC = if ($case.StartsWith('forward-')) { $asset.Replace('\', '/') } else { $asset.Replace('\', '\\') }
    $test = Join-Path $runDir "$name.c"
    $check = if ($ext -ieq 'gltf') { 'script_get_object("case_triangle") != NULL' } else {
        "script_get_texture(`"$name.$ext`") != NULL && script_get_texture(`"$name.$ext`")->width == 4 && script_get_texture(`"$name.$ext`")->height == 4"
    }
    [IO.File]::WriteAllText($test, @"
#include "global.h"
void verify() {
    if ($check) { printf("IMPORT PASS"); }
    else { printf("IMPORT FAIL"); }
}
void main() {
    script_import_asset("$assetC", false);
    script_notify_on_next_frame(verify);
}
"@)
    Invoke-Check $name "--background --script `"$test`"" 'IMPORT PASS'
}

$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'results.json')
Get-FileHash -LiteralPath $Executable -Algorithm SHA256 | Format-List | Out-String |
    Set-Content -LiteralPath (Join-Path $runDir 'executable.sha256.txt')
Write-Host "Receipts: $runDir"
if ($results | Where-Object { -not $_.passed }) { throw 'Smoke regressions failed; see results.json and per-check logs.' }
