# =============================================================================
# ASME e-HPVC Fast Incremental Builder & Uploader (Persistent Disk Cache)
# Monitors .o object files in real time (every 250 ms) to display a live
# visual progress bar and shares cached core/libraries between TX and RX.
# =============================================================================
param(
  [ValidateSet("rx", "tx", "ble", "zig", "both")]
  [string]$Target = "rx",

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

# 2. Board FQBN (Seeed Studio XIAO ESP32S3 with USB CDC On Boot Enabled)
$FQBN = "esp32:esp32:XIAO_ESP32S3:CDCOnBoot=cdc"
$WorkspaceRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$CacheRoot = Join-Path $WorkspaceRoot ".build_cache"

function Draw-Bar([int]$Percent, [int]$Width = 24) {
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
      Write-Host "[CACHE SHARE] Seeding ESP32S3 core & RadioLib cache from $($Sibling.Name)..." -ForegroundColor Magenta
      New-Item -ItemType Directory -Path $TargetBuildPath -Force | Out-Null
      Copy-Item -Path (Join-Path $Sibling.FullName "core") -Destination $TargetBuildPath -Recurse -Force -ErrorAction SilentlyContinue
      if (Test-Path (Join-Path $Sibling.FullName "libraries")) {
        Copy-Item -Path (Join-Path $Sibling.FullName "libraries") -Destination $TargetBuildPath -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
  }
}

function Build-Target([string]$Name, [string]$SketchDir) {
  $BuildPath   = Join-Path $CacheRoot $Name
  $LibPath     = Join-Path $BuildPath "libraries"
  $CorePath    = Join-Path $BuildPath "core"
  $CoreArchive = Join-Path $CorePath "core.a"
  $BinFile     = Join-Path $BuildPath "$Name.ino.bin"

  if (-not (Test-Path $BuildPath)) {
    New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
  }

  # If this target hasn't been built yet, seed its cache from any sibling target (e.g. TX -> RX)
  Seed-CacheFromSibling $BuildPath
  $IsCached = Test-Path $CoreArchive

  $StartTime = Get-Date
  if (-not $IsCached) {
    Write-Host ""
    Write-Host "[CACHE INIT] First build for $Name - compiling libraries & ESP32S3 core into cache..." -ForegroundColor Yellow
  } else {
    Write-Host ""
    Write-Host "[FAST BUILD] Incremental compile for $Name using cached core & libraries..." -ForegroundColor Cyan
  }

  $StdOutLog = Join-Path $BuildPath "build_stdout.log"
  $StdErrLog = Join-Path $BuildPath "build_stderr.log"
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
  # Keep process handle alive in PowerShell 5.1 so ExitCode is populated
  $null = $Proc.Handle

  $ExpectedLibObjs  = 78
  $ExpectedCoreObjs = 72
  $TotalExpected    = $ExpectedLibObjs + $ExpectedCoreObjs

  while (-not $Proc.HasExited) {
    Start-Sleep -Milliseconds 250
    $Elapsed = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 1)

    $LibObjs  = @()
    $CoreObjs = @()
    if (Test-Path $LibPath) {
      $LibObjs = @(Get-ChildItem -Path $LibPath -Recurse -Filter "*.o" -ErrorAction SilentlyContinue)
    }
    if (Test-Path $CorePath) {
      $CoreObjs = @(Get-ChildItem -Path $CorePath -Recurse -Filter "*.o" -ErrorAction SilentlyContinue)
    }

    $LibCount  = $LibObjs.Count
    $CoreCount = $CoreObjs.Count
    $TotalCount = $LibCount + $CoreCount

    $NewestFile = "Scanning & compiling..."
    $AllObjs = $LibObjs + $CoreObjs
    if ($AllObjs.Count -gt 0) {
      $LatestObj = $AllObjs | Sort-Object LastWriteTime -Descending | Select-Object -First 1
      if ($LatestObj -and $LatestObj.LastWriteTime -ge $StartTime.AddSeconds(-2)) {
        $NewestFile = $LatestObj.Name -replace '\.(cpp|c)\.o$', '.$1'
      } elseif ($IsCached) {
        $NewestFile = "Compiling sketch & linking..."
      }
    }

    if (-not $IsCached) {
      if ($TotalCount -eq 0) {
        $Pct = [math]::Min(12, [int]($Elapsed * 1.5))
      } else {
        $Pct = 12 + [int](($TotalCount / [double]$TotalExpected) * 82.0)
        if ($Pct -gt 95) { $Pct = 95 }
      }
    } else {
      $Pct = [math]::Min(95, [int](($Elapsed / 22.0) * 90.0))
      if ($Pct -lt 5) { $Pct = 5 }
    }

    $Bar = Draw-Bar $Pct 22
    $StatusLine = "`r$Bar {0,3}% | Libs: {1,2}/{2} .o | Core: {3,2}/{4} .o | {5,-28} | {6}s   " -f `
      $Pct, $LibCount, $ExpectedLibObjs, $CoreCount, $ExpectedCoreObjs, $NewestFile, $Elapsed

    Write-Host $StatusLine -NoNewline -ForegroundColor Cyan
    Write-Progress -Activity "Compiling $Name" `
      -Status "$Pct% | Libs: $LibCount/$ExpectedLibObjs | Core: $CoreCount/$ExpectedCoreObjs | $NewestFile" `
      -PercentComplete $Pct
  }

  $Proc.WaitForExit()
  Write-Progress -Activity "Compiling $Name" -Completed
  $Stopwatch.Stop()
  $ElapsedSec = [math]::Round($Stopwatch.Elapsed.TotalSeconds, 2)

  $BuildSucceeded = (Test-Path $BinFile) -and ($Proc.ExitCode -eq 0 -or $null -eq $Proc.ExitCode)

  if (-not $BuildSucceeded) {
    Write-Host ""
    Write-Host "[FAILED] Compiler Error Output:" -ForegroundColor Red
    if (Test-Path $StdErrLog) { Get-Content $StdErrLog | Write-Host -ForegroundColor Red }
    if (Test-Path $StdOutLog) { Get-Content $StdOutLog | Write-Host -ForegroundColor Gray }
    exit 1
  }

  $FinalBar = Draw-Bar 100 22
  Write-Host ("`r$FinalBar 100% | [SUCCESS] $Name compiled in $ElapsedSec seconds!                               ") -ForegroundColor Green

  if (Test-Path $StdOutLog) {
    Get-Content $StdOutLog | Where-Object { $_ -match "Sketch uses|Global variables use" } | ForEach-Object {
      Write-Host ("  " + $_) -ForegroundColor DarkGray
    }
  }

  if ($Upload) {
    $TargetPort = $Port
    if (-not $TargetPort) {
      $Ports = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\((COM\d+)\)' -and ($_.DeviceID -match 'VID_303A' -or $_.Name -match 'USB JTAG|Serial') }
      foreach ($p in $Ports) {
        if ($p.Name -match '\((COM\d+)\)') {
          $TargetPort = $Matches[1]
          break
        }
      }

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
      Write-Host "[UPLOAD FAILED] Make sure Arduino Serial Monitor is closed on $TargetPort." -ForegroundColor Red
      exit $LASTEXITCODE
    }
  }
}

$TestRoot = Split-Path -Parent $PSScriptRoot

switch ($Target) {
  "rx" {
    Build-Target "zig_lora_receiver" (Join-Path $PSScriptRoot "zig_lora_receiver")
  }
  "tx" {
    Build-Target "zig_lora_transmitter" (Join-Path $PSScriptRoot "zig_lora_transmitter")
  }
  "ble" {
    Build-Target "test_ble_iphone" (Join-Path $TestRoot "test_ble_iphone")
  }
  "zig" {
    Build-Target "test_zig_iphone" (Join-Path $TestRoot "test_zig_iphone")
  }
  "both" {
    Build-Target "zig_lora_transmitter" (Join-Path $PSScriptRoot "zig_lora_transmitter")
    Build-Target "zig_lora_receiver" (Join-Path $PSScriptRoot "zig_lora_receiver")
  }
}
