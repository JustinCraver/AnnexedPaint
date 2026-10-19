# P1-03: checked decoding and preservation of a populated native project.
[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\out\ArmorPaint.exe'),
    [switch]$SkipNative
)
$ErrorActionPreference = 'Stop'
$paintRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $paintRoot -Parent
$runDir = Join-Path $paintRoot ('build\tests\import-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
$vsRoot = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products '*' -latest `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Installed Visual Studio C++ tools are required.' }
$clang = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-cl.exe'

function Add-Result([string]$Name, [bool]$Passed, $Details) {
    $results.Add([pscustomobject]@{ name = $Name; passed = $Passed; details = $Details })
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$runDir\results.json"
    Write-Host "$Name`: passed=$Passed"
}
function Run-Unit([string]$Name, [string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & "$runDir\$Name.exe" @Arguments 2>&1 | Out-String | Set-Content -LiteralPath "$runDir\$Name-test.log" -Encoding utf8
    $unitExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Add-Result $Name ($unitExit -eq 0) @{ exitCode = $unitExit }
}
Push-Location $repoRoot
try {
    # Compile the exact production encoder function without its unrelated UI/GPU
    # functions. Keep the extracted source and hash beside the compilation log.
    $nodesSource = [IO.File]::ReadAllText((Join-Path $repoRoot 'base/sources/iron_ui_nodes.c'))
    $start = $nodesSource.IndexOf('void ui_node_canvas_encode(')
    $end = $nodesSource.IndexOf('uint32_t ui_node_canvas_encoded_size(', $start)
    if ($start -lt 0 -or $end -le $start) { throw 'Could not locate the production canvas encoder.' }
    $canvasSource = "$runDir\canvas-encode.c"
    [IO.File]::WriteAllText($canvasSource, "#include <iron_ui.h>`n" + $nodesSource.Substring($start, $end - $start))
    Get-FileHash -LiteralPath 'base/sources/iron_ui_nodes.c', $canvasSource | ConvertTo-Json |
        Set-Content -LiteralPath "$runDir\canvas-source-hashes.json"
    $meshSource = [IO.File]::ReadAllText((Join-Path $repoRoot 'paint/sources/io/import_mesh.c'))
    $start = $meshSource.IndexOf('bool import_mesh_clear_layers')
    $end = $meshSource.IndexOf('static mesh_object_t *import_mesh_make_root', $start)
    if ($start -lt 0 -or $end -le $start) { throw 'Could not locate the production mesh staging owner.' }
    [IO.File]::WriteAllText("$runDir\import-stream-owner.c", $meshSource.Substring($start, $end - $start))
    Get-FileHash -LiteralPath 'paint/sources/io/import_mesh.c', "$runDir\import-stream-owner.c" | ConvertTo-Json |
        Set-Content -LiteralPath "$runDir\mesh-source-hashes.json"
    $common = @('base/sources/iron_array.c', 'base/sources/iron_string.c', 'base/sources/iron_armpack.c',
        'base/sources/iron_lz4.c', 'base/sources/iron_map.c', 'base/sources/iron_json.c', $canvasSource)
    $objects = @{}
    foreach ($source in $common + @('paint/tests/project-import.c', 'paint/tests/import-lz4.c', 'paint/tests/gltf-import.c', 'paint/tests/import-stream.c')) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source)
        $object = "$runDir\$name.obj"
        $ErrorActionPreference = 'Continue'
        & $clang /nologo /TC /c /O2 /Gy /Gw /D_CRT_SECURE_NO_WARNINGS /FIiron_global.h /Ibase/sources /Ibase/sources/libs `
            -Wno-incompatible-pointer-types -Wno-microsoft-enum-forward-reference "/I$runDir" $source "/Fo:$object" 2>&1 |
            Out-String | Set-Content -LiteralPath "$runDir\$name-compile.log" -Encoding utf8
        $compiled = $LASTEXITCODE -eq 0
        $ErrorActionPreference = 'Stop'
        Add-Result "$name-compile" $compiled @{}
        if ($compiled) { $objects[$name] = $object }
    }
    foreach ($name in @('project-import', 'import-lz4', 'gltf-import', 'import-stream')) {
        $links = @($objects[$name], $objects['iron_array'], $objects['iron_string'])
        if ($name -eq 'project-import') { $links += @($objects['iron_armpack'], $objects['iron_lz4'], $objects['iron_map'], $objects['iron_json'], $objects['canvas-encode']) }
        if ($name -eq 'import-stream') { $links += $objects['iron_map'] }
        $ErrorActionPreference = 'Continue'
        & $clang /nologo @links "/Fe:$runDir\$name.exe" /link /OPT:REF 2>&1 | Out-String |
            Set-Content -LiteralPath "$runDir\$name-link.log" -Encoding utf8
        $linked = $LASTEXITCODE -eq 0
        $ErrorActionPreference = 'Stop'
        Add-Result "$name-link" $linked @{}
    }
    if ($results | Where-Object { -not $_.passed }) { throw 'Import test compilation failed.' }
    Run-Unit 'import-lz4' @()
    Run-Unit 'import-stream' @()
    Run-Unit 'project-import' @($runDir)
    foreach ($file in @('paint/assets/meshes/cube_shared_uvs.arm', 'paint/assets/meshes/default/cube_bevel.arm')) {
        $label = [IO.Path]::GetFileNameWithoutExtension($file)
        $ErrorActionPreference = 'Continue'
        & "$runDir\project-import.exe" $runDir $file --check-only 2>&1 | Out-String |
            Set-Content -LiteralPath "$runDir\bundled-$label.log" -Encoding utf8
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        Add-Result "bundled-$label" ($code -eq 0) @{ exitCode = $code }
    }

    $vertices = [single[]]@(-0.5,-0.5,0, 0.5,-0.5,0, 0,0.5,0, 0,0,1, 0,0,1, 0,0,1, 0,0, 1,0, 0.5,1)
    $bytes = [byte[]]::new($vertices.Length * 4)
    [Buffer]::BlockCopy($vertices, 0, $bytes, 0, $bytes.Length)
    $json = @{
        asset = @{ version = '2.0' }; scene = 0; scenes = @(@{ nodes = @(0,1) })
        nodes = @(@{ mesh = 0; name = 'MeshA' }, @{ mesh = 0; name = 'MeshB'; translation = @(2,0,0) })
        meshes = @(@{ primitives = @(@{ attributes = @{ POSITION = 0; NORMAL = 1; TEXCOORD_0 = 2 }; material = 0 }) })
        materials = @(@{ name = 'Source Material' })
        buffers = @(@{ byteLength = 96; uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($bytes) })
        bufferViews = @(@{ buffer = 0; byteOffset = 0; byteLength = 36 }, @{ buffer = 0; byteOffset = 36; byteLength = 36 }, @{ buffer = 0; byteOffset = 72; byteLength = 24 })
        accessors = @(@{ bufferView = 0; componentType = 5126; count = 3; type = 'VEC3'; min = @(-0.5,-0.5,0); max = @(0.5,0.5,0) },
            @{ bufferView = 1; componentType = 5126; count = 3; type = 'VEC3' }, @{ bufferView = 2; componentType = 5126; count = 3; type = 'VEC2' })
    } | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText("$runDir\valid.gltf", $json)
    $variants = @('missing-buffer', 'short-buffer', 'short-accessor', 'multiple-primitives', 'late-invalid-node', 'cycle', 'bad-index')
    foreach ($name in $variants) {
        $g = $json | ConvertFrom-Json
        switch ($name) {
            'missing-buffer' { $g.buffers[0].uri = 'absent.bin' }
            'short-buffer' { [IO.File]::WriteAllBytes("$runDir\short.bin", [byte[]]@(1,2,3)); $g.buffers[0].uri = 'short.bin' }
            'short-accessor' { $g.accessors[1].count = 1 }
            'multiple-primitives' { $g.meshes[0].primitives = @($g.meshes[0].primitives[0], $g.meshes[0].primitives[0]) }
            'late-invalid-node' { $g.nodes[1].mesh = 1; $g.meshes += @{ primitives = @() } }
            'cycle' { $g.nodes[0] | Add-Member children @(1); $g.nodes[1] | Add-Member children @(0) }
            'bad-index' { $g.accessors[0].bufferView = 99 }
        }
        $file = "$runDir\$name.gltf"
        [IO.File]::WriteAllText($file, ($g | ConvertTo-Json -Depth 12))
        $ErrorActionPreference = 'Continue'
        & "$runDir\gltf-import.exe" "$runDir\valid.gltf" unused $file 2>&1 | Out-String |
            Set-Content -LiteralPath "$runDir\gltf-$name.log" -Encoding utf8
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        Add-Result "gltf-$name" ($code -eq 0) @{ exitCode = $code }
    }

    if (-not $SkipNative) {
        $Executable = (Resolve-Path -LiteralPath $Executable).Path
        $runtime = Join-Path $runDir 'runtime'
        New-Item -ItemType Directory -Path "$runtime\data" -Force | Out-Null
        Copy-Item -LiteralPath $Executable -Destination "$runtime\ArmorPaint.exe"
        Get-ChildItem -LiteralPath "$paintRoot\build\out\data" | Where-Object Name -ne 'config.json' |
            Copy-Item -Destination "$runtime\data" -Recurse
        Get-FileHash -LiteralPath $Executable -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath "$runDir\executable.sha256.json"
        [IO.File]::WriteAllText("$runDir\asset.svg", '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"><rect width="4" height="4" fill="#336699"/></svg>')
        $template = [IO.File]::ReadAllText("$PSScriptRoot\import-workflow.c.in")
        function Invoke-Native([string]$Name, [string]$Mode, [string]$Source, [string]$ErrorMarker = '') {
            $script = "$runDir\$Name.c"
            $code = $template.Replace('@MODE@', $Mode).Replace('@SOURCE@', $Source.Replace('\','/')).Replace('@FOLDER@', $runDir.Replace('\','/'))
            [IO.File]::WriteAllText($script, $code)
            $process = Start-Process -FilePath "$runtime\ArmorPaint.exe" -ArgumentList "--background --script `"$script`"" -WorkingDirectory $runtime `
                -WindowStyle Hidden -RedirectStandardOutput "$runDir\$Name.stdout.log" -RedirectStandardError "$runDir\$Name.stderr.log" -PassThru
            $null = $process.Handle
            $finished = $process.WaitForExit(30000)
            if (-not $finished) { Stop-Process -Id $process.Id; $process.WaitForExit() }
            $output = [IO.File]::ReadAllText("$runDir\$Name.stdout.log") + [IO.File]::ReadAllText("$runDir\$Name.stderr.log")
            $passed = $finished -and $process.ExitCode -eq 0 -and $output.Contains("IMPORT WORKFLOW PASS $Mode") -and
                -not $output.Contains('IMPORT CHECK FAIL') -and $output.Contains($ErrorMarker)
            Add-Result $Name $passed @{ exitCode = $process.ExitCode; timedOut = -not $finished }
        }
        Invoke-Native 'prepare' 'prepare' "$runDir\saved.arm"
        if (-not (Test-Path -LiteralPath "$runDir\saved.arm")) { throw 'Native fixture did not save.' }
        Run-Unit 'project-import' @($runDir, "$runDir\saved.arm")
        foreach ($file in Get-ChildItem -LiteralPath $runDir -Filter '*.arm') {
            if ($file.BaseName.StartsWith('valid-version-') -or $file.BaseName -eq 'saved') { Invoke-Native $file.BaseName 'read' $file.FullName }
            else { Invoke-Native $file.BaseName 'reject' $file.FullName 'Invalid or unsupported project data' }
        }
        foreach ($name in $variants) {
            Invoke-Native "native-$name" 'reject' "$runDir\$name.gltf" 'Invalid or incomplete mesh data'
        }
        Invoke-Native 'append-missing-buffer' 'append' "$runDir\missing-buffer.gltf" 'Invalid or incomplete mesh data'
        $exportDir = "$runDir\rejected export.v1"
        New-Item -ItemType Directory -Path $exportDir | Out-Null
        foreach ($file in @('truncated.arm', 'missing-buffer.gltf')) {
            $label = 'cli-' + [IO.Path]::GetFileNameWithoutExtension($file)
            $process = Start-Process -FilePath "$runtime\ArmorPaint.exe" `
                -ArgumentList "--background `"$runDir\$file`" --export-textures png base_color `"$exportDir`"" -WorkingDirectory $runtime -WindowStyle Hidden `
                -RedirectStandardOutput "$runDir\$label.stdout.log" -RedirectStandardError "$runDir\$label.stderr.log" -PassThru
            $null = $process.Handle
            $finished = $process.WaitForExit(30000)
            if (-not $finished) { Stop-Process -Id $process.Id; $process.WaitForExit() }
            $output = [IO.File]::ReadAllText("$runDir\$label.stdout.log") + [IO.File]::ReadAllText("$runDir\$label.stderr.log")
            $count = @(Get-ChildItem -LiteralPath $exportDir -File).Count
            Add-Result $label ($finished -and $process.ExitCode -eq 1 -and $count -eq 0 -and $output.Contains('import failed')) `
                @{ exitCode = $process.ExitCode; timedOut = -not $finished; exportFileCount = $count }
        }
    }
}
finally {
    Pop-Location
    Write-Host "Receipts: $runDir"
}
if ($results | Where-Object { -not $_.passed }) { throw 'Import regressions failed; see retained results and logs.' }
