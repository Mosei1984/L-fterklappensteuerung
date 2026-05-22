param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("SessionStart", "UserPromptSubmit", "SubagentStart", "SubagentStop", "PreToolUse", "PermissionRequest", "PostToolUse", "Stop")]
    [string]$EventName
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-HookInput {
    $raw = [Console]::In.ReadToEnd()
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }

    try {
        return $raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        return $null
    }
}

function Write-HookJson {
    param([Parameter(Mandatory = $true)] [object]$Payload)
    $Payload | ConvertTo-Json -Depth 12 -Compress
}

function Get-StringValue {
    param([object]$Value)
    if ($null -eq $Value) {
        return ""
    }

    return [string]$Value
}

function Get-CommandText {
    param([object]$Payload)
    if ($null -eq $Payload -or $null -eq $Payload.tool_input) {
        return ""
    }

    $inputObject = $Payload.tool_input
    $commandProperty = $inputObject.PSObject.Properties["command"]
    if ($null -ne $commandProperty) {
        return Get-StringValue $commandProperty.Value
    }

    $scriptProperty = $inputObject.PSObject.Properties["script"]
    if ($null -ne $scriptProperty) {
        return Get-StringValue $scriptProperty.Value
    }

    return ""
}

function Get-ToolName {
    param([object]$Payload)
    if ($null -eq $Payload) {
        return ""
    }

    $toolProperty = $Payload.PSObject.Properties["tool_name"]
    if ($null -ne $toolProperty) {
        return Get-StringValue $toolProperty.Value
    }

    return ""
}

function Deny-PreToolUse {
    param([string]$Reason)
    Write-HookJson @{
        hookSpecificOutput = @{
            hookEventName = "PreToolUse"
            permissionDecision = "deny"
            permissionDecisionReason = $Reason
        }
    }
}

function Deny-PermissionRequest {
    param([string]$Reason)
    Write-HookJson @{
        hookSpecificOutput = @{
            hookEventName = "PermissionRequest"
            decision = @{
                behavior = "deny"
                message = $Reason
            }
        }
    }
}

function Add-Context {
    param([string]$Event, [string]$Message)
    Write-HookJson @{
        hookSpecificOutput = @{
            hookEventName = $Event
            additionalContext = $Message
        }
    }
}

function Get-HardGateState {
    param([string]$RepoRoot)
    $summaryPath = Join-Path $RepoRoot "artifacts\quality\hard-all-functions\summary.md"
    if (-not (Test-Path -LiteralPath $summaryPath)) {
        return @{
            state = "missing"
            path = $summaryPath
        }
    }

    $summary = Get-Content -LiteralPath $summaryPath -Raw
    if ($summary -match "(?im)\b(fail|failed|error|errors|not ok)\b") {
        return @{
            state = "failed"
            path = $summaryPath
        }
    }

    return @{
        state = "present"
        path = $summaryPath
    }
}

