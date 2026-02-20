[CmdletBinding()]
param(
  [switch]$NoCleanup,
  [switch]$FailFast,
  [string]$ReturnBranch = "feat/add-hooks"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = (git rev-parse --show-toplevel 2>$null).Trim()
if (-not $repoRoot) { throw "Not inside a git repository." }
Set-Location $repoRoot

$stamp = (Get-Date).ToString("yyyyMMdd-HHmmss")
$resultsDir = Join-Path $repoRoot "Scripts\Tests\Test-UnrealSyncResults"
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null
$logPath = Join-Path $resultsDir "UnrealSyncTest-$stamp.log"

$script:PassCount = 0
$script:FailCount = 0
$script:WarnCount = 0
$script:SkipCount = 0
$script:TempBranches = New-Object System.Collections.Generic.List[string]
$script:CleanupRan = $false

$script:OriginalBranch = (git rev-parse --abbrev-ref HEAD 2>$null).Trim()
$script:OriginalHead = (git rev-parse HEAD 2>$null).Trim()

function Write-Log {
  param(
    [Parameter(Mandatory)][AllowEmptyString()][string]$Message,
    [ConsoleColor]$Color = [ConsoleColor]::Gray
  )
  Write-Host $Message -ForegroundColor $Color
  Add-Content -LiteralPath $logPath -Value $Message -Encoding UTF8
}

function Step([string]$Title) {
  Write-Log ""
  Write-Log "============================================================" DarkGray
  Write-Log $Title DarkGray
  Write-Log "============================================================" DarkGray
}

function Pass([string]$Name, [string]$Detail) {
  $script:PassCount++
  Write-Log "[PASS] $Name - $Detail" Green
}

function Fail([string]$Name, [string]$Detail) {
  $script:FailCount++
  Write-Log "[FAIL] $Name - $Detail" Red
  if ($FailFast) { throw "FAILFAST" }
}

function Warn([string]$Name, [string]$Detail) {
  $script:WarnCount++
  Write-Log "[WARN] $Name - $Detail" Yellow
}

function Skip([string]$Name, [string]$Detail) {
  $script:SkipCount++
  Write-Log "[SKIP] $Name - $Detail" DarkYellow
}

function Get-HeadSha {
  ((git rev-parse HEAD 2>$null) | Select-Object -First 1).Trim()
}

function Write-TextFileLf {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Content
  )

  $normalized = $Content -replace "`r`n", "`n" -replace "`r", "`n"
  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($Path, $normalized, $utf8NoBom)
}

function Invoke-Git {
  param(
    [Parameter(Mandatory)][string[]]$Args,
    [switch]$AllowFail
  )

  $display = "git " + ($Args -join " ")
  Write-Log ">> $display" DarkGray
  $out = @(& git @Args 2>&1)
  $code = $LASTEXITCODE

  foreach ($line in $out) {
    $text = "$line"
    if (-not [string]::IsNullOrWhiteSpace($text)) {
      Write-Log ("   " + $text.TrimEnd()) DarkGray
    }
  }

  if (-not $AllowFail -and $code -ne 0) {
    throw "Command failed (exit=$code): $display"
  }

  [pscustomobject]@{
    Code = $code
    Output = ($out | ForEach-Object { "$_" }) -join "`n"
  }
}

function Invoke-UnrealSyncCapture {
  param([Parameter(Mandatory)][string[]]$Args)

  $scriptPath = Join-Path $repoRoot "Scripts\Unreal\UnrealSync.ps1"
  $pwshArgs = @(
    "-NoLogo",
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $scriptPath
  ) + $Args

  Write-Log ">> pwsh $($pwshArgs -join ' ')" DarkGray
  $out = @(& pwsh @pwshArgs 2>&1)
  $code = $LASTEXITCODE

  foreach ($line in $out) {
    $text = "$line"
    if (-not [string]::IsNullOrWhiteSpace($text)) {
      Write-Log ("   " + $text.TrimEnd()) DarkGray
    }
  }

  [pscustomobject]@{
    Code = $code
    Output = ($out | ForEach-Object { "$_" }) -join "`n"
  }
}

