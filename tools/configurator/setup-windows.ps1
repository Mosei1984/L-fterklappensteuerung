param(
  [string] $SourceDirectory = $PSScriptRoot,
  [string] $InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\LuefterConfigurator'),
  [switch] $ValidateOnly
)

$ErrorActionPreference = 'Stop'

$sourceRoot = (Resolve-Path $SourceDirectory).Path
$installScript = Join-Path $sourceRoot 'install-windows.ps1'
$exePath = Join-Path $sourceRoot 'LuefterConfigurator.Host.exe'
$eulaPath = Join-Path $sourceRoot 'EULA.md'

if ($ValidateOnly) {
  foreach ($required in @($installScript, $eulaPath)) {
    if (-not (Test-Path $required)) {
      throw "Setup-Datei fehlt: $required"
    }
  }

  Write-Host "Setup-Wizard OK: $sourceRoot"
  exit 0
}

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq 'Core') {
  throw 'Der grafische Setup-Wizard ist nur unter Windows verfuegbar.'
}

foreach ($required in @($installScript, $exePath, $eulaPath)) {
  if (-not (Test-Path $required)) {
    throw "Setup-Datei fehlt: $required"
  }
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Luefterklappen Konfigurator Setup'
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.ClientSize = New-Object System.Drawing.Size(720, 520)

$title = New-Object System.Windows.Forms.Label
$title.Font = New-Object System.Drawing.Font('Segoe UI', 18, [System.Drawing.FontStyle]::Bold)
$title.Location = New-Object System.Drawing.Point(28, 22)
$title.Size = New-Object System.Drawing.Size(660, 38)
$form.Controls.Add($title)

$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$subtitle.ForeColor = [System.Drawing.Color]::FromArgb(80, 88, 104)
$subtitle.Location = New-Object System.Drawing.Point(30, 62)
$subtitle.Size = New-Object System.Drawing.Size(650, 32)
$form.Controls.Add($subtitle)

$panel = New-Object System.Windows.Forms.Panel
$panel.Location = New-Object System.Drawing.Point(30, 108)
$panel.Size = New-Object System.Drawing.Size(660, 330)
$form.Controls.Add($panel)

$backButton = New-Object System.Windows.Forms.Button
$backButton.Text = 'Zurueck'
$backButton.Location = New-Object System.Drawing.Point(402, 462)
$backButton.Size = New-Object System.Drawing.Size(88, 32)
$form.Controls.Add($backButton)

$nextButton = New-Object System.Windows.Forms.Button
$nextButton.Text = 'Weiter'
$nextButton.Location = New-Object System.Drawing.Point(500, 462)
$nextButton.Size = New-Object System.Drawing.Size(88, 32)
$form.Controls.Add($nextButton)

$cancelButton = New-Object System.Windows.Forms.Button
$cancelButton.Text = 'Abbrechen'
$cancelButton.Location = New-Object System.Drawing.Point(598, 462)
$cancelButton.Size = New-Object System.Drawing.Size(92, 32)
$form.Controls.Add($cancelButton)

$installPathText = New-Object System.Windows.Forms.TextBox
$installPathText.Text = [System.IO.Path]::GetFullPath($InstallDirectory)
$installPathText.Width = 500

$desktopShortcutCheck = New-Object System.Windows.Forms.CheckBox
$desktopShortcutCheck.Text = 'Desktop-Verknuepfung erstellen'
$desktopShortcutCheck.Checked = $true
$desktopShortcutCheck.AutoSize = $true

$launchAfterInstallCheck = New-Object System.Windows.Forms.CheckBox
$launchAfterInstallCheck.Text = 'Konfigurator nach der Installation starten'
$launchAfterInstallCheck.Checked = $true
$launchAfterInstallCheck.AutoSize = $true

$acceptEulaCheck = New-Object System.Windows.Forms.CheckBox
$acceptEulaCheck.Text = 'Ich akzeptiere die Endbenutzer-Lizenzvereinbarung'
$acceptEulaCheck.AutoSize = $true

$step = 0
$installedExe = $null

function Add-Paragraph {
  param([string] $Text, [int] $Y)

  $label = New-Object System.Windows.Forms.Label
  $label.Text = $Text
  $label.Font = New-Object System.Drawing.Font('Segoe UI', 10)
  $label.Location = New-Object System.Drawing.Point(0, $Y)
  $label.Size = New-Object System.Drawing.Size(640, 64)
  $panel.Controls.Add($label)
}

function Render-Step {
  $panel.Controls.Clear()
  $backButton.Enabled = $step -gt 0 -and $step -lt 4
  $nextButton.Enabled = $true
  $cancelButton.Enabled = $step -lt 4

  switch ($step) {
    0 {
      $title.Text = 'Willkommen'
      $subtitle.Text = 'Dieser Assistent installiert den lokalen Service und die Browser-UI.'
      Add-Paragraph 'Der Luefterklappen Konfigurator wird lokal installiert, im Startmenue eingetragen und unter Apps & Features deinstallierbar gemacht.' 4
      Add-Paragraph 'Der Konfigurator laeuft nach dem Start lokal auf http://127.0.0.1:5184. Es wird keine Cloud-Verbindung eingerichtet.' 78
      $nextButton.Text = 'Weiter'
    }
    1 {
      $title.Text = 'Lizenzvereinbarung'
      $subtitle.Text = 'Bitte lesen und bestaetigen.'

      $eulaBox = New-Object System.Windows.Forms.TextBox
      $eulaBox.Multiline = $true
      $eulaBox.ReadOnly = $true
      $eulaBox.ScrollBars = 'Vertical'
      $eulaBox.Font = New-Object System.Drawing.Font('Consolas', 9)
      $eulaBox.Location = New-Object System.Drawing.Point(0, 0)
      $eulaBox.Size = New-Object System.Drawing.Size(660, 255)
      $eulaBox.Text = Get-Content -Path $eulaPath -Raw
      $panel.Controls.Add($eulaBox)

      $acceptEulaCheck.Location = New-Object System.Drawing.Point(0, 270)
      $panel.Controls.Add($acceptEulaCheck)
      $nextButton.Text = 'Weiter'
      $nextButton.Enabled = $acceptEulaCheck.Checked
      $acceptEulaCheck.Add_CheckedChanged({ $nextButton.Enabled = $acceptEulaCheck.Checked })
    }
    2 {
      $title.Text = 'Installationsoptionen'
      $subtitle.Text = 'Zielordner und Verknuepfungen festlegen.'

      $pathLabel = New-Object System.Windows.Forms.Label
      $pathLabel.Text = 'Installationsordner'
      $pathLabel.Location = New-Object System.Drawing.Point(0, 4)
      $pathLabel.Size = New-Object System.Drawing.Size(640, 24)
      $panel.Controls.Add($pathLabel)

      $installPathText.Location = New-Object System.Drawing.Point(0, 32)
      $panel.Controls.Add($installPathText)

      $browseButton = New-Object System.Windows.Forms.Button
      $browseButton.Text = 'Durchsuchen...'
      $browseButton.Location = New-Object System.Drawing.Point(510, 30)
      $browseButton.Size = New-Object System.Drawing.Size(120, 28)
      $browseButton.Add_Click({
        $browser = New-Object System.Windows.Forms.FolderBrowserDialog
        $browser.Description = 'Installationsordner auswaehlen'
        $browser.SelectedPath = $installPathText.Text
        if ($browser.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
          $installPathText.Text = $browser.SelectedPath
        }
      })
      $panel.Controls.Add($browseButton)

      $desktopShortcutCheck.Location = New-Object System.Drawing.Point(0, 82)
      $panel.Controls.Add($desktopShortcutCheck)
      $launchAfterInstallCheck.Location = New-Object System.Drawing.Point(0, 112)
      $panel.Controls.Add($launchAfterInstallCheck)
      $nextButton.Text = 'Weiter'
    }
    3 {
      $title.Text = 'Bereit zur Installation'
      $subtitle.Text = 'Bitte Angaben pruefen und dann Installieren.'
      Add-Paragraph "Installationsordner:`r`n$($installPathText.Text)" 0
      Add-Paragraph "Desktop-Verknuepfung: $($desktopShortcutCheck.Checked)`r`nStart nach Installation: $($launchAfterInstallCheck.Checked)" 90
      $nextButton.Text = 'Installieren'
    }
    4 {
      $title.Text = 'Installation abgeschlossen'
      $subtitle.Text = 'Der Konfigurator ist installiert.'
      Add-Paragraph "Installiert nach:`r`n$installedExe" 0
      Add-Paragraph 'Startmenue: Luefterklappen Konfigurator. Deinstallation: Apps & Features oder Startmenue-Eintrag.' 86
      $backButton.Enabled = $false
      $nextButton.Text = 'Fertig'
      $cancelButton.Enabled = $false
    }
  }
}

$backButton.Add_Click({
  if ($step -gt 0) {
    $script:step--
    Render-Step
  }
})

$nextButton.Add_Click({
  if ($step -eq 3) {
    try {
      $installArguments = @{
        SourceDirectory = $sourceRoot
        InstallDirectory = $installPathText.Text
        AcceptEula = $true
      }
      if ($desktopShortcutCheck.Checked) {
        $installArguments.CreateDesktopShortcut = $true
      }

      & $installScript @installArguments

      $script:installedExe = Join-Path ([System.IO.Path]::GetFullPath($installPathText.Text)) 'LuefterConfigurator.Host.exe'
      if ($launchAfterInstallCheck.Checked -and (Test-Path $script:installedExe)) {
        Start-Process -FilePath $script:installedExe -WorkingDirectory (Split-Path $script:installedExe -Parent) | Out-Null
      }

      $script:step = 4
      Render-Step
    } catch {
      [System.Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'Setup-Fehler', 'OK', 'Error') | Out-Null
    }
    return
  }

  if ($step -eq 4) {
    $form.Close()
    return
  }

  $script:step++
  Render-Step
})

$cancelButton.Add_Click({
  $result = [System.Windows.Forms.MessageBox]::Show($form, 'Setup wirklich abbrechen?', 'Setup abbrechen', 'YesNo', 'Question')
  if ($result -eq [System.Windows.Forms.DialogResult]::Yes) {
    $form.Close()
  }
})

Render-Step
[void]$form.ShowDialog()
