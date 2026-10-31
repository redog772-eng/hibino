param([string]$Root = 'C:\Users\User\starcraft-api\STARTcraft',
      [ValidateSet('Borderless','Windowed','Fullscreen')][string]$WindowMode = 'Borderless')

$ErrorActionPreference = 'Stop'
$arenaRoot = Join-Path $Root 'arena\STARTcraft_vs_Pluto'
$hostRoot = Join-Path $arenaRoot 'host'
$guestRoot = Join-Path $arenaRoot 'pluto'

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "File not found: $Path" }
}

function Start-BwapiGame([string]$Runtime, [int]$Left) {
    $inject = Join-Path $Runtime 'injectory_x86.exe'
    $launchArgs = @('--launch', 'StarCraft.exe', '--inject', 'bwapi-data\BWAPI.dll')
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $inject
    $info.Arguments = $launchArgs -join ' '
    $info.WorkingDirectory = $Runtime
    $info.UseShellExecute = $false
    # Official BWAPI 4.4.0 Config.cpp honors these before INI values. Scope
    # them to this injector/game, including the second process's position.
    $info.EnvironmentVariables['BWAPI_CONFIG_WINDOW__WINDOWED'] = $(if ($WindowMode -eq 'Fullscreen') { 'OFF' } else { 'ON' })
    $info.EnvironmentVariables['BWAPI_CONFIG_WINDOW__LEFT'] = [string]$Left
    $info.EnvironmentVariables['BWAPI_CONFIG_WINDOW__TOP'] = [string]$windowTop
    $info.EnvironmentVariables['BWAPI_CONFIG_WINDOW__WIDTH'] = [string]$windowWidth
    $info.EnvironmentVariables['BWAPI_CONFIG_WINDOW__HEIGHT'] = [string]$windowHeight
    [Diagnostics.Process]::Start($info) | Out-Null
}

function Sync-ArenaMaps {
    $sourceRoot = Join-Path $hostRoot 'maps'
    $targetRoot = Join-Path $guestRoot 'maps'
    if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
        throw "Host maps folder not found: $sourceRoot"
    }
    $sourceBase = [IO.Path]::GetFullPath($sourceRoot).TrimEnd('\') + '\'
    $maps = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.scx', '.scm') })
    if ($maps.Count -eq 0) { throw "No .scx/.scm maps found: $sourceRoot" }
    $backupRoot = Join-Path $arenaRoot ('map_backups\' + [Guid]::NewGuid().ToString('N'))
    $copied = 0
    foreach ($map in $maps) {
        $relative = $map.FullName.Substring($sourceBase.Length)
        $target = Join-Path $targetRoot $relative
        $expected = (Get-FileHash -LiteralPath $map.FullName -Algorithm SHA256).Hash
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $expected) {
                continue
            }
            $backup = Join-Path $backupRoot $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $backup -ErrorAction Stop
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $map.FullName -Destination $target -Force -ErrorAction Stop
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) {
            throw "Map verification failed: $target"
        }
        ++$copied
    }
    Write-Host "Maps verified: $($maps.Count); copied or updated: $copied"
    if (Test-Path -LiteralPath $backupRoot) { Write-Host "Previous maps backed up: $backupRoot" }
}

try {
    $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -in @('StarCraft', 'StarterBot', 'pluto_infer')
    })
    if ($running.Count -gt 0) {
        throw 'Close every existing StarCraft, StarterBot, and Pluto process first.'
    }
    Require-File (Join-Path $arenaRoot 'arena_manifest.json')
    Require-File (Join-Path $hostRoot 'bin\StarterBot.exe')
    Require-File (Join-Path $guestRoot 'bwapi-data\AI\pluto.dll')
    Require-File (Join-Path $guestRoot 'bwapi-data\AI\pluto\pluto_infer.exe')
    Require-File (Join-Path $guestRoot 'bwapi-data\AI\pluto\pluto_weights.bin')
    $windowHelper = Join-Path $PSScriptRoot 'configure_arena_windows.py'
    Require-File $windowHelper
    $windowWidth = 640; $windowHeight = 480; $windowLeft = 0; $windowTop = 0
    $guestLeft = 680
    if ($WindowMode -eq 'Borderless') {
        $displayHelper = Join-Path $PSScriptRoot 'arena_display.ps1'
        Require-File $displayHelper
        . $displayHelper
        Initialize-ArenaDisplay
        $screenBounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $windowWidth = $screenBounds.Width; $windowHeight = $screenBounds.Height
        $windowLeft = $screenBounds.X; $windowTop = $screenBounds.Y
        $guestLeft = $windowLeft
    }
    & python $windowHelper --root $Root --mode $WindowMode --width $windowWidth --height $windowHeight --left $windowLeft --top $windowTop
    if ($LASTEXITCODE -ne 0) { throw 'Could not configure matching window modes.' }
    Sync-ArenaMaps

    Write-Host 'Starting STARTcraft AI client first...'
    $botDirectory = Join-Path $hostRoot 'bin'
    Start-Process -FilePath (Join-Path $botDirectory 'StarterBot.exe') -WorkingDirectory $botDirectory | Out-Null
    Start-Sleep -Seconds 2
    if (-not (Get-Process -Name StarterBot -ErrorAction SilentlyContinue)) { throw 'StarterBot exited before Pluto was launched.' }

    Write-Host 'Launching the StarCraft instance that STARTcraft will control...'
    Start-BwapiGame $hostRoot $windowLeft
    Start-Sleep -Seconds 5
    if (-not (Get-Process -Name StarCraft -ErrorAction SilentlyContinue)) { throw 'STARTcraft StarCraft process did not start.' }
    if (-not (Get-Process -Name StarterBot -ErrorAction SilentlyContinue)) { throw 'StarterBot could not attach to the STARTcraft BWAPI instance.' }

    Write-Host 'Launching Pluto opponent...'
    Start-BwapiGame $guestRoot $guestLeft
    Start-Sleep -Seconds 3
    if (@(Get-Process -Name StarCraft -ErrorAction SilentlyContinue).Count -lt 2) { throw 'The second StarCraft process did not start.' }
    if ($WindowMode -eq 'Borderless') { Set-ArenaBorderless @($hostRoot, $guestRoot) $screenBounds }

    Write-Host 'SUCCESS: STARTcraft vs Pluto match is starting.'
    Write-Host "Requested display mode for BOTH instances: $WindowMode"
    Write-Host "Replay: $hostRoot\Replays\STARTcraft_vs_Pluto.rep"
    Write-Host "Pluto logs: $guestRoot\pluto.log and pluto_infer.log"
} catch {
    Write-Host "ERROR: $($_.Exception.Message)"
    exit 1
}

