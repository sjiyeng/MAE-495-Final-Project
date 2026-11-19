# =============================================================================
# ASME e-HPVC Fast Incremental Builder & Auto-Port Deployer (Wheel Sensor Test)
# =============================================================================
param(
  [switch]$Upload,
  [string]$Port = ""
)

$ErrorActionPreference = "Stop"

# 1. Locate bundled arduino-cli.exe from Arduino IDE 2.x
$CliCandidates = @(
  "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)
$ArduinoCli = $CliCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $ArduinoCli) {
  Write-Host "[ERROR] Could not find arduino-cli.exe!" -ForegroundColor Red
  exit 1
}

# 2. Board FQBN (matched to root fast_build.ps1 so shared core.a cache is reused)
$FQBN = "esp32:esp32:XIAO_ESP32S3:CDCOnBoot=cdc,PSRAM=opi"
$SketchDir = $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$CacheRoot = Join-Path $WorkspaceRoot ".build_cache"
$Name = "test_wheel_sensor"
$BuildPath = Join-Path $CacheRoot $Name

function Draw-Bar([int]$Percent, [int]$Width = 22) {
  $Filled = [math]::Round(($Percent / 100.0) * $Width)
  if ($Filled -gt $Width) { $Filled = $Width }
  if ($Filled -lt 0) { $Filled = 0 }
  $Empty = $Width - $Filled
  return "[" + ("#" * $Filled) + ("-" * $Empty) + "]"
}

function Seed-CacheFromSibling([string]$TargetBuildPath) {
  $TargetCore = Join-Path $TargetBuildPath "core\core.a"
  if (Test-Path $TargetCore) { return }

  if (Test-Path $CacheRoot) {
    $Sibling = Get-ChildItem -Path $CacheRoot -Directory -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -ne $TargetBuildPath -and (Test-Path (Join-Path $_.FullName "core\core.a")) } |
      Select-Object -First 1

    if ($Sibling) {
      Write-Host "[CACHE SHARE] Seeding ESP32S3 core & libraries cache from $($Sibling.Name)..." -ForegroundColor Magenta
      New-Item -ItemType Directory -Path $TargetBuildPath -Force | Out-Null
      Copy-Item -Path (Join-Path $Sibling.FullName "core") -Destination $TargetBuildPath -Recurse -Force -ErrorAction SilentlyContinue
      if (Test-Path (Join-Path $Sibling.FullName "libraries")) {
        Copy-Item -Path (Join-Path $Sibling.FullName "libraries") -Destination $TargetBuildPath -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
  }
}

function Find-XiaoComPort {
  # 1. Try Win32_PnPEntity for Espressif (VID_303A) or Seeed XIAO (VID_2886)
  $Ports = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '\((COM\d+)\)' -and ($_.DeviceID -match 'VID_303A|VID_2886' -or $_.Name -match 'USB JTAG|Serial') }
  foreach ($p in $Ports) {
    if ($p.Name -match '\((COM\d+)\)') {
      return $Matches[1]
    }
  }

  # 2. Try arduino-cli board list
  try {
    $BoardList = & $ArduinoCli board list --format json 2>$null | ConvertFrom-Json
    foreach ($entry in $BoardList.detected_ports) {
      if ($entry.port.address -match '^COM\d+$') {
        $isEsp = ($entry.matching_boards | Where-Object { $_.fqbn -match 'esp32|XIAO_ESP32S3' })
        if ($isEsp -or $entry.port.protocol -eq 'serial') {
          return $entry.port.address
        }
      }
    }
  } catch {}

  # 3. Fallback to .NET SerialPort enumeration
  try {
    $SerialPorts = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    if ($SerialPorts.Count -gt 0) {
      return $SerialPorts[0]
    }
  } catch {}

  return $null
}

$CoreArchive = Join-Path $BuildPath "core\core.a"
$BinFile     = Join-Path $BuildPath "$Name.ino.bin"

if (-not (Test-Path $BuildPath)) {
  New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
}

Seed-CacheFromSibling $BuildPath
$IsCached = Test-Path $CoreArchive

