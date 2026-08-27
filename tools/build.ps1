[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$dumpbinRoot = Join-Path $vsRoot 'VC\Tools\MSVC'
if (-not (Test-Path -LiteralPath $cmake) -or
    -not (Test-Path -LiteralPath $ctest) -or
    -not (Test-Path -LiteralPath $ninja) -or
    -not (Test-Path -LiteralPath $vsDevCmd)) {
    throw 'Visual Studio CMake, Ninja, or developer environment was not found.'
}

$environmentLines = & $env:ComSpec /d /s /c "`"$vsDevCmd`" -no_logo -arch=x64 -host_arch=x64 && set"
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio developer environment failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:CMAKE_MAKE_PROGRAM = $ninja

$buildDirectory = Join-Path $projectRoot 'build-ninja'
& $cmake -S $projectRoot -B $buildDirectory -G 'Ninja Multi-Config'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& $ctest --test-dir $buildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Focused tests failed.' }
$gameRoot = Split-Path -Parent $projectRoot
& (Join-Path $buildDirectory "$Configuration\cdf_catalog_probe.exe") (Join-Path $gameRoot 'resources.gpak')
if ($LASTEXITCODE -ne 0) { throw 'Local furniture catalog probe failed.' }

$dist = Join-Path $projectRoot "dist\$Configuration"
$runtimeData = Join-Path $dist 'Mods\CombineDuplicateFurniture'
$mewtatorData = Join-Path $dist 'Mewtator\mods\CombineDuplicateFurniture'
New-Item -ItemType Directory -Force -Path (Join-Path $dist 'Mods') | Out-Null
New-Item -ItemType Directory -Force -Path $runtimeData | Out-Null
New-Item -ItemType Directory -Force -Path $mewtatorData | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDirectory "out\$Configuration\CombineDuplicateFurniture.dll") -Destination (Join-Path $dist 'Mods') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'config\default_config.json') -Destination (Join-Path $runtimeData 'config.json') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\description.json') -Destination $mewtatorData -Force
New-Item -ItemType Directory -Force -Path (Join-Path $mewtatorData 'swfs') | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDirectory 'generated\cdf_prompt.swf') -Destination (Join-Path $mewtatorData 'swfs') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\swfs\swflist.gon.append') -Destination (Join-Path $mewtatorData 'swfs') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\mew_ui_api\LICENSE') -Destination (Join-Path $mewtatorData 'MewUI-LICENSE.txt') -Force

$dumpbin = Get-ChildItem -LiteralPath $dumpbinRoot -Filter dumpbin.exe -Recurse |
    Where-Object FullName -Match '\\bin\\Hostx64\\x64\\dumpbin.exe$' |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) { throw 'x64 dumpbin.exe was not found.' }
$dll = Join-Path $dist 'Mods\CombineDuplicateFurniture.dll'
$exports = (& $dumpbin /exports $dll) -join "`n"
if ($exports -notmatch 'CombineDuplicateFurniture_Initialize' -or
    $exports -notmatch 'CombineDuplicateFurniture_Shutdown') {
    throw 'Required DLL exports are missing.'
}
$headers = (& $dumpbin /headers $dll) -join "`n"
if ($headers -notmatch 'machine \(x64\)') {
    throw 'Built DLL is not x64.'
}
Write-Host "Built and focused-tested: $dll"
