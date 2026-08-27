[CmdletBinding()]
param(
    [string]$GameRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Split-Path -Parent $projectRoot
}
$dist = Join-Path $projectRoot "dist\$Configuration"
$dllSource = Join-Path $dist 'Mods\CombineDuplicateFurniture.dll'
$configSource = Join-Path $dist 'Mods\CombineDuplicateFurniture\config.json'
$descriptionSource = Join-Path $dist 'Mewtator\mods\CombineDuplicateFurniture\description.json'
$uiSource = Join-Path $dist 'Mewtator\mods\CombineDuplicateFurniture\swfs'
if (-not (Test-Path -LiteralPath $dllSource)) {
    throw 'Build output is missing. Run tools\build.ps1 first.'
}
if (-not (Test-Path -LiteralPath (Join-Path $uiSource 'cdf_prompt.swf')) -or
    -not (Test-Path -LiteralPath (Join-Path $uiSource 'swflist.gon.append'))) {
    throw 'In-game prompt assets are missing. Run tools\build.ps1 first.'
}
if (Get-Process -Name Mewgenics -ErrorAction SilentlyContinue) {
    throw 'Mewgenics is running. Exit the game before deployment.'
}

$modsRoot = Join-Path $GameRoot 'Mods'
$runtimeData = Join-Path $modsRoot 'CombineDuplicateFurniture'
$mewtatorData = Join-Path $GameRoot 'Mewtator\mods\CombineDuplicateFurniture'
New-Item -ItemType Directory -Force -Path $modsRoot | Out-Null
New-Item -ItemType Directory -Force -Path $runtimeData | Out-Null
New-Item -ItemType Directory -Force -Path $mewtatorData | Out-Null
Copy-Item -LiteralPath $dllSource -Destination (Join-Path $modsRoot 'CombineDuplicateFurniture.dll') -Force
if (-not (Test-Path -LiteralPath (Join-Path $runtimeData 'config.json'))) {
    Copy-Item -LiteralPath $configSource -Destination (Join-Path $runtimeData 'config.json')
}
Copy-Item -LiteralPath $descriptionSource -Destination (Join-Path $mewtatorData 'description.json') -Force
New-Item -ItemType Directory -Force -Path (Join-Path $mewtatorData 'swfs') | Out-Null
Copy-Item -LiteralPath (Join-Path $uiSource 'cdf_prompt.swf'), (Join-Path $uiSource 'swflist.gon.append') -Destination (Join-Path $mewtatorData 'swfs') -Force
Copy-Item -LiteralPath (Join-Path (Split-Path $uiSource -Parent) 'MewUI-LICENSE.txt') -Destination $mewtatorData -Force

if (-not (Test-Path -LiteralPath (Join-Path $modsRoot 'CombineDuplicateFurniture.dll')) -or
    -not (Test-Path -LiteralPath (Join-Path $runtimeData 'config.json')) -or
    -not (Test-Path -LiteralPath (Join-Path $mewtatorData 'description.json'))) {
    throw 'Deployment content check failed.'
}
Write-Host 'Deployed CombineDuplicateFurniture without changing Mewtator modlist.txt.'