if (-not $IsCached) {
  Write-Host ""
  Write-Host "[CACHE INIT] First build for $Name - compiling libraries & ESP32S3 core..." -ForegroundColor Yellow
} else {
  Write-Host ""
  Write-Host "[FAST BUILD] Incremental compile for $Name using cached core & libraries..." -ForegroundColor Cyan
}

$StdOutLog = Join-Path $CacheRoot "${Name}_stdout.log"
$StdErrLog = Join-Path $CacheRoot "${Name}_stderr.log"
if (Test-Path $StdOutLog) { Remove-Item $StdOutLog -Force -ErrorAction SilentlyContinue }
if (Test-Path $StdErrLog) { Remove-Item $StdErrLog -Force -ErrorAction SilentlyContinue }
if (Test-Path $BinFile)   { Remove-Item $BinFile   -Force -ErrorAction SilentlyContinue }

$Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

$ArgList = @(
  "compile",
  "--fqbn", $FQBN,
  "--build-path", "`"$BuildPath`"",
  "--jobs", "0",
  "`"$SketchDir`""
)

$Proc = Start-Process -FilePath $ArduinoCli -ArgumentList $ArgList -NoNewWindow -PassThru `
  -RedirectStandardOutput $StdOutLog -RedirectStandardError $StdErrLog
$null = $Proc.Handle

while (-not $Proc.HasExited) {
  Start-Sleep -Milliseconds 250
  $Elapsed = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 1)
  $Pct = [math]::Min(95, [int](($Elapsed / 22.0) * 90.0))
  if ($Pct -lt 5) { $Pct = 5 }
  $Bar = Draw-Bar $Pct 22
  Write-Host ("`r$Bar {0,3}% | Compiling $Name... | {1}s   " -f $Pct, $Elapsed) -NoNewline -ForegroundColor Cyan
}

$Proc.WaitForExit()
$Stopwatch.Stop()
$ElapsedSec = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 2)

$BuildSucceeded = (Test-Path $BinFile) -and ($Proc.ExitCode -eq 0 -or $null -eq $Proc.ExitCode)
if (-not $BuildSucceeded) {
  Write-Host ""
  Write-Host "[FAILED] Compiler Error Output for ${Name}:" -ForegroundColor Red
  if (Test-Path $StdErrLog) { Get-Content $StdErrLog | Write-Host -ForegroundColor Red }
  if (Test-Path $StdOutLog) { Get-Content $StdOutLog | Write-Host -ForegroundColor Gray }
  exit 1
}

$FinalBar = Draw-Bar 100 22
Write-Host ("`r$FinalBar 100% | [SUCCESS] $Name compiled in $ElapsedSec seconds!                   ") -ForegroundColor Green

if (Test-Path $StdOutLog) {
  Get-Content $StdOutLog | Where-Object { $_ -match "Sketch uses|Global variables use" } | ForEach-Object {
    Write-Host ("  " + $_) -ForegroundColor DarkGray
  }
}

if ($Upload) {
  $TargetPort = $Port
  if (-not $TargetPort) {
    $TargetPort = Find-XiaoComPort
    if ($TargetPort) {
      Write-Host "[AUTO-PORT] Detected XIAO ESP32S3 on $TargetPort" -ForegroundColor Cyan
    } else {
      Write-Host "[ERROR] No XIAO ESP32S3 COM port detected. Plug in the board and try again." -ForegroundColor Red
      exit 1
    }
  }

  Write-Host "[UPLOADING] Flashing $Name to $TargetPort..." -ForegroundColor Cyan
  & $ArduinoCli upload --fqbn $FQBN --port $TargetPort --input-dir $BuildPath $SketchDir

  if ($LASTEXITCODE -eq 0) {
    Write-Host "[UPLOAD COMPLETE] $Name is now running on $TargetPort!" -ForegroundColor Green
  } else {
    Write-Host "[UPLOAD FAILED] Make sure Serial Monitor is closed on $TargetPort." -ForegroundColor Red
    exit $LASTEXITCODE
  }
}
