$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$handlerPath = Join-Path $repoRoot "main/story/interactive_story_handler.cc"
$controllerPath = Join-Path $repoRoot "main/story/story_controller.cc"

function Read-Source([string]$path) {
    return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
}

function Get-BraceBlock([string]$source, [string]$marker) {
    $markerIndex = $source.IndexOf($marker, [System.StringComparison]::Ordinal)
    if ($markerIndex -lt 0) {
        throw "Marker not found: $marker"
    }

    $openIndex = $source.IndexOf("{", $markerIndex, [System.StringComparison]::Ordinal)
    if ($openIndex -lt 0) {
        throw "Opening brace not found after marker: $marker"
    }

    $depth = 0
    for ($index = $openIndex; $index -lt $source.Length; ++$index) {
        if ($source[$index] -eq "{") {
            ++$depth
        } elseif ($source[$index] -eq "}") {
            --$depth
            if ($depth -eq 0) {
                return $source.Substring($openIndex, $index - $openIndex + 1)
            }
        }
    }

    throw "Closing brace not found for marker: $marker"
}

function Assert-Contains([string]$text, [string]$expected, [string]$context) {
    if (-not $text.Contains($expected)) {
        throw "$context is missing: $expected"
    }
}

function Assert-NotContains([string]$text, [string]$unexpected, [string]$context) {
    if ($text.Contains($unexpected)) {
        throw "$context must not contain: $unexpected"
    }
}

$handlerSource = Read-Source $handlerPath
$controllerSource = Read-Source $controllerPath
$documentPath = Get-ChildItem -Path (Join-Path $repoRoot "docs") -File -Filter "*.md" |
    Where-Object {
        $source = Read-Source $_.FullName
        $source.Contains("interactive_story") -and $source.Contains("request_rejected")
    } |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $documentPath) {
    throw "Interactive story protocol document not found"
}
$documentSource = Read-Source $documentPath

$errorBranch = Get-BraceBlock $handlerSource `
    'else if (strcmp(action->valuestring, "error") == 0)'
Assert-Contains $errorBranch "HandleError(" "interactive_story error branch"
Assert-NotContains $errorBranch "HandleRequestRejected(" "interactive_story error branch"

$rejectedBranch = Get-BraceBlock $handlerSource `
    'else if (strcmp(action->valuestring, "request_rejected") == 0)'
Assert-Contains $rejectedBranch "HandleRequestRejected(" "request_rejected branch"

$errorHandler = Get-BraceBlock $handlerSource `
    "void InteractiveStoryHandler::HandleError("
Assert-Contains $errorHandler "StoryController::GetInstance().HandleError(" `
    "InteractiveStoryHandler::HandleError"

$interrupted = Get-BraceBlock $controllerSource `
    "void StoryController::HandleInteractionInterrupted()"
Assert-Contains $interrupted "interaction_request_id_.clear()" "HandleInteractionInterrupted"
Assert-Contains $interrupted "ready_requests_.clear()" "HandleInteractionInterrupted"

$errorHandler = Get-BraceBlock $controllerSource "void StoryController::HandleError("
foreach ($expected in @(
        "StopListening()",
        "AbortSpeaking(",
        "HandleInteractionInterrupted()",
        "ResetStoryRuntimeState()",
        "story_history_.clear()",
        "continue_available_ = false",
        "continue_session_id_.clear()",
        "continue_title_.clear()",
        "SetDeviceState(kDeviceStateIdle)",
        "ShowHome()")) {
    Assert-Contains $errorHandler $expected "StoryController::HandleError"
}

$runtimeReset = Get-BraceBlock $controllerSource `
    "void StoryController::ResetStoryRuntimeState()"
foreach ($expected in @(
        "choice_pending_ = false",
        "current_session_id_.clear()",
        "active_story_session_id_.clear()",
        "current_node_id_.clear()",
        "confirmation_session_id_.clear()",
        "confirmation_id_.clear()",
        "pending_enter_request_id_.clear()",
        "pending_history_request_id_.clear()",
        "pending_resume_request_id_.clear()")) {
    Assert-Contains $runtimeReset $expected "ResetStoryRuntimeState"
}

$rejectedHandler = Get-BraceBlock $controllerSource `
    "void StoryController::HandleRequestRejected("
Assert-Contains $rejectedHandler "RestoreInteractionButtons(" `
    "StoryController::HandleRequestRejected"
Assert-NotContains $rejectedHandler "HandleError(" `
    "StoryController::HandleRequestRejected"

Assert-Contains $documentSource "request_rejected" "interactive story protocol document"
Assert-Contains $documentSource "error" "interactive story protocol document"
Assert-Contains $documentSource "Listening" "interactive story protocol document"
Assert-Contains $documentSource "Idle" "interactive story protocol document"

Write-Output "interactive story error contract: PASS"
