# Scripts/Tests/Test-BinaryGuard-Fixes.ps1
# Automated test script for strict binary guard + LEDGER model + helpers.
# Produces PASS/FAIL per test and a final summary log.
#
# Run:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Tests\Test-BinaryGuard-Fixes.ps1
#
# Options:
#   -VerifyPngs    : Opens PNGs for human verification at key points
#   -NoCleanup     : Leave branches + Content/Test in place
#   -SkipPreflight : Skip Scripts/git-hooks/Test-Hooks.ps1
#   -CleanupOnly   : ONLY run cleanup portion (safe to run after a failed test run)
#   -ReturnBranch  : Branch to return to after tests/cleanup (default feature/add-to-hooks, fallback main)
#   -PauseOnFail   : Rewind+replay to just before the failing moment and pause for inspection
#   -FailFast      : Quit immediately on first FAIL, run cleanup, and mark remaining tests as SKIPPED
#
# NOTE:
# - All assertion callsites MUST pass a unique test name.
# - This script uses New-TestName() to guarantee uniqueness even if the same base phrase is re-used.

[CmdletBinding()]
param(
  [switch]$VerifyPngs,
  [switch]$NoCleanup,
  [switch]$SkipPreflight,
  [switch]$CleanupOnly,
  [switch]$PauseOnFail,
  [switch]$FailFast,
  [string]$ReturnBranch = "feat/add-hooks"
)

$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"

# Source common functions.
. "${PSScriptRoot}\BinaryGuard-Test-Functions.ps1"

# Load global variables
if ($NoCleanup.IsPresent) { Initialize-BinaryGuardTestState -NoCleanup -ReturnBranch $ReturnBranch }
else { Initialize-BinaryGuardTestState -ReturnBranch $ReturnBranch }

if (-not $script:BaselineRef) { throw "Could not capture baseline HEAD." }

# ----------------------------
# Ctrl+C cleanup hook
# ----------------------------
$script:CancelEventSub = Register-ObjectEvent -InputObject ([Console]) -EventName CancelKeyPress -Action {
  try {
    $script:WasCancelled = $true
    $eventArgs.Cancel = $true
    if (Get-Command Cleanup -ErrorAction SilentlyContinue) { Cleanup }
  }
  catch { }
}

