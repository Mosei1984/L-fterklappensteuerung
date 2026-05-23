param(
  [string]$FirmwareUf2 = ".\.pio\build\pico\firmware.uf2",
  [string]$Port = "",
  [int]$TimeoutSeconds = 10
)

$ErrorActionPreference = "Stop"

function Get-Rp2Drive {
  Get-PSDrive -PSProvider FileSystem | ForEach-Object {
    $infoPath = Join-Path $_.Root "INFO_UF2.TXT"
    if (Test-Path $infoPath) {
      $info = Get-Content -Path $infoPath -ErrorAction SilentlyContinue
      if ($info -match "RPI-RP2|RP2") {
        return $_.Root
      }
    }
  }
}

function Wait-Rp2Drive {
  param([int]$Seconds)

  $deadline = (Get-Date).AddSeconds($Seconds)
  do {
    $drive = Get-Rp2Drive
    if ($drive) {
      return $drive
    }
    Start-Sleep -Milliseconds 250
  } while ((Get-Date) -lt $deadline)

  return $null
}

function Get-UploadPort {
  if ($Port) {
    return $Port
  }

  $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
  if ($ports.Count -eq 0) {
    return $null
  }

  return $ports[-1]
}

function Send-BootselCommand {
  param([string]$PortName)

  $serial = $null
  try {
    $serial = [System.IO.Ports.SerialPort]::new(
      $PortName,
      115200,
      [System.IO.Ports.Parity]::None,
      8,
      [System.IO.Ports.StopBits]::One)
    $serial.WriteTimeout = 1000
    $serial.ReadTimeout = 500
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    $serial.Open()
    Start-Sleep -Milliseconds 250
    $serial.WriteLine("BOOTSEL")
    Start-Sleep -Milliseconds 500
  } catch {
    Write-Host "BOOTSEL service command failed on ${PortName}: $($_.Exception.Message)"
  } finally {
    if ($serial -and $serial.IsOpen) {
      $serial.Close()
    }
  }
}

function Touch-1200Baud {
  param([string]$PortName)

  $serial = $null
  try {
    $serial = [System.IO.Ports.SerialPort]::new(
      $PortName,
      1200,
      [System.IO.Ports.Parity]::None,
      8,
      [System.IO.Ports.StopBits]::One)
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    Start-Sleep -Milliseconds 250
  } catch {
    Write-Host "1200-baud touch failed on ${PortName}: $($_.Exception.Message)"
  } finally {
    if ($serial -and $serial.IsOpen) {
      $serial.Close()
    }
  }
}

$resolvedUf2 = Resolve-Path -Path $FirmwareUf2
$driveRoot = Wait-Rp2Drive -Seconds 1

if (-not $driveRoot) {
  $uploadPort = Get-UploadPort
  if (-not $uploadPort) {
    throw "No Pico COM port found and no RPI-RP2 drive is mounted."
  }

  Write-Host "Requesting BOOTSEL via service command on $uploadPort..."
  Send-BootselCommand -PortName $uploadPort
  $driveRoot = Wait-Rp2Drive -Seconds $TimeoutSeconds

  if (-not $driveRoot) {
    Write-Host "Service command did not expose RPI-RP2; trying 1200-baud touch..."
    Touch-1200Baud -PortName $uploadPort
    $driveRoot = Wait-Rp2Drive -Seconds $TimeoutSeconds
  }
}

if (-not $driveRoot) {
  throw "RPI-RP2 drive was not detected. Put the Pico in BOOTSEL once, then rerun this script."
}

Write-Host "Copying $resolvedUf2 to $driveRoot..."
Copy-Item -LiteralPath $resolvedUf2 -Destination (Join-Path $driveRoot "firmware.uf2") -Force
Start-Sleep -Seconds 3

$portsAfterFlash = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
if ($portsAfterFlash.Count -gt 0) {
  Write-Host "Pico serial ports after flash: $($portsAfterFlash -join ', ')"
} else {
  Write-Host "Flash copied; no serial port detected yet."
}
