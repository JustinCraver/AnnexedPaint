# Focused P1-01 checks. Keep every run and its logs under paint/build/tests.
[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\build\out\ArmorPaint.exe'),
    [switch]$SkipNative
)

$ErrorActionPreference = 'Stop'
$paintRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $paintRoot -Parent
$runDir = Join-Path $paintRoot ('build\tests\save-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -products '*' -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Installed Visual Studio C++ tools are required.' }
$clang = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-cl.exe'

function Add-Result([string]$Name, [bool]$Passed, $Details) {
    $results.Add([pscustomobject]@{ name = $Name; passed = $Passed; details = $Details })
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$runDir\results.json"
    Write-Host "$Name`: passed=$Passed"
}

Push-Location $repoRoot
try {
    foreach ($name in @('file-save', 'project-save')) {
        $unitDir = Join-Path $runDir $name
        New-Item -ItemType Directory -Path $unitDir | Out-Null
        $compileArgs = @('/nologo', '/TC', '/O2', '/Gy', '/Gw', '/D_CRT_SECURE_NO_WARNINGS',
            '/FIiron_global.h', '/Ibase/sources', '/Ibase/sources/libs', "paint/tests/$name.c",
            "/Fe:$unitDir\$name.exe", "/Fo:$unitDir\")
        if ($name -eq 'project-save') {
            # These C compatibility flags also appear in the generated application project.
            $compileArgs += @('-Wno-incompatible-pointer-types', '-Wno-microsoft-enum-forward-reference',
                'base/sources/iron_string.c', 'base/sources/iron_array.c')
        }
        $compileArgs += @('/link', '/OPT:REF')
        & $clang @compileArgs 2>&1 | Set-Content -LiteralPath "$unitDir\compile.log"
        $compiled = $LASTEXITCODE -eq 0
        $exitCode = $null
        if ($compiled) {
            $unitArgs = @()
            if ($name -eq 'file-save') { $unitArgs += $unitDir }
            & "$unitDir\$name.exe" @unitArgs 2>&1 | Set-Content -LiteralPath "$unitDir\test.log"
            $exitCode = $LASTEXITCODE
        }
        Add-Result $name ($compiled -and $exitCode -eq 0) @{ compiled = $compiled; exitCode = $exitCode }
    }

    $interruptionDir = Join-Path $runDir 'interrupted'
    New-Item -ItemType Directory -Path $interruptionDir | Out-Null
    if (Test-Path -LiteralPath "$runDir\file-save\file-save.exe") {
        & "$runDir\file-save\file-save.exe" $interruptionDir --interrupt *> "$interruptionDir\test.log"
        $exitCode = $LASTEXITCODE
        $previous = Join-Path $interruptionDir ("save space $([char]0xe9).arm")
        $orphans = @(Get-ChildItem -LiteralPath $interruptionDir -File -Filter '*.tmp.*')
        $preserved = (Test-Path -LiteralPath $previous) -and [IO.File]::ReadAllText($previous) -eq 'previous project'
        Add-Result 'interrupted-write' ($exitCode -eq 73 -and $preserved -and $orphans.Count -eq 1 -and $orphans[0].Length -eq 5) @{
            exitCode = $exitCode; previousBytesPreserved = $preserved; orphanCount = $orphans.Count
        }
    }
    else { Add-Result 'interrupted-write' $false @{ reason = 'file-save test did not compile' } }

    if (-not $SkipNative) {
        $Executable = (Resolve-Path -LiteralPath $Executable).Path
        $runtime = Join-Path $runDir 'runtime'
        New-Item -ItemType Directory -Path "$runtime\data" -Force | Out-Null
        Copy-Item -LiteralPath $Executable -Destination "$runtime\ArmorPaint.exe"
        Get-ChildItem -LiteralPath "$paintRoot\build\out\data" | Where-Object Name -ne 'config.json' |
            Copy-Item -Destination "$runtime\data" -Recurse
        Get-FileHash -LiteralPath $Executable -Algorithm SHA256 | ConvertTo-Json |
            Set-Content -LiteralPath "$runDir\executable.sha256.json"
        $template = [IO.File]::ReadAllText("$PSScriptRoot\save-workflow.c.in")

        function Invoke-Native([string]$Name, [string]$Mode, [string]$Project, [string]$Asset = '', [int]$Packed = 0) {
            $script = Join-Path $runDir "$Name.c"
            $code = $template.Replace('@PROJECT@', $Project.Replace('\', '/')).Replace('@ASSET@', $Asset.Replace('\', '/')).
                Replace('@MODE@', $Mode).Replace('@PIXELS@', (Split-Path $Project -Parent).Replace('\', '/')).Replace('@PACKED@', "$Packed")
            [IO.File]::WriteAllText($script, $code)
            $stdout = Join-Path $runDir "$Name.stdout.log"
            $stderr = Join-Path $runDir "$Name.stderr.log"
            $process = Start-Process -FilePath "$runtime\ArmorPaint.exe" -ArgumentList "--background --script `"$script`"" `
                -WorkingDirectory $runtime -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
            $finished = $process.WaitForExit(30000)
            if (-not $finished) { Stop-Process -Id $process.Id; $process.WaitForExit() }
            $text = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
            $marker = if ($Mode -eq 'quit') { 'SAVE QUIT REQUEST' } else { "SAVE WORKFLOW PASS $Mode" }
            $passed = $finished -and $process.ExitCode -eq 0 -and $text.Contains($marker) -and -not $text.Contains('SAVE CHECK FAIL')
            if ($Mode -eq 'failure') {
                $passed = $passed -and $text.Contains('Could not save project') -and -not $text.Contains('Project saved')
            }
            elseif ($Mode -ne 'read') {
                $passed = $passed -and $text.Contains('Project saved') -and (Test-Path -LiteralPath $Project) -and (Get-Item -LiteralPath $Project).Length -gt 0
            }
            return [pscustomobject]@{ passed = $passed; exitCode = $process.ExitCode; timedOut = -not $finished }
        }

        $missing = Join-Path $runDir 'missing\project.arm'
        $result = Invoke-Native 'missing-parent' 'failure' $missing
        Add-Result 'missing-parent' ($result.passed -and -not (Test-Path -LiteralPath $missing)) $result
        foreach ($kind in @('locked', 'read-only')) {
            $project = Join-Path $runDir "$kind.arm"
            [IO.File]::WriteAllText($project, 'previous project bytes')
            $before = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
            $lock = $null
            try {
                if ($kind -eq 'locked') { $lock = [IO.File]::Open($project, 'Open', 'Read', 'Read') }
                else { (Get-Item -LiteralPath $project).IsReadOnly = $true }
                $result = Invoke-Native $kind 'failure' $project
            }
            finally {
                if ($lock) { $lock.Dispose() }
                if ($kind -eq 'read-only') { (Get-Item -LiteralPath $project).IsReadOnly = $false }
            }
            $after = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
            Add-Result $kind ($result.passed -and $before -eq $after) @{ process = $result; before = $before; after = $after }
        }

        foreach ($packed in @(0, 1)) {
            $fixtureDir = Join-Path $runDir "roundtrip-$packed space"
            New-Item -ItemType Directory -Path $fixtureDir | Out-Null
            New-Item -ItemType Directory -Path "$fixtureDir\before", "$fixtureDir\after" | Out-Null
            $asset = Join-Path $fixtureDir 'asset.svg'
            [IO.File]::WriteAllText($asset, '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"><rect width="4" height="4" fill="#336699"/></svg>')
            $project = Join-Path $fixtureDir 'project.arm'
            $result = Invoke-Native "write-$packed" 'write' $project $asset $packed
            Add-Result "write-$packed" $result.passed $result
            # Re-save an existing valid project, then read it in a fresh process.
            $result = Invoke-Native "replace-$packed" 'write' $project $asset $packed
            Add-Result "replace-$packed" $result.passed $result
            if ($packed) { Move-Item -LiteralPath $asset -Destination ($asset + '.offline') }
            $result = Invoke-Native "read-$packed" 'read' $project $asset $packed
            Add-Result "read-$packed" $result.passed $result
            $beforeFiles = @(Get-ChildItem -LiteralPath "$fixtureDir\before" -File -Filter '*.png')
            $afterFiles = @(Get-ChildItem -LiteralPath "$fixtureDir\after" -File -Filter '*.png')
            $matching = $beforeFiles.Count -gt 0 -and $beforeFiles.Count -eq $afterFiles.Count
            $hashes = @($beforeFiles | ForEach-Object {
                $beforeHash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
                $afterFile = Join-Path "$fixtureDir\after" $_.Name
                $afterHash = if (Test-Path -LiteralPath $afterFile) { (Get-FileHash -LiteralPath $afterFile -Algorithm SHA256).Hash } else { '' }
                if ($beforeHash -ne $afterHash) { $matching = $false }
                [pscustomobject]@{ file = $_.Name; before = $beforeHash; after = $afterHash }
            })
            Add-Result "pixels-$packed" $matching $hashes
        }
        $result = Invoke-Native 'save-and-quit' 'quit' (Join-Path $runDir 'quit.arm')
        Add-Result 'save-and-quit' $result.passed $result
        # An interrupted process cannot remove its partial sibling; retain that
        # receipt. Every completed write must clean up its own temporary file.
        $unexpectedTemps = @(Get-ChildItem -LiteralPath $runDir -Recurse -File -Filter '*.tmp.*' |
            Where-Object { $_.DirectoryName -ne $interruptionDir })
        Add-Result 'temporary-cleanup' ($unexpectedTemps.Count -eq 0) @{}
    }
}
finally {
    Pop-Location
    Write-Host "Receipts: $runDir"
}
if ($results | Where-Object { -not $_.passed }) { throw 'Save regressions failed; see results.json and per-check logs.' }
