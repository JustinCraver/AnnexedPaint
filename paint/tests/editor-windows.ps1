# P2-04: repeatable interpreter and native editor checks with retained evidence.
[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\out\ArmorPaint.exe'),
    [switch]$SkipNative,
    [switch]$InjectFailure
)
$ErrorActionPreference = 'Stop'
$paintRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $paintRoot -Parent
$runDir = Join-Path $paintRoot ('build\tests\editor-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result([string]$Name, [bool]$Passed, $Details) {
    $results.Add([pscustomobject]@{ name = $Name; passed = $Passed; details = $Details })
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$runDir\results.json"
    Write-Host "$Name`: passed=$Passed"
}

function Invoke-Checked([string]$Name, [string]$Program, [string]$Arguments, [int]$ExpectedExit, [string]$Marker, [string]$WorkingDirectory) {
    $stdout = "$runDir\$Name.stdout.log"
    $stderr = "$runDir\$Name.stderr.log"
    $start = @{ FilePath = $Program; WorkingDirectory = $WorkingDirectory; WindowStyle = 'Hidden';
        RedirectStandardOutput = $stdout; RedirectStandardError = $stderr; PassThru = $true }
    if ($Arguments) { $start.ArgumentList = $Arguments }
    $process = Start-Process @start
    $null = $process.Handle
    $finished = $process.WaitForExit(30000)
    if (-not $finished) { Stop-Process -Id $process.Id; $process.WaitForExit() }
    $output = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
    $passed = $finished -and $process.ExitCode -eq $ExpectedExit -and $output.Contains($Marker)
    if ($ExpectedExit -eq 0) { $passed = $passed -and -not ($output -match 'TEST FAIL|\[test\]\s+FAIL|PIXELS FAIL|^\d+: FAIL') }
    Add-Result $Name $passed @{ exitCode = $process.ExitCode; expectedExit = $ExpectedExit; timedOut = -not $finished; marker = $Marker }
}

function Compare-Pixels([string]$Name, [string]$Mode, [string]$Before, [string]$After, [int]$ExpectedExit = 0) {
    Invoke-Checked $Name "$runDir\editor-inspect.exe" "$Mode `"$Before`" `"$After`"" $ExpectedExit `
        $(if ($ExpectedExit -eq 0) { 'PIXELS PASS' } else { 'PIXELS FAIL' }) $runDir
}

