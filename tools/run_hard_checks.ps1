param(
  [switch] $SkipFirmwareGate,
  [switch] $SkipMarkdownLint,
  [switch] $SkipVsCodeLogCopy
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$artifactRoot = Join-Path $repoRoot 'artifacts\quality\hard-all-functions'
$testResults = Join-Path $artifactRoot 'test-results'
$logs = Join-Path $artifactRoot 'logs'
$coverageSummary = Join-Path $artifactRoot 'coverage-summary.md'
$summary = Join-Path $artifactRoot 'summary.md'
$settingsSnapshot = Join-Path $artifactRoot 'vscode-settings-snapshot.json'

$repoRootPath = [System.IO.Path]::GetFullPath($repoRoot)
$artifactRootPath = [System.IO.Path]::GetFullPath($artifactRoot)
if (-not $artifactRootPath.StartsWith($repoRootPath + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing to clean unsafe artifact directory outside repository: $artifactRootPath"
}

if (Test-Path $artifactRootPath) {
  Remove-Item -LiteralPath $artifactRootPath -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $testResults, $logs | Out-Null

function Invoke-HardStep {
  param(
    [string] $Name,
    [scriptblock] $Command
  )

  Write-Host ""
  Write-Host "== $Name =="
  & $Command
}

function Invoke-LoggedCommand {
  param(
    [string] $Name,
    [string] $LogFile,
    [scriptblock] $Command
  )

  $logPath = Join-Path $logs $LogFile
  Write-Host "-- $Name"
  & $Command *>&1 | Tee-Object -FilePath $logPath
  if ($LASTEXITCODE -ne 0) {
    throw "$Name failed with exit code $LASTEXITCODE. See $logPath"
  }
}

function Write-CoverageSummary {
  param([string] $CoverageFile)

  [xml] $coverage = Get-Content -LiteralPath $CoverageFile -Raw
  $lineRate = [double]::Parse($coverage.coverage.'line-rate', [System.Globalization.CultureInfo]::InvariantCulture)
  $branchRate = [double]::Parse($coverage.coverage.'branch-rate', [System.Globalization.CultureInfo]::InvariantCulture)
  $linePercent = [Math]::Round($lineRate * 100, 2)
  $branchPercent = [Math]::Round($branchRate * 100, 2)
  $linesValid = [int] $coverage.coverage.'lines-valid'
  $linesCovered = [int] $coverage.coverage.'lines-covered'
  $branchesValid = [int] $coverage.coverage.'branches-valid'
  $branchesCovered = [int] $coverage.coverage.'branches-covered'

  $content = New-Object System.Collections.Generic.List[string]
  $content.Add('# Configurator Coverage Summary')
  $content.Add('')
  $content.Add("| Metric | Covered | Valid | Percent |")
  $content.Add("| --- | ---: | ---: | ---: |")
  $content.Add("| Lines | $linesCovered | $linesValid | $linePercent% |")
  $content.Add("| Branches | $branchesCovered | $branchesValid | $branchPercent% |")
  $content.Add('')
  $content.Add("Cobertura file: ``$CoverageFile``")
  $content.Add('')
  $content.Add('## Packages')
  $content.Add('')
  $content.Add("| Package | Line % | Branch % |")
  $content.Add("| --- | ---: | ---: |")

  foreach ($package in $coverage.coverage.packages.package) {
    $packageLineRate = [double]::Parse($package.'line-rate', [System.Globalization.CultureInfo]::InvariantCulture)
    $packageBranchRate = [double]::Parse($package.'branch-rate', [System.Globalization.CultureInfo]::InvariantCulture)
    $packageLinePercent = [Math]::Round($packageLineRate * 100, 2)
    $packageBranchPercent = [Math]::Round($packageBranchRate * 100, 2)
    $content.Add("| $($package.name) | $packageLinePercent% | $packageBranchPercent% |")
  }

  Set-Content -Path $coverageSummary -Value $content -Encoding UTF8
  return @{
    LinePercent = $linePercent
    BranchPercent = $branchPercent
    LinesCovered = $linesCovered
    LinesValid = $linesValid
    BranchesCovered = $branchesCovered
    BranchesValid = $branchesValid
  }
}

function Invoke-AgentHookSmoke {
  param(
    [Parameter(Mandatory = $true)]
    [string] $EventName,
    [Parameter(Mandatory = $true)]
    [hashtable] $Payload
  )

  $hookPath = Join-Path $repoRoot 'tools\agent-hooks\luefter_repo_hook.ps1'
  $payloadJson = $Payload | ConvertTo-Json -Depth 12 -Compress
  $output = $payloadJson | powershell -NoProfile -ExecutionPolicy Bypass -File $hookPath -EventName $EventName
  if ($LASTEXITCODE -ne 0) {
    throw "Agent hook $EventName failed with exit code $LASTEXITCODE"
  }

  Add-Content -LiteralPath (Join-Path $logs 'codex-hook-smoke.log') -Value "[$EventName] $output"
  return $output
}

Push-Location $repoRoot
try {
  $pythonScriptCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\Scripts'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python311\Scripts'),
    'C:\Python312\Scripts',
    'C:\Python311\Scripts'
  )
  $toolPathPrefix = (@(
    'C:\msys64\mingw64\bin',
    'C:\msys64\usr\bin',
    'C:\Program Files\Git\cmd'
  ) + $pythonScriptCandidates) |
    Where-Object { Test-Path $_ } |
    Select-Object -Unique
  $env:PATH = "$(($toolPathPrefix -join ';'));$env:PATH"

  $dotnet = Join-Path $env:USERPROFILE '.dotnet8\dotnet.exe'
  if (-not (Test-Path $dotnet)) {
    $dotnetCommand = Get-Command dotnet -ErrorAction Stop
    $dotnet = $dotnetCommand.Source
  }

  $env:DOTNET_ROOT = Split-Path $dotnet -Parent
  New-Item -ItemType Directory -Force -Path .\.dotnet-cli-home, .\.nuget | Out-Null
  $env:DOTNET_CLI_HOME = (Resolve-Path .\.dotnet-cli-home).Path
  $env:NUGET_PACKAGES = (Resolve-Path .\.nuget).Path

  if (Test-Path .\.vscode\settings.json) {
    Copy-Item -LiteralPath .\.vscode\settings.json -Destination $settingsSnapshot -Force
  }

  Invoke-HardStep 'Environment inventory' {
    Invoke-LoggedCommand 'dotnet --info' 'environment-dotnet.log' { & $dotnet --info }
    Invoke-LoggedCommand 'npm --version' 'environment-npm.log' { npm --version }
    Invoke-LoggedCommand 'workspace VS Code settings' 'vscode-settings.log' { Get-Content .\.vscode\settings.json }
  }

  Invoke-HardStep 'Workspace trace and Test Explorer settings validation' {
    $settings = Get-Content .\.vscode\settings.json -Raw | ConvertFrom-Json
    if ($settings.'dotnet.defaultSolution' -ne 'tools/configurator/LuefterConfigurator.sln') {
      throw 'dotnet.defaultSolution does not point to the configurator solution.'
    }
    if ($settings.'dotnet.server.trace' -ne 'Trace') {
      throw 'dotnet.server.trace is not Trace.'
    }
    if ($settings.'razor.trace' -ne 'Verbose') {
      throw 'razor.trace is not Verbose.'
    }
    if (-not $settings.'dotnet.testWindow.enableTestExplorerDiff') {
      throw 'Test Explorer diff support is not enabled.'
    }
    Write-Host 'VS Code trace/Test Explorer settings validated.'
  }

  Invoke-HardStep 'Agent hook smoke tests' {
    $cwd = (Resolve-Path .).Path

    $subagentStart = Invoke-AgentHookSmoke -EventName 'SubagentStart' -Payload @{
      cwd = $cwd
      hook_event_name = 'SubagentStart'
      agent_id = 'smoke-subagent'
      agent_type = 'reviewer'
      permission_mode = 'default'
    } | ConvertFrom-Json
    if ($subagentStart.hookSpecificOutput.hookEventName -ne 'SubagentStart' -or
        $subagentStart.hookSpecificOutput.additionalContext -notmatch 'Subagent guardrails') {
      throw 'SubagentStart hook did not inject subagent guardrails.'
    }

    $subagentStopBlock = Invoke-AgentHookSmoke -EventName 'SubagentStop' -Payload @{
      cwd = $cwd
      hook_event_name = 'SubagentStop'
      agent_id = 'smoke-subagent'
      agent_type = 'reviewer'
      stop_hook_active = $false
      last_assistant_message = 'done clean passed'
    } | ConvertFrom-Json
    if ($subagentStopBlock.decision -ne 'block') {
      throw 'SubagentStop hook did not block unverified completion language.'
    }

    $subagentStopContinue = Invoke-AgentHookSmoke -EventName 'SubagentStop' -Payload @{
      cwd = $cwd
      hook_event_name = 'SubagentStop'
      agent_id = 'smoke-subagent'
      agent_type = 'reviewer'
      stop_hook_active = $false
      last_assistant_message = 'dotnet test passed: 61/61'
    } | ConvertFrom-Json
    if (-not $subagentStopContinue.continue) {
      throw 'SubagentStop hook did not continue when verification evidence is present.'
    }

    $preToolBlock = Invoke-AgentHookSmoke -EventName 'PreToolUse' -Payload @{
      cwd = $cwd
      hook_event_name = 'PreToolUse'
      tool_name = 'Bash'
      tool_input = @{
        command = 'git reset --hard HEAD'
      }
    } | ConvertFrom-Json
    if ($preToolBlock.hookSpecificOutput.permissionDecision -ne 'deny') {
      throw 'PreToolUse hook did not block destructive git reset.'
    }

    $outsideRepo = Invoke-AgentHookSmoke -EventName 'SubagentStart' -Payload @{
      cwd = 'C:\Temp'
      hook_event_name = 'SubagentStart'
      agent_id = 'outside-subagent'
      agent_type = 'reviewer'
      permission_mode = 'default'
    }
    if (-not [string]::IsNullOrWhiteSpace($outsideRepo)) {
      throw 'Repo hook produced output outside the Luefterklappensteuerung workspace.'
    }

    Write-Host 'Repo and subagent hooks validated.'
  }

  Invoke-HardStep 'Configurator restore and build logs' {
    Invoke-LoggedCommand 'dotnet restore configurator solution' 'dotnet-restore.log' {
      & $dotnet restore .\tools\configurator\LuefterConfigurator.sln --ignore-failed-sources -p:NuGetAudit=false
    }
    Invoke-LoggedCommand 'dotnet build configurator solution' 'dotnet-build.log' {
      & $dotnet build .\tools\configurator\LuefterConfigurator.sln --no-restore -bl:(Join-Path $logs 'configurator-build.binlog') -flp:"logfile=$(Join-Path $logs 'configurator-build-msbuild.log');verbosity=normal"
    }
  }

  Invoke-HardStep 'Razor compile log' {
    Invoke-LoggedCommand 'dotnet build Razor host with RazorCompileOnBuild' 'razor-build.log' {
      & $dotnet build .\tools\configurator\src\LuefterConfigurator.Host\LuefterConfigurator.Host.csproj --no-restore -p:RazorCompileOnBuild=true -bl:(Join-Path $logs 'razor-build.binlog') -flp:"logfile=$(Join-Path $logs 'razor-build-msbuild.log');verbosity=normal"
    }
  }

  Invoke-HardStep 'Configurator tests with TRX and code coverage' {
    Invoke-LoggedCommand 'dotnet test configurator with coverage' 'dotnet-test-coverage.log' {
      & $dotnet test .\tools\configurator\LuefterConfigurator.sln --no-restore --settings .\tools\configurator\coverlet.runsettings --logger "trx;LogFileName=configurator-tests.trx" --collect:"XPlat Code Coverage" --results-directory $testResults
    }

    $coverageFile = Get-ChildItem -LiteralPath $testResults -Recurse -Filter coverage.cobertura.xml |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 1
    if ($null -eq $coverageFile) {
      throw "Coverage file was not created under $testResults"
    }

    $coverage = Write-CoverageSummary -CoverageFile $coverageFile.FullName
    Write-Host "Line coverage: $($coverage.LinePercent)% ($($coverage.LinesCovered)/$($coverage.LinesValid))"
    Write-Host "Branch coverage: $($coverage.BranchPercent)% ($($coverage.BranchesCovered)/$($coverage.BranchesValid))"
  }

  if (-not $SkipMarkdownLint) {
    Invoke-HardStep 'Markdown lint' {
      Invoke-LoggedCommand 'npm run markdownlint' 'markdownlint.log' {
        npm run markdownlint
      }
    }
  }

  if (-not $SkipVsCodeLogCopy) {
    Invoke-HardStep 'VS Code C# Dev Kit log snapshot' {
      $codeLogRoot = Join-Path $env:APPDATA 'Code\logs'
      $target = Join-Path $logs 'vscode-csharp'
      New-Item -ItemType Directory -Force -Path $target | Out-Null
      if (Test-Path $codeLogRoot) {
        $matchingLogs = Get-ChildItem -LiteralPath $codeLogRoot -Recurse -File -ErrorAction SilentlyContinue |
          Where-Object {
            $_.FullName -match 'ms-dotnettools\.csdevkit|ms-dotnettools\.csharp|razor|omnisharp|ServiceHub' -or
            $_.Name -match 'C#|csharp|razor|dotnet|Dev Kit|omnisharp|lsp'
          } |
          Sort-Object LastWriteTime -Descending |
          Select-Object -First 80

        $copiedLogs = 0
        $skippedLogs = 0
        foreach ($file in $matchingLogs) {
          $safeName = ($file.FullName.Substring($codeLogRoot.Length).TrimStart('\') -replace '[\\/:*?"<>|]', '_')
          $destination = Join-Path $target $safeName
          try {
            if (Test-Path -LiteralPath $file.FullName) {
              Copy-Item -LiteralPath $file.FullName -Destination $destination -Force -ErrorAction Stop
              $copiedLogs++
            } else {
              $skippedLogs++
            }
          } catch {
            $skippedLogs++
            Add-Content -LiteralPath (Join-Path $logs 'vscode-log-copy-skipped.log') -Value "$($file.FullName): $($_.Exception.Message)"
          }
        }

        Write-Host "Copied $copiedLogs VS Code C#/Razor/LSP log files to $target; skipped $skippedLogs volatile files."
      } else {
        Write-Host "VS Code log root not found: $codeLogRoot"
      }
    }
  }

  if (-not $SkipFirmwareGate) {
    Invoke-HardStep 'Full firmware/configurator quality gate' {
      Invoke-LoggedCommand 'tools/run_quality_checks.ps1' 'run-quality-checks.log' {
        $env:PATH = "$(($toolPathPrefix -join ';'));$env:PATH"
        $env:PLATFORMIO_CORE_DIR = 'C:\pio-luefter'
        powershell -ExecutionPolicy Bypass -File .\tools\run_quality_checks.ps1
      }
    }
  }

  $summaryContent = @(
    '# Hard All-Functions Check Summary',
    '',
    "- Artifact directory: ``$artifactRoot``",
    "- TRX results: ``$testResults``",
    "- Coverage summary: ``$coverageSummary``",
    "- Build/Razor/Markdown/LSP logs: ``$logs``",
    '',
    'Fresh gates executed:',
    '',
    '- Environment inventory',
    '- VS Code trace/Test Explorer settings validation',
    '- Repo/subagent hook smoke tests',
    '- Configurator restore/build with MSBuild binlog',
    '- Razor host build with RazorCompileOnBuild and binlog',
    '- Configurator xUnit tests with TRX and Cobertura coverage',
    $(if (-not $SkipMarkdownLint) { '- Markdown lint' } else { '- Markdown lint skipped by parameter' }),
    $(if (-not $SkipVsCodeLogCopy) { '- VS Code C# Dev Kit/Razor/LSP log snapshot' } else { '- VS Code log copy skipped by parameter' }),
    $(if (-not $SkipFirmwareGate) { '- Full firmware/configurator quality gate' } else { '- Full firmware/configurator quality gate skipped by parameter' })
  )
  Set-Content -Path $summary -Value $summaryContent -Encoding UTF8
  Write-Host ""
  Write-Host "Hard check summary: $summary"
} finally {
  Pop-Location
}
