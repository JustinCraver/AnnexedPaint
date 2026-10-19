# P1-02: real CLI status, decoded output, write failures and automatic shutdown.
[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\out\ArmorPaint.exe'),
    [switch]$SkipNative,
    [ValidateRange(1, 50)][int]$ShutdownRepeats = 12
)

$ErrorActionPreference = 'Stop'
$paintRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $paintRoot -Parent
$runDir = Join-Path $paintRoot ('build\tests\export-' + [guid]::NewGuid().ToString('N'))
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

Push-Location $repoRoot
try {
    foreach ($name in @('image-write', 'image-encode', 'export-inspect')) {
        $unitDir = Join-Path $runDir $name
        New-Item -ItemType Directory -Path $unitDir | Out-Null
        $ErrorActionPreference = 'Continue'
        & $clang /nologo /TC /O2 /Gy /Gw /D_CRT_SECURE_NO_WARNINGS /FIiron_global.h /Ibase/sources /Ibase/sources/libs `
            "paint/tests/$name.c" "/Fe:$unitDir\$name.exe" "/Fo:$unitDir\$name.obj" /link /OPT:REF 2>&1 |
            Out-String | Set-Content -LiteralPath "$unitDir\compile.log" -Encoding utf8
        $compiled = $LASTEXITCODE -eq 0
        $ErrorActionPreference = 'Stop'
        Add-Result "$name-compile" $compiled @{}
        if ($compiled -and $name -ne 'export-inspect') {
            $ErrorActionPreference = 'Continue'
            & "$unitDir\$name.exe" $unitDir 2>&1 | Out-String | Set-Content -LiteralPath "$unitDir\test.log" -Encoding utf8
            $unitExit = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            Add-Result $name ($unitExit -eq 0) @{ exitCode = $unitExit }
        }
    }

    if (-not $SkipNative) {
        $Executable = (Resolve-Path -LiteralPath $Executable).Path
        $runtime = Join-Path $runDir 'runtime'
        New-Item -ItemType Directory -Path "$runtime\data" -Force | Out-Null
        Copy-Item -LiteralPath $Executable -Destination "$runtime\ArmorPaint.exe"
        Get-ChildItem -LiteralPath "$paintRoot\build\out\data" | Where-Object Name -ne 'config.json' |
            Copy-Item -Destination "$runtime\data" -Recurse
        Get-FileHash -LiteralPath $Executable -Algorithm SHA256 | ConvertTo-Json |
            Set-Content -LiteralPath "$runDir\executable.sha256.json"
        Copy-Item -LiteralPath "$PSScriptRoot\export-workflow.c.in" -Destination "$runDir\prepare.c"

        function Invoke-Native([string]$Name, [string]$Arguments, [int]$ExpectedExit = 0, [string]$Marker = '') {
            $stdout = "$runDir\$Name.stdout.log"
            $stderr = "$runDir\$Name.stderr.log"
            $process = Start-Process -FilePath "$runtime\ArmorPaint.exe" -ArgumentList $Arguments -WorkingDirectory $runtime -WindowStyle Hidden `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
            $null = $process.Handle
            $finished = $process.WaitForExit(30000)
            if (-not $finished) { Stop-Process -Id $process.Id; $process.WaitForExit() }
            $text = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
            $passed = $finished -and $process.ExitCode -eq $ExpectedExit -and $text.Contains($Marker)
            if ($ExpectedExit -ne 0) { $passed = $passed -and -not $text.Contains('Textures exported') }
            Add-Result $Name $passed @{ exitCode = $process.ExitCode; timedOut = -not $finished }
        }

        function Inspect-Output([string]$Name, [string]$Folder, [string]$Type, [bool]$Known = $false) {
            $ext = if ($Type.StartsWith('exr')) { 'exr' } else { $Type }
            $files = @(Get-ChildItem -LiteralPath $Folder -File)
            $file = Join-Path $Folder "untitled_base.$ext"
            $passed = $files.Count -eq 1 -and (Test-Path -LiteralPath $file)
            if ($passed -and (Test-Path -LiteralPath "$runDir\export-inspect\export-inspect.exe")) {
                $decodeArgs = @($file, $Type)
                if ($Known) { $decodeArgs += 'known' }
                $ErrorActionPreference = 'Continue'
                & "$runDir\export-inspect\export-inspect.exe" @decodeArgs 2>&1 |
                    Out-String | Set-Content -LiteralPath "$runDir\$Name.decode.log" -Encoding utf8
                $passed = $LASTEXITCODE -eq 0
                $ErrorActionPreference = 'Stop'
            }
            else { $passed = $false }
            $hash = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { '' }
            Add-Result "$Name-output" $passed @{ count = $files.Count; sha256 = $hash }
        }

        Invoke-Native 'help' '--help' 0 'Usage: armorpaint'

        $writerFolder = Join-Path $runDir 'script writers.v1'
        New-Item -ItemType Directory -Path $writerFolder | Out-Null
        $writerPath = $writerFolder.Replace('\', '/')
        [IO.File]::WriteAllText("$runDir\writer-status.c", @"
int main() {
    buffer_t *pixels = buffer_create(16);
    for (int i = 0; i < 16; i++) { buffer_set_u8(pixels, i, 128); }
    if (!iron_write_png("$writerPath/good.png", pixels, 2, 2, 2)) { return 1; }
    if (!iron_write_jpg("$writerPath/good.jpg", pixels, 2, 2, 2, 95)) { return 2; }
    if (iron_write_png("$writerPath/missing/bad.png", pixels, 2, 2, 2)) { return 3; }
    if (iron_write_jpg("$writerPath/missing/bad.jpg", pixels, 2, 2, 2, 95)) { return 4; }
    printf("SCRIPT WRITER STATUS PASS");
    return 0;
}
"@)
        Invoke-Native 'script-writer-status' "--background --script `"$runDir\writer-status.c`"" 0 'SCRIPT WRITER STATUS PASS'
        $folder = Join-Path $runDir 'direct output.v1'
        New-Item -ItemType Directory -Path $folder | Out-Null
        Invoke-Native 'direct' "--background --export-textures png base_color `"$folder`"" 0 'Textures exported'
        Inspect-Output 'direct' $folder 'png'
        foreach ($type in @('png', 'jpg', 'exr16', 'exr32')) {
            $folder = Join-Path $runDir "$type output.v1"
            New-Item -ItemType Directory -Path $folder | Out-Null
            $cliFolder = $folder.Replace('\', '/')
            Invoke-Native "script-$type" "--background --export-textures $type base_color `"$cliFolder`" --script `"$runDir\prepare.c`"" 0 'EXPORT SCRIPT READY'
            Inspect-Output "script-$type" $folder $type $true
        }

        $folder = Join-Path $runDir 'generic preset.v1'
        New-Item -ItemType Directory -Path $folder | Out-Null
        Invoke-Native 'generic-preset' "--background --export-textures png generic `"$folder`"" 0 'Textures exported'
        $names = @(Get-ChildItem -LiteralPath $folder -File | Sort-Object Name | Select-Object -ExpandProperty Name)
        Add-Result 'exact-preset-channels' (($names -join ',') -eq 'untitled_base.png,untitled_metal.png,untitled_nor.png,untitled_occ.png,untitled_rough.png') $names

        $folder = Join-Path $runDir 'invalid output.v1'
        New-Item -ItemType Directory -Path $folder | Out-Null
        $asFile = Join-Path $runDir 'output-file'
        [IO.File]::WriteAllText($asFile, 'directory sentinel')
        $invalid = @(
            @('invalid-type', "--background --export-textures bad base_color `"$folder`"", 'Invalid texture type'),
            @('unknown-preset', "--background --export-textures png absent `"$folder`"", 'Unknown export preset'),
            @('missing-directory', "--background --export-textures png base_color `"$folder\missing`"", 'Invalid export directory'),
            @('file-directory', "--background --export-textures png base_color `"$asFile`"", 'Invalid export directory'),
            @('missing-type', '--background --export-textures', 'Missing arguments'),
            @('missing-preset', '--background --export-textures png', 'Missing arguments'),
            @('missing-path', '--background --export-textures png base_color', 'Missing arguments'),
            @('flag-as-value', '--export-textures png base_color --background', 'Missing arguments'),
            @('missing-script-value', '--background --script', 'Missing arguments'),
            @('missing-mesh-value', '--background --export-mesh', 'Missing arguments'),
            @('missing-material-value', '--background --export-material', 'Missing arguments'),
            @('conflicting-api', "--api --export-textures png base_color `"$folder`"", 'Use one export'),
            @('conflicting-exports', "--background --export-mesh `"$folder`" --export-textures png base_color `"$folder`"", 'Use one export'),
            @('unknown-flag', '--background --absent-option', 'Unknown argument')
        )
        foreach ($case in $invalid) { Invoke-Native $case[0] $case[1] 1 $case[2] }

        $scripts = @{
            'compile-error' = 'void main() { no_such_function(); }'
            'runtime-error' = 'void main() { int values[1]; int index = 1; printf("%d", values[index]); }'
            'callback-error' = 'void broken() { int values[1]; int index = 1; printf("%d", values[index]); } void main() { script_notify_on_next_frame(broken); }'
            'script-return' = 'int main() { return 7; }'
            'early-quit' = 'void main() { script_quit(); }'
        }
        foreach ($entry in $scripts.GetEnumerator()) {
            [IO.File]::WriteAllText("$runDir\$($entry.Key).c", $entry.Value)
            $marker = if ($entry.Key -eq 'early-quit') { 'Stopped before texture export completed' } else { 'Script failed' }
            Invoke-Native $entry.Key "--background --script `"$runDir\$($entry.Key).c`" --export-textures png base_color `"$folder`"" 1 $marker
        }
        Invoke-Native 'missing-script' "--background --script `"$runDir\absent.c`" --export-textures png base_color `"$folder`"" 1 'Invalid script path'
        Add-Result 'no-fallback-output' (@(Get-ChildItem -LiteralPath $folder -File).Count -eq 0) @{}

        foreach ($type in @('png', 'jpg', 'exr16', 'exr32')) {
            $folder = Join-Path $runDir "locked-$type"
            New-Item -ItemType Directory -Path $folder | Out-Null
            $ext = if ($type.StartsWith('exr')) { 'exr' } else { $type }
            $file = Join-Path $folder "untitled_base.$ext"
            [IO.File]::WriteAllText($file, 'locked texture sentinel')
            $before = (Get-FileHash -LiteralPath $file).Hash
            $lock = [IO.File]::Open($file, 'Open', 'Read', 'Read')
            try { Invoke-Native "locked-$type" "--background --export-textures $type base_color `"$folder`"" 1 'Texture export failed' }
            finally { $lock.Dispose() }
            Add-Result "locked-$type-preserved" ((Get-FileHash -LiteralPath $file).Hash -eq $before) @{}
        }

        $folder = Join-Path $runDir 'partial failure.v1'
        New-Item -ItemType Directory -Path $folder | Out-Null
        $file = Join-Path $folder 'untitled_nor.png'
        [IO.File]::WriteAllText($file, 'locked texture sentinel')
        $lock = [IO.File]::Open($file, 'Open', 'Read', 'Read')
        try { Invoke-Native 'partial-export-failure' "--background --export-textures png generic `"$folder`"" 1 'Texture export failed' }
        finally { $lock.Dispose() }
        Add-Result 'partial-export-evidence' ((Test-Path -LiteralPath "$folder\untitled_base.png") -and [IO.File]::ReadAllText($file) -eq 'locked texture sentinel') @{}

        # Retain the rich P1-01 fixture, but exercise the actual automatic CLI
        # shutdown path: no script_quit() shortcut after the assertions.
        $fixture = Join-Path $runDir 'automatic packed.v1'
        New-Item -ItemType Directory -Path "$fixture\before" -Force | Out-Null
        [IO.File]::WriteAllText("$fixture\asset.svg", '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"><rect width="4" height="4" fill="#336699"/></svg>')
        $code = [IO.File]::ReadAllText("$PSScriptRoot\save-workflow.c.in").Replace('@PROJECT@', "$fixture/project.arm".Replace('\', '/')).
            Replace('@ASSET@', "$fixture/asset.svg".Replace('\', '/')).
            Replace('@MODE@', 'write').Replace('@PIXELS@', $fixture.Replace('\', '/')).Replace('@PACKED@', '1')
        [IO.File]::WriteAllText("$runDir\automatic.c", $code)
        foreach ($i in 1..$ShutdownRepeats) {
            Invoke-Native "automatic-$i" "--background --script `"$runDir\automatic.c`"" 0 'SAVE WORKFLOW PASS write'
        }
    }
}
finally {
    Pop-Location
    Write-Host "Receipts: $runDir"
}
if ($results | Where-Object { -not $_.passed }) { throw 'Export regressions failed; see results.json and per-check logs.' }