function Assert-CodeZero {
  param([string]$Name, [int]$Code)
  if ($Code -eq 0) { Pass $Name "exit=0"; return }
  Fail $Name "expected exit=0, got exit=$Code"
}

function Assert-OutputEmpty {
  param([string]$Name, [string]$Output)
  if ([string]::IsNullOrWhiteSpace($Output)) { Pass $Name "no output"; return }
  Fail $Name "expected no output, got: $Output"
}

function Assert-OutputContains {
  param(
    [string]$Name,
    [string]$Output,
    [string]$Needle
  )
  if ($Output -like "*$Needle*") { Pass $Name "matched: $Needle"; return }
  Fail $Name "missing expected text: $Needle"
}

function Assert-Condition {
  param(
    [string]$Name,
    [bool]$Condition,
    [string]$PassDetail = "condition is true",
    [string]$FailDetail = "condition is false"
  )
  if ($Condition) { Pass $Name $PassDetail; return }
  Fail $Name $FailDetail
}

function Restore-RepoState {
  if ($script:CleanupRan) { return }
  $script:CleanupRan = $true

  if ($NoCleanup) {
    Warn "Cleanup" "NoCleanup set; leaving temp branch/data in place."
    return
  }

  Step "Cleanup"

  try {
    if ($script:OriginalBranch -and $script:OriginalBranch -ne "HEAD") {
      Invoke-Git -Args @("checkout", $script:OriginalBranch) -AllowFail | Out-Null
    }
    elseif ($script:OriginalHead) {
      Invoke-Git -Args @("checkout", "--detach", $script:OriginalHead) -AllowFail | Out-Null
    }
  }
  catch {
    Warn "Cleanup checkout" "$($_.Exception.Message)"
  }

  foreach ($b in ($script:TempBranches | Sort-Object -Unique)) {
    try {
      Invoke-Git -Args @("branch", "-D", "--", $b) -AllowFail | Out-Null
    }
    catch {
      Warn "Cleanup branch delete" "$b -> $($_.Exception.Message)"
    }
  }

  foreach ($p in @(
      (Join-Path $repoRoot "Intermediate\UE_Sync_LockTest"),
      (Join-Path $repoRoot "Content\Test\UE_Sync_Test")
    )) {
    try {
      if (Test-Path -LiteralPath $p) {
        Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
    catch { }
  }
}

try {
  Step "Unreal Sync Automated Tests ($stamp)"
  Write-Log "Repo: $repoRoot" Cyan
  Write-Log "Log : $logPath" Cyan

  $dirty = @((git status --porcelain 2>$null) | Where-Object { $_ -and $_.Trim() -ne "" })
  if ($dirty.Count -gt 0) {
    throw "Working tree is not clean. Commit/stash changes before running Test-UnrealSync.ps1."
  }

  Step "Prepare isolated test branch"
  $baseSha = Get-HeadSha
  $testBranch = "test/ue-sync-auto-$((Get-Date).ToString('HHmmss'))"
  Invoke-Git -Args @("checkout", "-B", $testBranch, $baseSha) | Out-Null
  $script:TempBranches.Add($testBranch) | Out-Null

  Step "Case 1: Hook non-branch checkout flag skips silently"
  $head0 = Get-HeadSha
  $res = Invoke-UnrealSyncCapture -Args @("-OldRev", $head0, "-NewRev", $head0, "-Flag", "0")
  Assert-CodeZero "UE Sync case 1 exit code" $res.Code
  Assert-OutputEmpty "UE Sync case 1 output" $res.Output

  Step "Case 2: No structural trigger changes are silent"
  $nonStructRel = "Content/Test/UE_Sync_Test/NonStructural.txt"
  $nonStructAbs = Join-Path $repoRoot $nonStructRel
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $nonStructAbs) | Out-Null
  Write-TextFileLf -Path $nonStructAbs -Content "UE sync non-structural $(Get-Date -Format o)`n"
  Invoke-Git -Args @("add", "--", $nonStructRel) | Out-Null
  Invoke-Git -Args @("commit", "-m", "test: ue sync non-structural change") | Out-Null

  $head1 = Get-HeadSha
  $res = Invoke-UnrealSyncCapture -Args @("-OldRev", $head0, "-NewRev", $head1, "-Flag", "1")
  Assert-CodeZero "UE Sync case 2 exit code" $res.Code
  Assert-OutputEmpty "UE Sync case 2 output" $res.Output

  Step "Case 3: Rebase marker causes silent skip"
  $gitDir = (git rev-parse --git-dir 2>$null).Trim()
  if (-not [System.IO.Path]::IsPathRooted($gitDir)) {
    $gitDir = Join-Path $repoRoot $gitDir
  }
  $rebaseMergeDir = Join-Path $gitDir "rebase-merge"

  if (Test-Path -LiteralPath $rebaseMergeDir) {
    Skip "UE Sync case 3 setup" "rebase-merge already exists (operation in progress)."
  }
  else {
    New-Item -ItemType Directory -Force -Path $rebaseMergeDir | Out-Null
    try {
      $res = Invoke-UnrealSyncCapture -Args @("-OldRev", $head0, "-NewRev", $head1, "-Flag", "1")
      Assert-CodeZero "UE Sync case 3 exit code" $res.Code
      Assert-OutputEmpty "UE Sync case 3 output" $res.Output
    }
    finally {
      Remove-Item -LiteralPath $rebaseMergeDir -Recurse -Force -ErrorAction SilentlyContinue
    }
  }

  Step "Case 4: Rebase reflog action causes silent skip"
  $previousReflogAction = $env:GIT_REFLOG_ACTION
  try {
    $env:GIT_REFLOG_ACTION = "rebase (pick)"
    $res = Invoke-UnrealSyncCapture -Args @("-OldRev", $head0, "-NewRev", $head1, "-Flag", "1")
    Assert-CodeZero "UE Sync case 4 exit code" $res.Code
    Assert-OutputEmpty "UE Sync case 4 output" $res.Output
  }
  finally {
    if ($null -eq $previousReflogAction) {
      Remove-Item Env:GIT_REFLOG_ACTION -ErrorAction SilentlyContinue
    }
    else {
      $env:GIT_REFLOG_ACTION = $previousReflogAction
    }
  }

  Step "Case 5: Hook suppression contract avoids duplicate UE Sync runs"
  $postCheckoutPath = Join-Path $repoRoot ".githooks\post-checkout"
  $hookCommonPath = Join-Path $repoRoot "Scripts\git-hooks\hook-common.sh"

  $postCheckoutText = Get-Content -LiteralPath $postCheckoutPath -Raw
  $hookCommonText = Get-Content -LiteralPath $hookCommonPath -Raw

  Assert-Condition `
    -Name "UE Sync case 5 hook-common has UE_SYNC_SUPPRESS gate" `
    -Condition (
      $hookCommonText -match 'case "\$\{UE_SYNC_SUPPRESS:-0\}"' -and
      $hookCommonText -match 'skip UnrealSync: suppressed by UE_SYNC_SUPPRESS'
    ) `
    -FailDetail "hook-common.sh missing UE_SYNC_SUPPRESS guard in hook_run_unrealsync"

  Assert-Condition `
    -Name "UE Sync case 5 post-checkout suppresses nested fetch/pull/stash commands" `
    -Condition (
      $postCheckoutText -match 'UE_SYNC_SUPPRESS=1 git fetch --all --prune --quiet' -and
      $postCheckoutText -match 'UE_SYNC_SUPPRESS=1 git pull --ff-only' -and
      $postCheckoutText -match 'UE_SYNC_SUPPRESS=1 git stash push -u' -and
      $postCheckoutText -match 'UE_SYNC_SUPPRESS=1 git stash pop "\$STASH_REF"'
    ) `
    -FailDetail "post-checkout missing one or more UE_SYNC_SUPPRESS-prefixed nested git commands"

  $runCount = [regex]::Matches($postCheckoutText, 'hook_run_unrealsync\s+"').Count
  Assert-Condition `
    -Name "UE Sync case 5 post-checkout calls hook_run_unrealsync once" `
    -Condition ($runCount -eq 1) `
    -PassDetail "hook_run_unrealsync calls=$runCount" `
    -FailDetail "expected 1 hook_run_unrealsync call in post-checkout, found $runCount"

  Step "Case 6: Structural change detection with DryRun"
  $structRel = "Source/Ghost_Game/UE_Sync_TestTmp_$((Get-Date).ToString('HHmmss')).cpp"
  $structAbs = Join-Path $repoRoot $structRel
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $structAbs) | Out-Null
  $structContent = @(
    '#include "CoreMinimal.h"'
    '// UE sync structural trigger file for automated test'
    ''
  ) -join "`n"
  Write-TextFileLf -Path $structAbs -Content $structContent

  Invoke-Git -Args @("add", "--", $structRel) | Out-Null
  Invoke-Git -Args @("commit", "-m", "test: ue sync structural change") | Out-Null

  $head2 = Get-HeadSha
  $res = Invoke-UnrealSyncCapture -Args @(
    "-OldRev", $head1,
    "-NewRev", $head2,
    "-Flag", "1",
    "-NonInteractive",
    "-DryRun"
  )

  Assert-CodeZero "UE Sync case 6 exit code" $res.Code
  Assert-OutputContains "UE Sync case 6 detects structural triggers" $res.Output "Structural C++ changes detected"
  Assert-OutputContains "UE Sync case 6 non-interactive path" $res.Output "Non-interactive execution detected; proceeding without confirmation."
  Assert-OutputContains "UE Sync case 6 dry-run path" $res.Output "DryRun enabled. Skipping cleanup/regeneration/build."

  Step "Case 7: Locked file in cleanup is handled gracefully (non-interactive)"
  $lockDir = Join-Path $repoRoot "Intermediate\UE_Sync_LockTest"
  $lockPath = Join-Path $lockDir "Locked.txt"
  New-Item -ItemType Directory -Force -Path $lockDir | Out-Null
  Set-Content -LiteralPath $lockPath -Encoding UTF8 -Value "locked $(Get-Date -Format o)"

  $fs = $null
  try {
    $fs = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    $res = Invoke-UnrealSyncCapture -Args @(
      "-Force",
      "-NoRegen",
      "-NoBuild",
      "-NonInteractive"
    )
    Assert-CodeZero "UE Sync case 7 exit code" $res.Code
    Assert-OutputContains "UE Sync case 7 lock warning" $res.Output "Could not clean 'Intermediate' because a file is in use."
  }
  finally {
    if ($fs) { $fs.Dispose() }
    if (Test-Path -LiteralPath $lockDir) {
      Remove-Item -LiteralPath $lockDir -Recurse -Force -ErrorAction SilentlyContinue
    }
  }

  Step "Summary"
  Write-Log ("PASS={0} FAIL={1} WARN={2} SKIP={3}" -f $script:PassCount, $script:FailCount, $script:WarnCount, $script:SkipCount) Cyan
  if ($script:FailCount -eq 0) {
    Write-Log "Unreal Sync automated tests passed." Green
  }
  else {
    Write-Log "Unreal Sync automated tests failed." Red
    exit 1
  }
}
catch {
  if ($_.Exception.Message -ne "FAILFAST") {
    Write-Log "[FATAL] $($_.Exception.Message)" Red
  }
  Write-Log ("PASS={0} FAIL={1} WARN={2} SKIP={3}" -f $script:PassCount, $script:FailCount, $script:WarnCount, $script:SkipCount) Cyan
  if ($script:FailCount -eq 0) { $script:FailCount = 1 }
  exit 1
}
finally {
  Restore-RepoState
  Write-Log ""
  Write-Log "Log saved: $logPath" Cyan
}
