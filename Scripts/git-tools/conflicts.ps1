param(
  [CmdletBinding()]
  [Parameter(Position=0)]
  [ValidateSet("ours","theirs","sync","status","abort","restart","continue","help")]
  [string]$Command = "help",

  [Parameter(ValueFromRemainingArguments=$true)]
  [string[]]$ArgsList
)

$repoRoot = (git rev-parse --show-toplevel 2>$null).Trim()
if (-not $repoRoot) { throw "Not inside a git repository." }

. (Join-Path $repoRoot "Scripts/git-tools/GitConflictHelpers.ps1")

# Parse flags
$VerboseMode = $PSBoundParameters.ContainsKey('Verbose') -or ($VerbosePreference -ne 'SilentlyContinue')
$SkipEditor = $false

# Extract -SkipEditor / --skip-editor from ArgsList
if ($ArgsList) {
  $filteredArgs = New-Object System.Collections.Generic.List[string]
  foreach ($arg in $ArgsList) {
    if ($arg -eq "-SkipEditor" -or $arg -eq "--skip-editor" -or $arg -eq "-se") {
      $SkipEditor = $true
    } else {
      $filteredArgs.Add($arg)
    }
  }
  $ArgsList = $filteredArgs.ToArray()
}

function Show-Help {
  Write-Host ""
  Write-Host "Unreal Binary Conflict Helpers" -ForegroundColor Cyan
  Write-Host "--------------------------------" -ForegroundColor Cyan
  Write-Host "These commands enforce safe resolution for guarded binary assets (e.g. .uasset/.umap/.png) during merges/rebases." -ForegroundColor Gray
  Write-Host "Use git ours / git theirs to pick a side for guarded files. Use git conflicts status/sync to inspect approvals." -ForegroundColor Gray
  Write-Host ""

  Write-Host "Usage:" -ForegroundColor Cyan
  Write-Host "  git ours   <pattern> [pattern...] [--verbose|-v]" -ForegroundColor Gray
  Write-Host "  git theirs <pattern> [pattern...] [--verbose|-v]" -ForegroundColor Gray
  Write-Host "  git conflicts <command> [--verbose|-v] [--skip-editor|-se]" -ForegroundColor Gray
  Write-Host ""

  Write-Host "Commands:" -ForegroundColor Cyan

  Write-Host "  ours" -ForegroundColor Green
  Write-Host "    Resolve guarded binary conflicts by choosing the OURS side." -ForegroundColor Gray
  Write-Host "    (In a rebase, ours/theirs are flipped to match human meaning.)" -ForegroundColor DarkGray
  Write-Host "    Examples:" -ForegroundColor DarkGray
  Write-Host '      git ours "Content/**/*.uasset"' -ForegroundColor DarkGray
  Write-Host '      git ours "**/*.png" -v' -ForegroundColor DarkGray
  Write-Host '      git ours "*" # chooses all conflicted files.' -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  theirs" -ForegroundColor Green
  Write-Host "    Resolve guarded binary conflicts by choosing the THEIRS side." -ForegroundColor Gray
  Write-Host "    Examples:" -ForegroundColor DarkGray
  Write-Host '      git theirs "Content/**/*.umap"' -ForegroundColor DarkGray
  Write-Host '      git theirs "Content/Test/*.png" -v' -ForegroundColor DarkGray
  Write-Host '      git theirs "*" # chooses all conflicted files.' -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  conflicts status" -ForegroundColor Green
  Write-Host "    Show current merge/rebase context and approval status." -ForegroundColor Gray
  Write-Host "    Default = summary. Use -v for full lists (unmerged/required/approved/remaining)." -ForegroundColor DarkGray
  Write-Host "    Examples:" -ForegroundColor DarkGray
  Write-Host "      git conflicts status" -ForegroundColor DarkGray
  Write-Host "      git conflicts status -v" -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  conflicts sync" -ForegroundColor Green
  Write-Host "    Recompute required guarded set and refresh context-bound ledgers." -ForegroundColor Gray
  Write-Host "    Default = summary. Use -v to print full status after syncing." -ForegroundColor DarkGray
  Write-Host "    Examples:" -ForegroundColor DarkGray
  Write-Host "      git conflicts sync" -ForegroundColor DarkGray
  Write-Host "      git conflicts sync -v" -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  conflicts continue" -ForegroundColor Green
  Write-Host "    Continue rebase after resolving conflicts (enforces guard before continuing)." -ForegroundColor Gray
  Write-Host "    Use --skip-editor or -se to skip commit message editing (automated tests)." -ForegroundColor DarkGray
  Write-Host "    Examples:" -ForegroundColor DarkGray
  Write-Host "      git conflicts continue" -ForegroundColor DarkGray
  Write-Host "      git conflicts continue --skip-editor" -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  conflicts abort" -ForegroundColor Green
  Write-Host "    Abort the current merge or rebase operation (no effect if none is active)." -ForegroundColor Gray
  Write-Host "    Example:" -ForegroundColor DarkGray
  Write-Host "      git conflicts abort" -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  conflicts restart" -ForegroundColor Green
  Write-Host "    Abort and attempt to re-run the current merge/rebase using detected args." -ForegroundColor Gray
  Write-Host "    (Merge is reliable. Rebase restart depends on metadata availability.)" -ForegroundColor DarkGray
  Write-Host "    Example:" -ForegroundColor DarkGray
  Write-Host "      git conflicts restart" -ForegroundColor DarkGray
  Write-Host ""

  Write-Host "  conflicts help" -ForegroundColor Green
  Write-Host "    Show this help." -ForegroundColor Gray
  Write-Host ""

  Write-Host "Notes:" -ForegroundColor Cyan
  Write-Host "  - Patterns are PowerShell -like wildcards (not bash globs). Quote patterns with * or **." -ForegroundColor Gray
  Write-Host '  - Example pattern: "**/*.uasset" or "Content/Test/*.png" or "*" for all conflicted files' -ForegroundColor Gray
  Write-Host "  - -v / --verbose prints detailed file lists and internal context info." -ForegroundColor Gray
  Write-Host "  - --skip-editor / -se skips commit message editing during rebase --continue (for automation)." -ForegroundColor Gray
  Write-Host ""
}

switch ($Command) {
  "help"   { Show-Help; break }

  "sync"   {
    [void](Sync-BinaryConflictLock)
    if ($VerboseMode) { Show-ConflictStatus -VerboseMode } else { Show-ConflictSummary }
    break
  }

  "status" {
    if ($VerboseMode) { Show-ConflictStatus -VerboseMode } else { Show-ConflictStatus }
    break
  }

  "continue" { Continue-RebaseWithGuard -SkipEditor:$SkipEditor; break }
  "abort"  { Abort-ConflictOperation; break }
  "restart"{ Restart-ConflictOperation; break }

  "ours"   {
    $Patterns = @($ArgsList | Where-Object { $_ -and $_.Trim() -ne "" })
    if (-not $Patterns -or $Patterns.Count -eq 0) { 
      Show-Help
      break
    }

    Resolve-BinaryConflicts -Side "ours" -Patterns $Patterns -VerboseMode:$VerboseMode
    break
  }

  "theirs" {
    $Patterns = @($ArgsList | Where-Object { $_ -and $_.Trim() -ne "" })
    if (-not $Patterns -or $Patterns.Count -eq 0) {
      Show-Help
      break
    }

    Resolve-BinaryConflicts -Side "theirs" -Patterns $Patterns -VerboseMode:$VerboseMode
    break
  }
}