Push-Location $repoRoot
try {
    $vsRoot = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products '*' -latest `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Installed Visual Studio C++ tools are required.' }
    $clang = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
    & $clang --version | Set-Content -LiteralPath "$runDir\toolchain.txt"
    $sources = @('base/sources/libs/minic.c', 'base/sources/libs/minic.h', 'base/sources/libs/minic_tests.c',
        'base/sources/iron_math.c', 'base/sources/iron_array.c', 'base/sources/iron_string.c',
        'paint/tests/minic-runner.c', 'paint/tests/editor-inspect.c',
        'paint/tests/editor-windows.ps1', 'paint/assets/plugins/dev/test.c', 'paint/sources/minic_api.c',
        'paint/sources/minic_api_list.h', 'paint/sources/minic_impl.c', 'paint/sources/functions.h')
    Get-FileHash -LiteralPath $sources | ConvertTo-Json | Set-Content -LiteralPath "$runDir\source-hashes.json"
    @{ nativeRequested = -not $SkipNative; injectFailure = [bool]$InjectFailure; timeoutMilliseconds = 30000 } |
        ConvertTo-Json | Set-Content -LiteralPath "$runDir\scope.json"

    $objects = @()
    foreach ($source in @('base/sources/libs/minic.c', 'base/sources/libs/minic_tests.c', 'base/sources/iron_math.c',
        'base/sources/iron_array.c', 'base/sources/iron_string.c', 'paint/tests/minic-runner.c')) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source)
        $object = "$runDir\$name.obj"
        $ErrorActionPreference = 'Continue'
        & $clang /nologo /TC /c /O2 /Gy /Gw /D_CRT_SECURE_NO_WARNINGS /FIiron_global.h /Ibase/sources /Ibase/sources/libs `
            -Wno-incompatible-pointer-types $source "/Fo:$object" 2>&1 | Out-String | Set-Content -LiteralPath "$runDir\$name-compile.log" -Encoding utf8
        $compiled = $LASTEXITCODE -eq 0
        $ErrorActionPreference = 'Stop'
        Add-Result "$name-compile" $compiled @{}
        $objects += $object
    }
    if ($results | Where-Object { -not $_.passed }) { throw 'Interpreter compilation failed.' }
    $ErrorActionPreference = 'Continue'
    & $clang /nologo @objects "/Fe:$runDir\minic-runner.exe" /link /OPT:REF 2>&1 | Out-String |
        Set-Content -LiteralPath "$runDir\minic-link.log" -Encoding utf8
    $linked = $LASTEXITCODE -eq 0
    $ErrorActionPreference = 'Stop'
    Add-Result 'minic-link' $linked @{}
    if (-not $linked) { throw 'Interpreter link failed.' }
    $arguments = if ($InjectFailure) { '--inject-failure' } else { '' }
    Invoke-Checked 'minic' "$runDir\minic-runner.exe" $arguments 0 'MINIC: 39 checks, 0 failures' $runDir
    Invoke-Checked 'minic-failure-control' "$runDir\minic-runner.exe" '--inject-failure' 1 'MINIC: 39 checks, 1 failures' $runDir

    if (-not $SkipNative) {
        $ErrorActionPreference = 'Continue'
        & $clang /nologo /TC /O2 /D_CRT_SECURE_NO_WARNINGS /Ibase/sources/libs paint/tests/editor-inspect.c `
            "/Fe:$runDir\editor-inspect.exe" "/Fo:$runDir\editor-inspect.obj" 2>&1 | Out-String |
            Set-Content -LiteralPath "$runDir\editor-inspect-compile.log" -Encoding utf8
        $compiled = $LASTEXITCODE -eq 0
        $ErrorActionPreference = 'Stop'
        Add-Result 'editor-inspect-compile' $compiled @{}
        if (-not $compiled) { throw 'Pixel inspector compilation failed.' }

        $Executable = (Resolve-Path -LiteralPath $Executable).Path
        $runtime = Join-Path $runDir 'runtime'
        New-Item -ItemType Directory -Path "$runtime\data" -Force | Out-Null
        Copy-Item -LiteralPath $Executable -Destination "$runtime\ArmorPaint.exe"
        Get-ChildItem -LiteralPath "$paintRoot\build\out\data" | Where-Object Name -ne 'config.json' |
            Copy-Item -Destination "$runtime\data" -Recurse
        Get-FileHash -LiteralPath $Executable | ConvertTo-Json | Set-Content -LiteralPath "$runDir\executable.sha256.json"
        $fixture = Join-Path $runDir 'editor output.v1'
        foreach ($folder in @('', 'known', 'textures', 'after', 'resaved', 'fresh')) {
            New-Item -ItemType Directory -Path (Join-Path $fixture $folder) -Force | Out-Null
        }
        $code = [IO.File]::ReadAllText("$paintRoot\assets\plugins\dev\test.c")
        foreach ($mode in @('failure', 'full', 'reopen')) {
            $script = "$runDir\$mode.c"
            $rootC = $fixture.Replace('\', '/').Replace('"', '\"')
            [IO.File]::WriteAllText($script, $code + "`nvoid main() { test_begin(`"$rootC`", `"$mode`"); }`n")
            $status = if ($mode -eq 'failure') { 1 } else { 0 }
            $marker = if ($mode -eq 'failure') { 'TEST FAIL failure' } else { "TEST PASS $mode" }
            Invoke-Checked "editor-$mode" "$runtime\ArmorPaint.exe" "--background --script `"$script`"" $status $marker $runtime
        }

        # A no-op stroke, wrong undo snapshot or corrupt reopen must fail even
        # when a native process prints its success marker and exits zero.
        $pairs = @(
            @('paint-changes-pixels', 'changed', 'empty', 'paint'),
            @('undo-restores-pixels', 'equal', 'empty', 'undo'),
            @('redo-restores-pixels', 'equal', 'paint', 'redo'),
            @('eraser-reduces-alpha', 'erased', 'redo', 'erased'),
            @('world-stroke-changes-pixels', 'changed', 'erased', 'world'),
            @('fill-changes-pixels', 'changed', 'world', 'filled'),
            @('save-reopen-pixels', 'equal', 'filled', 'reopened'),
            @('repaint-changes-pixels', 'erased', 'reopened', 'resaved'),
            @('resave-fresh-process-pixels', 'equal', 'resaved', 'fresh')
        )
        foreach ($pair in $pairs) {
            Compare-Pixels $pair[0] $pair[1] "$fixture\$($pair[2]).png" "$fixture\$($pair[3]).png"
        }
        Compare-Pixels 'pixel-failure-control' 'equal' "$fixture\empty.png" "$fixture\paint.png" 1
        Compare-Pixels 'missing-output-control' 'equal' "$fixture\missing.png" "$fixture\paint.png" 1
        foreach ($folder in @('known', 'textures', 'after', 'resaved', 'fresh')) {
            $count = @(Get-ChildItem -LiteralPath "$fixture\$folder" -File).Count
            Add-Result "$folder-output-count" ($count -eq 5) @{ expected = 5; actual = $count }
        }
        foreach ($channel in @('base', 'nor', 'occ', 'rough', 'metal')) {
            $known = @(Get-ChildItem -LiteralPath "$fixture\known" -Filter "*_$channel.png" -File)
            Add-Result "known-$channel-output" ($known.Count -eq 1) @{ count = $known.Count }
            if ($known.Count -eq 1) {
                Invoke-Checked "known-$channel-pixels" "$runDir\editor-inspect.exe" "known `"$($known[0].FullName)`" $channel" 0 'PIXELS PASS' $runDir
            }
            foreach ($pair in @(@('textures', 'after'), @('resaved', 'fresh'))) {
                $left = @(Get-ChildItem -LiteralPath "$fixture\$($pair[0])" -Filter "*_$channel.png" -File)
                $right = @(Get-ChildItem -LiteralPath "$fixture\$($pair[1])" -Filter "*_$channel.png" -File)
                $unique = $left.Count -eq 1 -and $right.Count -eq 1
                Add-Result "$($pair[0])-$channel-output" $unique @{ beforeCount = $left.Count; afterCount = $right.Count }
                if ($unique) {
                    Compare-Pixels "$($pair[0])-$channel-roundtrip" 'equal' $left[0].FullName $right[0].FullName
                }
            }
        }
        $base = @(Get-ChildItem -LiteralPath "$fixture\known" -Filter '*_base.png' -File)
        if ($base.Count -eq 1) {
            Invoke-Checked 'known-value-failure-control' "$runDir\editor-inspect.exe" "known `"$($base[0].FullName)`" nor" 1 'PIXELS FAIL' $runDir
        }
        Get-ChildItem -LiteralPath $fixture -Recurse -File | Get-FileHash | ConvertTo-Json |
            Set-Content -LiteralPath "$runDir\output-hashes.json"
    }
}
catch {
    Add-Result 'runner-error' $false @{ message = $_.Exception.Message }
    throw
}
finally {
    Pop-Location
    Write-Host "Receipts: $runDir"
}
if ($results | Where-Object { -not $_.passed }) { throw 'Editor/interpreter checks failed; see results.json and per-check logs.' }