function Test-InRepo {
    param([object]$Payload, [string]$RepoRoot)
    $cwd = ""
    if ($null -ne $Payload) {
        $cwdProperty = $Payload.PSObject.Properties["cwd"]
        if ($null -ne $cwdProperty) {
            $cwd = Get-StringValue $cwdProperty.Value
        }
    }

    if ([string]::IsNullOrWhiteSpace($cwd)) {
        $cwd = (Get-Location).Path
    }

    try {
        $resolvedCwd = (Resolve-Path -LiteralPath $cwd -ErrorAction Stop).Path
    } catch {
        $resolvedCwd = $cwd
    }

    return $resolvedCwd.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

$payload = Read-HookInput
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
if (-not (Test-InRepo -Payload $payload -RepoRoot $repoRoot)) {
    exit 0
}

$repoContext = @"
Repository guardrails for Luefterklappensteuerung:
- Use Airhog-Fpv <116745046+Mosei1984@users.noreply.github.com> for commits.
- Do not add AI-assistant co-author trailers or generated-by metadata.
- Before push or completion claims, run .\tools\run_hard_checks.ps1 and inspect artifacts\quality\hard-all-functions\summary.md.
- Do not use destructive git resets/cleans or recursive deletes in this repo unless the user explicitly requested that exact operation.
"@

$subagentContext = @"
Subagent guardrails for Luefterklappensteuerung:
- Work inside this repository only and read AGENTS.md plus the touched files before giving findings.
- Return exact file paths, line references when available, and verification commands/results.
- Do not claim done/clean/passed unless the relevant command output was inspected in this subagent turn.
- Flag unsafe firmware, Modbus, installer, Windows app, and home-automation integration assumptions explicitly.
- Never suggest AI-assistant commit metadata; commits must use Airhog-Fpv <116745046+Mosei1984@users.noreply.github.com>.
"@

switch ($EventName) {
    "SessionStart" {
        Add-Context -Event "SessionStart" -Message $repoContext
        exit 0
    }

    "UserPromptSubmit" {
        $prompt = ""
        if ($null -ne $payload) {
            $promptProperty = $payload.PSObject.Properties["prompt"]
            if ($null -ne $promptProperty) {
                $prompt = Get-StringValue $promptProperty.Value
            }
        }

        if ($prompt -match "(?i)\b(commit|push|github|fertig|tests?|coverage|release|installer|flash|firmware)\b") {
            Add-Context -Event "UserPromptSubmit" -Message $repoContext
        }
        exit 0
    }

    "SubagentStart" {
        Add-Context -Event "SubagentStart" -Message $subagentContext
        exit 0
    }

    "SubagentStop" {
        $lastMessage = ""
        $stopHookActive = $false
        if ($null -ne $payload) {
            $messageProperty = $payload.PSObject.Properties["last_assistant_message"]
            if ($null -ne $messageProperty) {
                $lastMessage = Get-StringValue $messageProperty.Value
            }

            $activeProperty = $payload.PSObject.Properties["stop_hook_active"]
            if ($null -ne $activeProperty -and $null -ne $activeProperty.Value) {
                $stopHookActive = [bool]$activeProperty.Value
            }
        }

        if (-not $stopHookActive -and $lastMessage -match "(?i)\b(done|fertig|clean|passed|passes|no issues|keine fehler|pushed|committed|getestet)\b" -and
            $lastMessage -notmatch "(?i)\b(dotnet test|platformio|run_hard_checks|markdownlint|clangtidy|cppcheck|coverage|TRX|Cobertura|nicht ausgeführt|not run|unable to run)\b") {
            Write-HookJson @{
                decision = "block"
                reason = "Subagent result needs one more pass: include exact verification evidence or explicitly state which checks were not run."
            }
            exit 0
        }

        Write-HookJson @{ continue = $true }
        exit 0
    }

    { $_ -in @("PreToolUse", "PermissionRequest") } {
        $command = Get-CommandText -Payload $payload
        $toolName = Get-ToolName -Payload $payload
        if ([string]::IsNullOrWhiteSpace($command) -and $toolName -notmatch "(?i)apply_patch|edit|write") {
            exit 0
        }

        $denyReason = $null
        if ($command -match "(?i)\bgit\s+reset\s+--hard\b") {
            $denyReason = "Blocked: git reset --hard would discard repository state. Ask the user for an explicit destructive-reset request first."
        } elseif ($command -match "(?i)\bgit\s+clean\s+-[A-Za-z]*[fdx][A-Za-z]*\b") {
            $denyReason = "Blocked: git clean with force/delete flags is destructive in this firmware repo."
        } elseif ($command -match "(?i)\bgit\s+push\b.*\s--force(?:-with-lease)?\b") {
            $denyReason = "Blocked: force-push is disabled by repo hook policy."
        } elseif ($command -match "(?i)\bgit\s+(commit|config)\b" -and $command -match "(?i)\b(codex|co-authored-by|generated\s+by)\b") {
            $denyReason = "Blocked: commits/config must not contain AI-assistant, co-author, or generated-by metadata."
        } elseif ($command -match "(?i)\bRemove-Item\b.*\b-Recurse\b.*(\.git|\\src\\|\\lib\\|\\tools\\|\\docs\\|\\test\\|README)") {
            $denyReason = "Blocked: recursive delete targets important repository paths."
        }

        if ($null -ne $denyReason) {
            if ($EventName -eq "PermissionRequest") {
                Deny-PermissionRequest -Reason $denyReason
            } else {
                Deny-PreToolUse -Reason $denyReason
            }
            exit 0
        }

        if ($EventName -eq "PreToolUse" -and $command -match "(?i)\bgit\s+push\b") {
            $gate = Get-HardGateState -RepoRoot $repoRoot
            if ($gate.state -ne "present") {
                Deny-PreToolUse -Reason "Blocked: run .\tools\run_hard_checks.ps1 successfully before pushing. Current hard-gate state: $($gate.state)."
                exit 0
            }
        }

        if ($EventName -eq "PreToolUse" -and $command -match "(?i)\bgit\s+commit\b") {
            Add-Context -Event "PreToolUse" -Message "Before committing this repo, confirm hard checks are current and use author Airhog-Fpv <116745046+Mosei1984@users.noreply.github.com> with no AI-assistant metadata."
            exit 0
        }

        exit 0
    }

    "PostToolUse" {
        exit 0
    }

    "Stop" {
        $lastMessage = ""
        if ($null -ne $payload) {
            $messageProperty = $payload.PSObject.Properties["last_assistant_message"]
            if ($null -ne $messageProperty) {
                $lastMessage = Get-StringValue $messageProperty.Value
            }
        }

        if ($lastMessage -match "(?i)\b(fertig|erledigt|done|passed|passes|clean|pushed|committed|getestet|installiert)\b") {
            $gate = Get-HardGateState -RepoRoot $repoRoot
            if ($gate.state -ne "present") {
                Write-HookJson @{
                    decision = "block"
                    reason = "Do not finish yet: .\tools\run_hard_checks.ps1 is not recorded as clean for this repo. Run/fix the hard gate or explicitly report the remaining failure."
                }
                exit 0
            }
        }

        Write-HookJson @{ continue = $true }
        exit 0
    }
}