# ----------------------------
# MAIN
# ----------------------------
try {
  Step "Binary Guard Automated Tests ($stamp)"
  Log "Repo: $script:repoRoot" Cyan
  Log "Log : $script:logPath" Cyan

  if ($CleanupOnly) {
    Cleanup
    return
  }

  if (-not $SkipPreflight) {
    Step "Preflight: hook plumbing"
    $c = RunArgs pwsh @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "Scripts/git-hooks/Test-Hooks.ps1")
    $tn = New-TestName "Preflight runs hook plumbing test script"
    if ($c -eq 0) { Pass $tn "Test-Hooks.ps1 ok" } else { Warn $tn "Test-Hooks.ps1 exit=$c (proceeding)" }
  }

  Step "Reset to clean baseline"
  Invoke-AbortInProgress
  RunArgs git @("reset", "--hard") | Out-Null
  RunArgs git @("clean", "-fd") | Out-Null

  New-Item -ItemType Directory -Force "Content\Test\Glob" | Out-Null

  $pngRel = "Content/Test/GuardTest.png"
  $pngAbs = Join-Path $script:repoRoot $pngRel

  # ============================================================
  # Phase 1: Base/A/B (single file)
  # ============================================================
  Step "Phase 1: Create base/A/B (single file)"

  $base = "test/png-base"; Add-TestBranch $base
  RunArgs git @("checkout", "-B", $base) | Out-Null
  Write-LabeledPng -Path $pngAbs -Label "BASE" -Bg "White"
  RunArgs git @("add", ".gitattributes") | Out-Null
  RunArgs git @("add", $pngRel) | Out-Null
  Assert-CommitSucceeds (New-TestName "Base branch commits the initial guarded PNG") "test: base png" | Out-Null
  Assert-NotPointer (New-TestName "Base branch guarded PNG is not an LFS pointer in the working tree") $pngAbs | Out-Null
  OpenIfVerify $pngAbs

  $a = "test/png-a"; Add-TestBranch $a
  RunArgs git @("checkout", "-B", $a) | Out-Null
  Write-LabeledPng -Path $pngAbs -Label "A (OURS)" -Bg "Red"
  RunArgs git @("add", $pngRel) | Out-Null
  Assert-CommitSucceeds (New-TestName "Ours branch commits a different guarded PNG version") "test: png A" | Out-Null
  Assert-NotPointer (New-TestName "Ours branch guarded PNG is not an LFS pointer in the working tree") $pngAbs | Out-Null
  OpenIfVerify $pngAbs

  $b = "test/png-b"; Add-TestBranch $b
  RunArgs git @("checkout", "-B", $b, $base) | Out-Null
  Write-LabeledPng -Path $pngAbs -Label "B (THEIRS)" -Bg "Blue"
  RunArgs git @("add", $pngRel) | Out-Null
  Assert-CommitSucceeds (New-TestName "Theirs branch commits a different guarded PNG version") "test: png B" | Out-Null
  Assert-NotPointer (New-TestName "Theirs branch guarded PNG is not an LFS pointer in the working tree") $pngAbs | Out-Null
  OpenIfVerify $pngAbs

  # ============================================================
  # Phase 2: Merge (single) ours/theirs
  # ============================================================
  Step "Phase 2: Merge (single) + ours"

  $mO = "test/merge-ours"; Add-TestBranch $mO
  RunArgs git @("checkout", "-B", $mO, $a) | Out-Null
  RunArgs git @("merge", $b) | Out-Null
  if (Assert-Conflicts (New-TestName "Single-file merge produces a conflict") 1) {
    RunArgs -NoReplay git @("add", ".") | Out-Null
    Assert-CommitBlocked (New-TestName "After git add dot in a binary conflict, commit is blocked by the guard") | Out-Null

    RunArgs git @("ours", $pngRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving the single-file merge conflict with git ours clears conflicts") | Out-Null
    Assert-NotPointer (New-TestName "After resolving with git ours, the guarded PNG is not an LFS pointer") $pngAbs | Out-Null
    OpenIfVerify $pngAbs
    Assert-CommitSucceeds (New-TestName "After resolving with git ours, the merge commit succeeds") "test: merge ours resolved" | Out-Null
  }

  Step "Phase 2: Merge (single) + theirs"

  $mT = "test/merge-theirs"; Add-TestBranch $mT
  RunArgs git @("checkout", "-B", $mT, $a) | Out-Null
  RunArgs git @("merge", $b) | Out-Null
  if (Assert-Conflicts (New-TestName "Single-file merge produces a conflict (theirs path)") 1) {
    RunArgs git @("theirs", $pngRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving the single-file merge conflict with git theirs clears conflicts") | Out-Null
    Assert-NotPointer (New-TestName "After resolving with git theirs, the guarded PNG is not an LFS pointer") $pngAbs | Out-Null
    OpenIfVerify $pngAbs
    Assert-CommitSucceeds (New-TestName "After resolving with git theirs, the merge commit succeeds") "test: merge theirs resolved" | Out-Null
  }

  # ============================================================
  # Phase 3: Rebase (single) ours/theirs
  # ============================================================
  Step "Phase 3: Rebase (single) + ours"

  $rO = "test/rebase-ours"; Add-TestBranch $rO
  Invoke-AbortInProgress
  RunArgs git @("checkout", "-B", $rO, $a) | Out-Null
  RunArgs git @("rebase", $b) | Out-Null
  if (Assert-Conflicts (New-TestName "Single-file rebase produces a conflict") 1) {
    RunArgs git @("ours", $pngRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving the single-file rebase conflict with git ours clears conflicts") | Out-Null
    Assert-NotPointer (New-TestName "After resolving rebase with git ours, the guarded PNG is not an LFS pointer") $pngAbs | Out-Null
    OpenIfVerify $pngAbs
    Assert-ConflictsContinueSucceeds (New-TestName "After resolving with git ours, rebase continue succeeds") | Out-Null
  }

  Step "Phase 3: Rebase (single) + theirs"

  $rT = "test/rebase-theirs"; Add-TestBranch $rT
  Invoke-AbortInProgress
  RunArgs git @("checkout", "-B", $rT, $a) | Out-Null
  RunArgs git @("rebase", $b) | Out-Null
  if (Assert-Conflicts (New-TestName "Single-file rebase produces a conflict (theirs path)") 1) {
    RunArgs git @("theirs", $pngRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving the single-file rebase conflict with git theirs clears conflicts") | Out-Null
    Assert-NotPointer (New-TestName "After resolving rebase with git theirs, the guarded PNG is not an LFS pointer") $pngAbs | Out-Null
    OpenIfVerify $pngAbs
    Assert-ConflictsContinueSucceeds (New-TestName "After resolving with git theirs, rebase continue succeeds") | Out-Null
  }

  # ============================================================
  # Phase 4: Glob tests (multi) - validate by SHA256
  # ============================================================
  Step "Phase 4: Glob tests (multi): validate by SHA256"

  $globDirRel = "Content/Test/Glob"
  $globPaths = @(
    "$globDirRel/Asset1.png",
    "$globDirRel/Asset2.png",
    "$globDirRel/Asset3.png"
  )

  $gBase = "test/glob-base"
  $gA = "test/glob-a"
  $gB = "test/glob-b"
  Add-TestBranch $gBase; Add-TestBranch $gA; Add-TestBranch $gB

  RunCmd "git checkout -B $gBase $base" | Out-Null
  Write-GlobSet -LabelPrefix "BASE" -Bg "Green"
  RunArgs git @("add", $globDirRel) | Out-Null
  RunArgs git @("commit", "-m", "test: add glob set base") | Out-Null

  RunCmd "git checkout -B $gA $gBase" | Out-Null
  Write-GlobSet -LabelPrefix "A" -Bg "Red"
  RunArgs git @("add", $globDirRel) | Out-Null
  RunArgs git @("commit", "-m", "test: glob set A") | Out-Null
  $hashA = Get-Hashes -RelPaths $globPaths

  RunCmd "git checkout -B $gB $gBase" | Out-Null
  Write-GlobSet -LabelPrefix "B" -Bg "Blue"
  RunArgs git @("add", $globDirRel) | Out-Null
  RunArgs git @("commit", "-m", "test: glob set B") | Out-Null
  $hashB = Get-Hashes -RelPaths $globPaths

  $gMergeO = "test/glob-merge-ours"; Add-TestBranch $gMergeO
  RunCmd "git checkout -B $gMergeO $gA" | Out-Null
  RunCmd "git merge $gB" | Out-Null
  Assert-Conflicts (New-TestName "Glob merge produces conflicts across multiple guarded files") 3 | Out-Null
  RunArgs -NoReplay git @("add", ".") | Out-Null
  Assert-CommitBlocked (New-TestName "After git add dot in a glob binary conflict, commit is blocked by the guard") | Out-Null
  RunArgs git @("ours", "$globDirRel/*.png") | Out-Null
  Assert-NoConflicts (New-TestName "Resolving glob merge with git ours clears all conflicts") | Out-Null
  Assert-HashesEqual (New-TestName "Glob merge resolution via git ours selects the ours hash set") $hashA $globPaths | Out-Null
  RunArgs git @("commit", "-m", "test: glob merge ours resolved") | Out-Null

  $gMergeT = "test/glob-merge-theirs"; Add-TestBranch $gMergeT
  RunCmd "git checkout -B $gMergeT $gA" | Out-Null
  RunCmd "git merge $gB" | Out-Null
  Assert-Conflicts (New-TestName "Glob merge produces conflicts across multiple guarded files (theirs path)") 3 | Out-Null
  RunArgs git @("theirs", "$globDirRel/*.png") | Out-Null
  Assert-NoConflicts (New-TestName "Resolving glob merge with git theirs clears all conflicts") | Out-Null
  Assert-HashesEqual (New-TestName "Glob merge resolution via git theirs selects the theirs hash set") $hashB $globPaths | Out-Null
  RunArgs git @("commit", "-m", "test: glob merge theirs resolved") | Out-Null

  $gRebaseO = "test/glob-rebase-ours"; Add-TestBranch $gRebaseO
  Invoke-AbortInProgress
  RunCmd "git checkout -B $gRebaseO $gA" | Out-Null
  RunCmd "git rebase $gB" | Out-Null
  Assert-Conflicts (New-TestName "Glob rebase produces conflicts across multiple guarded files") 3 | Out-Null
  RunArgs git @("ours", "$globDirRel/*.png") | Out-Null
  Assert-NoConflicts (New-TestName "Resolving glob rebase with git ours clears all conflicts") | Out-Null
  Assert-HashesEqual (New-TestName "Glob rebase resolution via git ours selects the ours hash set") $hashA $globPaths | Out-Null
  Assert-ConflictsContinueSucceeds (New-TestName "After resolving glob rebase with git ours, rebase continue succeeds") | Out-Null

  $gRebaseT = "test/glob-rebase-theirs"; Add-TestBranch $gRebaseT
  Invoke-AbortInProgress
  RunCmd "git checkout -B $gRebaseT $gA" | Out-Null
  RunCmd "git rebase $gB" | Out-Null
  Assert-Conflicts (New-TestName "Glob rebase produces conflicts across multiple guarded files (theirs path)") 3 | Out-Null
  RunArgs git @("theirs", "$globDirRel/*.png") | Out-Null
  Assert-NoConflicts (New-TestName "Resolving glob rebase with git theirs clears all conflicts") | Out-Null
  Assert-HashesEqual (New-TestName "Glob rebase resolution via git theirs selects the theirs hash set") $hashB $globPaths | Out-Null
  Assert-ConflictsContinueSucceeds (New-TestName "After resolving glob rebase with git theirs, rebase continue succeeds") | Out-Null

  # ============================================================
  # Phase 4b: Partial approvals still block
  # ============================================================
  Step "Phase 4b: Partial approvals still block"

  $gPartial = "test/glob-partial-approval"; Add-TestBranch $gPartial
  Invoke-AbortInProgress
  RunCmd "git checkout -B $gPartial $gA" | Out-Null
  RunCmd "git merge $gB" | Out-Null
  Assert-Conflicts (New-TestName "Partial-approval setup merge produces glob conflicts") 3 | Out-Null
  RunArgs git @("ours", "$globDirRel/Asset1.png") | Out-Null
  RunArgs -NoReplay git @("add", ".") | Out-Null
  Assert-CommitBlocked (New-TestName "Approving only one of multiple guarded files still blocks the merge commit") | Out-Null
  RunArgs git @("merge", "--abort") | Out-Null

  # ============================================================
  # Phase 5: Stale approval protection
  # ============================================================
  Step "Phase 5: Stale approval protection"

  $stale = "test/stale-reset-check"; Add-TestBranch $stale
  Invoke-AbortInProgress
  RunCmd "git checkout -B $stale $a" | Out-Null
  RunCmd "git merge $b" | Out-Null
  if (Assert-Conflicts (New-TestName "Stale-approval test merge produces a conflict") 1) {
    RunArgs git @("ours", $pngRel) | Out-Null
    Assert-NoConflicts (New-TestName "Stale-approval test resolves the conflict with git ours") | Out-Null
    RunArgs git @("merge", "--abort") | Out-Null

    RunCmd "git merge $b" | Out-Null
    Assert-Conflicts (New-TestName "Stale-approval test reproduces the conflict after aborting and restarting merge") 1 | Out-Null

    RunArgs -NoReplay git @("add", ".") | Out-Null
    Assert-CommitBlocked (New-TestName "Stale approvals do not bypass the guard after restarting the merge") | Out-Null
    RunArgs git @("merge", "--abort") | Out-Null
  }

  # ============================================================
  # Phase 6: False-positive tests (text-only conflicts, mixed changes)
  # ============================================================
  Step "Phase 6: False-positive tests"

  Invoke-AbortInProgress
  RunArgs -NoReplay git @("config", "core.autocrlf", "false") | Out-Null
  RunArgs -NoReplay git @("config", "core.eol", "lf") | Out-Null
  RunArgs -NoReplay git @("config", "core.safecrlf", "true") | Out-Null

  Write-TextFile "Content/Test/LfCheck.txt" "line1`nline2`n"
  $code = RunArgs -NoReplay git @("add", "Content/Test/LfCheck.txt")
  Assert-True ($code -eq 0) (New-TestName "Write-TextFile writes LF so git add does not fail safecrlf checks") | Out-Null

  RunArgs -NoReplay git @("reset", "--hard") | Out-Null
  RunArgs -NoReplay git @("clean", "-fd") | Out-Null

  $txtRel = "Content/Test/TextConflict.txt"

  # FP1: Text conflict only; binary exists but unchanged => guard should not trigger
  $fp1Base = "test/fp1-base"; Add-TestBranch $fp1Base
  RunCmd "git checkout -B $fp1Base $base" | Out-Null
  Write-TextFile $txtRel "LINE=BASE`n"
  RunArgs git @("add", $txtRel) | Out-Null
  RunArgs git @("commit", "-m", "test: fp1 base text") | Out-Null

  $fp1A = "test/fp1-a"; Add-TestBranch $fp1A
  RunCmd "git checkout -B $fp1A $fp1Base" | Out-Null
  Write-TextFile $txtRel "LINE=OURS`n"
  RunArgs git @("add", $txtRel) | Out-Null
  RunArgs git @("commit", "-m", "test: fp1 ours text") | Out-Null

  $fp1B = "test/fp1-b"; Add-TestBranch $fp1B
  RunCmd "git checkout -B $fp1B $fp1Base" | Out-Null
  Write-TextFile $txtRel "LINE=THEIRS`n"
  RunArgs git @("add", $txtRel) | Out-Null
  RunArgs git @("commit", "-m", "test: fp1 theirs text") | Out-Null

  $fp1M = "test/fp1-merge"; Add-TestBranch $fp1M
  RunCmd "git checkout -B $fp1M $fp1A" | Out-Null
  RunCmd "git merge $fp1B" | Out-Null
  Assert-Conflicts (New-TestName "Text-only merge conflict occurs while guarded binary is untouched, and the guard should not be involved yet") 1 | Out-Null
  Resolve-TextConflict-Ours $txtRel
  Assert-NoConflicts (New-TestName "After resolving the text-only merge conflict, there are no remaining conflicts") | Out-Null
  Assert-CommitSucceeds (New-TestName "Resolving a text-only conflict should allow commit without any binary approvals") "Test: merge resolved text only" | Out-Null

  $fp1R = "test/fp1-rebase"; Add-TestBranch $fp1R
  Invoke-AbortInProgress
  RunCmd "git checkout -B $fp1R $fp1A" | Out-Null
  RunCmd "git rebase $fp1B" | Out-Null
  Assert-Conflicts (New-TestName "Text-only rebase conflict occurs while guarded binary is untouched, and the guard should not be involved yet") 1 | Out-Null
  Resolve-TextConflict-Ours $txtRel
  Assert-NoConflicts (New-TestName "After resolving the text-only rebase conflict, there are no remaining conflicts") | Out-Null
  Assert-ConflictsContinueSucceedsNoGuardWarning (New-TestName "Rebase continue after a text-only conflict should not print Binary Guard messaging") | Out-Null

  # FP2: Text conflicts, binary changed on one side only (never conflicted)
  $fp2Base = "test/fp2-base"; Add-TestBranch $fp2Base
  Invoke-AbortInProgress
  RunCmd "git checkout -B $fp2Base $base" | Out-Null
  Write-TextFile $txtRel "LINE=BASE`n"
  RunArgs git @("add", $txtRel) | Out-Null
  RunArgs git @("commit", "-m", "test: fp2 base") | Out-Null

  $fp2A = "test/fp2-a"; Add-TestBranch $fp2A
  RunCmd "git checkout -B $fp2A $fp2Base" | Out-Null
  Write-TextFile $txtRel "LINE=OURS`n"
  Write-LabeledPng -Path $pngAbs -Label "FP2-A" -Bg "Red"
  RunArgs git @("add", $txtRel, $pngRel) | Out-Null
  RunArgs git @("commit", "-m", "test: fp2 ours (text+binary)") | Out-Null
  $hashFp2A = Get-Hashes -RelPaths @($pngRel)

  $fp2B = "test/fp2-b"; Add-TestBranch $fp2B
  RunCmd "git checkout -B $fp2B $fp2Base" | Out-Null
  Write-TextFile $txtRel "LINE=THEIRS`n"
  RunArgs git @("add", $txtRel) | Out-Null
  RunArgs git @("commit", "-m", "test: fp2 theirs (text only)") | Out-Null

  $fp2M = "test/fp2-merge"; Add-TestBranch $fp2M
  RunCmd "git checkout -B $fp2M $fp2A" | Out-Null
  RunCmd "git merge $fp2B" | Out-Null
  Assert-Conflicts (New-TestName "Merge produces a text conflict while a guarded binary changed on only one side and never conflicted") 1 | Out-Null
  Resolve-TextConflict-Ours $txtRel
  Assert-NoConflicts (New-TestName "After resolving only the text conflict, there are no remaining conflicts") | Out-Null
  Assert-CommitSucceeds (New-TestName "Commit should succeed when guarded binary never conflicted and only text conflict was resolved") "Test: merge resolved text only" | Out-Null

  $fp2R = "test/fp2-rebase"; Add-TestBranch $fp2R
  Invoke-AbortInProgress
  RunCmd "git checkout -B $fp2R $fp2A" | Out-Null
  RunCmd "git rebase $fp2B" | Out-Null
  Assert-Conflicts (New-TestName "Rebase produces a text conflict while a guarded binary changed on only one side and never conflicted") 1 | Out-Null
  Resolve-TextConflict-Ours $txtRel
  Assert-NoConflicts (New-TestName "After resolving only the text conflict in rebase, there are no remaining conflicts") | Out-Null
  Assert-ConflictsContinueSucceedsNoGuardWarning (New-TestName "Rebase continue should succeed without Binary Guard messaging when guarded binary never conflicted") | Out-Null
  Assert-HashesEqual (New-TestName "Binary that never conflicted remains valid after rebase resolves the text conflict") $hashFp2A @($pngRel) | Out-Null

  # ============================================================
  # Phase 6b: Guarded delete/modify conflict (extra coverage)
  # ============================================================
  Step "Phase 6b: Guarded delete/modify conflict"

  $dmBase = "test/dm-base"; Add-TestBranch $dmBase
  Invoke-AbortInProgress
  RunCmd "git checkout -B $dmBase $base" | Out-Null

  $dmRel = "Content/Test/DeleteModify.png"
  $dmAbs = Join-Path $script:repoRoot $dmRel
  Write-LabeledPng -Path $dmAbs -Label "DM-BASE" -Bg "White"
  RunArgs git @("add", $dmRel) | Out-Null
  RunArgs git @("commit", "-m", "test: dm base") | Out-Null

  $dmDel = "test/dm-delete"; Add-TestBranch $dmDel
  RunCmd "git checkout -B $dmDel $dmBase" | Out-Null
  RunCmd "git rm -- $dmRel" | Out-Null
  RunArgs git @("commit", "-m", "test: dm delete") | Out-Null

  $dmMod = "test/dm-modify"; Add-TestBranch $dmMod
  RunCmd "git checkout -B $dmMod $dmBase" | Out-Null
  Write-LabeledPng -Path $dmAbs -Label "DM-MOD" -Bg "Red"
  RunArgs git @("add", $dmRel) | Out-Null
  RunArgs git @("commit", "-m", "test: dm modify") | Out-Null

  $dmMerge = "test/dm-merge"; Add-TestBranch $dmMerge
  RunCmd "git checkout -B $dmMerge $dmMod" | Out-Null
  RunCmd "git merge $dmDel" | Out-Null

  if (Assert-Conflicts (New-TestName "Delete/modify merge produces a conflict on a guarded binary") 1) {
    RunArgs -NoReplay git @("add", ".") | Out-Null
    Assert-CommitBlocked (New-TestName "Delete/modify merge commit is blocked after git add dot on guarded conflict") | Out-Null

    # Resolve by choosing ours (keep the modified file)
    RunArgs git @("ours", $dmRel) | Out-Null
    Assert-NoConflicts (New-TestName "Delete/modify conflict cleared by git ours") | Out-Null
    Assert-True (Test-Path -LiteralPath $dmAbs) (New-TestName "After choosing ours in delete/modify conflict, file still exists") | Out-Null
    Assert-CommitSucceeds (New-TestName "After resolving delete/modify conflict with git ours, merge commit succeeds") "test: dm merge ours resolved" | Out-Null
  }

  # ============================================================
  # Phase 7: Multi-commit rebase conflicts (two stops, deterministic)
  # Goal: BOTH commits stop with conflicts, and each stop conflicts on MULTIPLE files.
  # Also restore git add . -> guard blocking checks.
  # ============================================================
  Step "Phase 7: Multi-commit rebase conflicts (two stops, deterministic)"

  $mcBase = "test/mc2-base"; Add-TestBranch $mcBase
  Invoke-AbortInProgress
  RunCmd "git checkout -B $mcBase $base" | Out-Null

  $mc1Rel = "Content/Test/MC_File1.png"
  $mc2Rel = "Content/Test/MC_File2.png"
  $mc1Abs = Join-Path $script:repoRoot $mc1Rel
  $mc2Abs = Join-Path $script:repoRoot $mc2Rel
  $mcBoth = @($mc1Rel, $mc2Rel)

  # Base files
  Write-LabeledPng -Path $mc1Abs -Label "MC-BASE-1" -Bg "White"
  Write-LabeledPng -Path $mc2Abs -Label "MC-BASE-2" -Bg "White"
  RunArgs git @("add", $mc1Rel, $mc2Rel) | Out-Null
  RunArgs git @("commit", "-m", "test: mc2 base files") | Out-Null

  # ours: TWO commits, EACH modifies BOTH files
  $mcOurs = "test/mc2-ours"; Add-TestBranch $mcOurs
  RunCmd "git checkout -B $mcOurs $mcBase" | Out-Null

  Write-LabeledPng -Path $mc1Abs -Label "O1-F1" -Bg "Red"
  Write-LabeledPng -Path $mc2Abs -Label "O1-F2" -Bg "Red"
  RunArgs git @("add", $mc1Rel, $mc2Rel) | Out-Null
  RunArgs git @("commit", "-m", "test: mc2 ours commit1 both") | Out-Null

  Write-LabeledPng -Path $mc1Abs -Label "O2-F1" -Bg "Yellow"
  Write-LabeledPng -Path $mc2Abs -Label "O2-F2" -Bg "Yellow"
  RunArgs git @("add", $mc1Rel, $mc2Rel) | Out-Null
  RunArgs git @("commit", "-m", "test: mc2 ours commit2 both") | Out-Null

  # theirs: TWO commits, EACH modifies BOTH files differently
  $mcTheirs = "test/mc2-theirs"; Add-TestBranch $mcTheirs
  RunCmd "git checkout -B $mcTheirs $mcBase" | Out-Null

  Write-LabeledPng -Path $mc1Abs -Label "T1-F1" -Bg "Blue"
  Write-LabeledPng -Path $mc2Abs -Label "T1-F2" -Bg "Blue"
  RunArgs git @("add", $mc1Rel, $mc2Rel) | Out-Null
  RunArgs git @("commit", "-m", "test: mc2 theirs commit1 both") | Out-Null

  Write-LabeledPng -Path $mc1Abs -Label "T2-F1" -Bg "Green"
  Write-LabeledPng -Path $mc2Abs -Label "T2-F2" -Bg "Green"
  RunArgs git @("add", $mc1Rel, $mc2Rel) | Out-Null
  RunArgs git @("commit", "-m", "test: mc2 theirs commit2 both") | Out-Null

  # Rebase ours onto theirs => must stop twice
  $mcRebase = "test/mc2-rebase"; Add-TestBranch $mcRebase
  Invoke-AbortInProgress
  RunCmd "git checkout -B $mcRebase $mcOurs" | Out-Null
  RunCmd "git rebase $mcTheirs" | Out-Null

  # ---- Stop 1
  if (-not (Assert-Conflicts (New-TestName "Multi-commit rebase (stop 1) produces conflicts") 1)) {
    Fail (New-TestName "Multi-commit rebase expected to stop at commit 1") "rebase did not stop on first commit" ([Nullable[int]]$script:Replay.Count)
    Invoke-AbortInProgress
    return
  }
  Assert-ConflictsIncludeAll (New-TestName "Multi-commit rebase (stop 1) conflicts include both guarded files") $mcBoth | Out-Null

  # Verify guard blocks continue after git add .
  RunArgs -NoReplay git @("add", ".") | Out-Null
  Assert-ConflictsContinueBlocked (New-TestName "Multi-commit rebase (stop 1) conflicts continue blocked after git add dot") | Out-Null

  # Resolve stop 1 with THEIRS so commit2 is forced to conflict
  RunArgs git @("theirs", $mc1Rel, $mc2Rel) | Out-Null
  Assert-NoConflicts (New-TestName "Multi-commit rebase (stop 1) conflicts cleared after git theirs on both files") | Out-Null
  Assert-NotPointer (New-TestName "Multi-commit rebase (stop 1) file1 not LFS pointer") $mc1Abs | Out-Null
  Assert-NotPointer (New-TestName "Multi-commit rebase (stop 1) file2 not LFS pointer") $mc2Abs | Out-Null

  Assert-ConflictsContinueSucceeds (New-TestName "Multi-commit rebase continue after stop 1 succeeds") -AllowStopOnNextConflict | Out-Null

  # ---- Stop 2 (must now happen)
  if (-not (Assert-Conflicts (New-TestName "Multi-commit rebase (stop 2) produces conflicts") 1)) {
    Fail (New-TestName "Multi-commit rebase expected to stop at commit 2") "rebase did not stop on second commit" ([Nullable[int]]$script:Replay.Count)
    Invoke-AbortInProgress
  }
  Assert-ConflictsIncludeAll (New-TestName "Multi-commit rebase (stop 2) conflicts include both guarded files") $mcBoth | Out-Null

  # Verify guard blocks again
  RunArgs -NoReplay git @("add", ".") | Out-Null
  Assert-ConflictsContinueBlocked (New-TestName "Multi-commit rebase (stop 2) conflicts continue blocked after git add dot") | Out-Null

  # Resolve stop 2 with OURS
  RunArgs git @("ours", $mc1Rel, $mc2Rel) | Out-Null
  Assert-NoConflicts (New-TestName "Multi-commit rebase (stop 2) conflicts cleared after git ours on both files") | Out-Null
  Assert-NotPointer (New-TestName "Multi-commit rebase (stop 2) file1 not LFS pointer") $mc1Abs | Out-Null
  Assert-NotPointer (New-TestName "Multi-commit rebase (stop 2) file2 not LFS pointer") $mc2Abs | Out-Null

  Assert-ConflictsContinueSucceeds (New-TestName "Multi-commit rebase final continue after stop 2 succeeds") | Out-Null


  # ============================================================
  # Phase 8: Synthetic sidecar bundle tests (fully automated)
  # ============================================================
  Step "Phase 8: Synthetic sidecar bundle tests (automated)"

  $scDirRel = "Content/Test/SidecarSynthetic"
  $scStem = "$scDirRel/GuardSidecarBundle"
  $scUassetRel = "$scStem.uasset"
  $scUexpRel = "$scStem.uexp"
  $scUbulkRel = "$scStem.ubulk"
  $scAll = @($scUassetRel, $scUexpRel, $scUbulkRel)

  $scBase = "test/sc-base"; Add-TestBranch $scBase
  $scA = "test/sc-a"; Add-TestBranch $scA
  $scB = "test/sc-b"; Add-TestBranch $scB

  Invoke-AbortInProgress
  RunCmd "git checkout -B $scBase $base" | Out-Null
  Write-SyntheticSidecarBundle -StemRel $scStem -Tag "BASE"
  RunArgs git @("add", $scDirRel) | Out-Null
  RunArgs git @("commit", "-m", "test: sidecar base bundle") | Out-Null

  RunCmd "git checkout -B $scA $scBase" | Out-Null
  Write-SyntheticSidecarBundle -StemRel $scStem -Tag "A"
  RunArgs git @("add", $scDirRel) | Out-Null
  RunArgs git @("commit", "-m", "test: sidecar A bundle") | Out-Null
  $hashScA = Get-Hashes -RelPaths $scAll

  RunCmd "git checkout -B $scB $scBase" | Out-Null
  Write-SyntheticSidecarBundle -StemRel $scStem -Tag "B"
  RunArgs git @("add", $scDirRel) | Out-Null
  RunArgs git @("commit", "-m", "test: sidecar B bundle") | Out-Null
  $hashScB = Get-Hashes -RelPaths $scAll

  $scMergeO = "test/sc-merge-ours"; Add-TestBranch $scMergeO
  RunCmd "git checkout -B $scMergeO $scA" | Out-Null
  RunCmd "git merge $scB" | Out-Null
  if (Assert-Conflicts (New-TestName "Synthetic sidecar merge produces conflicts") 1) {
    Assert-GuardedConflictsIncludeAnyOf (New-TestName "Synthetic sidecar merge conflicts include guarded bundle files") $scAll | Out-Null
    RunArgs -NoReplay git @("add", ".") | Out-Null
    Assert-CommitBlocked (New-TestName "Synthetic sidecar merge commit is blocked after git add dot") | Out-Null

    # Pick the primary only; helper must carry sidecars consistently.
    RunArgs git @("ours", $scUassetRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving synthetic sidecar merge with git ours clears conflicts") | Out-Null
    Assert-HashesEqual (New-TestName "Synthetic sidecar merge resolution via git ours selects the ours bundle") $hashScA $scAll | Out-Null
    Assert-CommitSucceeds (New-TestName "Synthetic sidecar merge commit succeeds after helper resolution") "test: sidecar merge ours resolved" | Out-Null
  }

  $scMergeT = "test/sc-merge-theirs"; Add-TestBranch $scMergeT
  RunCmd "git checkout -B $scMergeT $scA" | Out-Null
  RunCmd "git merge $scB" | Out-Null
  if (Assert-Conflicts (New-TestName "Synthetic sidecar merge produces conflicts (theirs path)") 1) {
    RunArgs git @("theirs", $scUassetRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving synthetic sidecar merge with git theirs clears conflicts") | Out-Null
    Assert-HashesEqual (New-TestName "Synthetic sidecar merge resolution via git theirs selects the theirs bundle") $hashScB $scAll | Out-Null
    Assert-CommitSucceeds (New-TestName "Synthetic sidecar merge commit succeeds after git theirs") "test: sidecar merge theirs resolved" | Out-Null
  }

  $scRebaseO = "test/sc-rebase-ours"; Add-TestBranch $scRebaseO
  Invoke-AbortInProgress
  RunCmd "git checkout -B $scRebaseO $scA" | Out-Null
  RunCmd "git rebase $scB" | Out-Null
  if (Assert-Conflicts (New-TestName "Synthetic sidecar rebase produces conflicts") 1) {
    RunArgs -NoReplay git @("add", ".") | Out-Null
    Assert-ConflictsContinueBlocked (New-TestName "Synthetic sidecar rebase blocks conflicts continue after git add dot") | Out-Null
    RunArgs git @("ours", $scUassetRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving synthetic sidecar rebase with git ours clears conflicts") | Out-Null
    Assert-HashesEqual (New-TestName "Synthetic sidecar rebase resolution via git ours selects the ours bundle") $hashScA $scAll | Out-Null
    Assert-ConflictsContinueSucceeds (New-TestName "Synthetic sidecar rebase continue succeeds after git ours") | Out-Null
  }

  $scRebaseT = "test/sc-rebase-theirs"; Add-TestBranch $scRebaseT
  Invoke-AbortInProgress
  RunCmd "git checkout -B $scRebaseT $scA" | Out-Null
  RunCmd "git rebase $scB" | Out-Null
  if (Assert-Conflicts (New-TestName "Synthetic sidecar rebase produces conflicts (theirs path)") 1) {
    RunArgs -NoReplay git @("add", ".") | Out-Null
    Assert-ConflictsContinueBlocked (New-TestName "Synthetic sidecar rebase (theirs path) blocks conflicts continue after git add dot") | Out-Null
    RunArgs git @("theirs", $scUassetRel) | Out-Null
    Assert-NoConflicts (New-TestName "Resolving synthetic sidecar rebase with git theirs clears conflicts") | Out-Null
    Assert-HashesEqual (New-TestName "Synthetic sidecar rebase resolution via git theirs selects the theirs bundle") $hashScB $scAll | Out-Null
    Assert-ConflictsContinueSucceeds (New-TestName "Synthetic sidecar rebase continue succeeds after git theirs") | Out-Null
  }

  Step "Phase 9: Unreal sidecar integration moved to separate script"
  Skip (New-TestName "Manual Unreal sidecar integration phase") "run Scripts/Tests/Test-BinaryGuard-Unreal-Integration.ps1"

  # ============================================================
  # Summary
  # ============================================================
  Step "Summary"

  foreach ($t in @($script:PlannedSet)) {
    if (-not $script:RanSet.Contains($t)) {
      Skip $t "not executed (early termination)"
    }
  }

  $passCount = ($script:Results | Where-Object { $_.Status -eq "PASS" }).Count
  $failCount = ($script:Results | Where-Object { $_.Status -eq "FAIL" }).Count
  $warnCount = ($script:Results | Where-Object { $_.Status -eq "WARN" }).Count
  $skipCount = ($script:Results | Where-Object { $_.Status -eq "SKIP" }).Count

  Log "Total: $($script:Results.Count)  PASS: $passCount  FAIL: $failCount  WARN: $warnCount  SKIP: $skipCount" Cyan
  Log "" Cyan

  foreach ($r in $script:Results) {
    $c = if ($r.Status -eq "PASS") { "Green" }
    elseif ($r.Status -eq "WARN") { "Yellow" }
    elseif ($r.Status -eq "SKIP") { "DarkGray" }
    else { "Red" }
    Log ("{0,-5} {1} - {2}" -f $r.Status, $r.Test, $r.Detail) ([ConsoleColor]::$c)
  }

  Log "" Cyan
  Log "Log saved: $script:logPath" Cyan

  if (-not $script:NoCleanup) { Cleanup }
  else {
    Step "NoCleanup set - leaving branches + Content/Test"
    Log "Branches created:" Cyan
    foreach ($bname in ($script:TestBranches | Sort-Object)) { Log "  - $bname" DarkGray }
  }

  if ($failCount -gt 0) {
    throw "Binary guard tests had failures. See log: $script:logPath"
  }
}
catch {
  if ($_.Exception -is [System.OperationCanceledException] -or "$_" -match "Cancelled by user") {
    $script:WasCancelled = $true
    Log "" Yellow
    Log "[WARN] Cancelled by user (Ctrl+C). Will run cleanup and print summary." Yellow
  }
  elseif ("$_" -eq "FAILFAST") {
    Log "" Yellow
    Log "[WARN] FailFast triggered. Will run cleanup and print summary." Yellow
  }
  else {
    Log "" Yellow
    Log "[WARN] Exception: $_" Yellow
  }

  Step "Summary (Early Exit)"

  foreach ($t in @($script:PlannedSet)) {
    if (-not $script:RanSet.Contains($t)) {
      Skip $t "not executed (early termination)"
    }
  }

  $passCount = ($script:Results | Where-Object { $_.Status -eq "PASS" }).Count
  $failCount = ($script:Results | Where-Object { $_.Status -eq "FAIL" }).Count
  $warnCount = ($script:Results | Where-Object { $_.Status -eq "WARN" }).Count
  $skipCount = ($script:Results | Where-Object { $_.Status -eq "SKIP" }).Count

  Log "Total: $($script:Results.Count)  PASS: $passCount  FAIL: $failCount  WARN: $warnCount  SKIP: $skipCount" Cyan
  foreach ($r in $script:Results) {
    $c = if ($r.Status -eq "PASS") { "Green" }
    elseif ($r.Status -eq "WARN") { "Yellow" }
    elseif ($r.Status -eq "SKIP") { "DarkGray" }
    else { "Red" }
    Log ("{0,-5} {1} - {2}" -f $r.Status, $r.Test, $r.Detail) ([ConsoleColor]::$c)
  }

  Log "" Cyan
  Log "Log saved: $script:logPath" Cyan

  if (-not $script:NoCleanup) { Cleanup }
  throw
}
finally {
  try {
    if ($script:CancelEventSub) {
      Unregister-Event -SubscriptionId $script:CancelEventSub.Id -ErrorAction SilentlyContinue
      Remove-Job -Id $script:CancelEventSub.Id -Force -ErrorAction SilentlyContinue
      $script:CancelEventSub = $null
    }
  }
  catch { }

  if (-not $script:NoCleanup) {
    if (-not $script:CleanupRan) {
      try { Cleanup } catch { }
    }
  }
}
