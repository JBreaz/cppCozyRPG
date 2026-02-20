# Scripts/git-tools/GitConflictHelpers.ps1
# Strict Unreal binary conflict helpers for PowerShell 7+
#
# Guarded binary = gitattributes: merge=binary -text  (LFS not required)
#
# Ledgers in .git:
#   - ue_binary_conflicts.context   (context id to prevent stale approvals)
#   - ue_binary_conflicts.resolved  (approvals written by helpers)
#   - ue_binary_conflicts.audit     (audit trail)
#
# LOCKLESS MODEL:
#   "Required" guarded files are recomputed each run:
#     required = guarded(unmerged) ∪ guarded(overlap candidates)
#   Approvals live in resolved ledger only.

$ErrorActionPreference = "Stop"

if (-not $script:RunMemo) {
  $script:RunMemo = @{}
}

function Test-GuardPerfEnabled {
  $v = "$($env:UE_GUARD_PROFILE)".Trim().ToLowerInvariant()
  return ($v -eq "1" -or $v -eq "true" -or $v -eq "yes" -or $v -eq "on")
}

function Write-GuardPerf {
  param([Parameter(Mandatory)][string]$Message)
  if (-not (Test-GuardPerfEnabled)) { return }
  Write-Host "[conflicts][perf] $Message" -ForegroundColor DarkGray
}

function Clear-GuardMemo {
  param([string[]]$Keys)

  if (-not $script:RunMemo) { return }
  if (-not $Keys -or $Keys.Count -eq 0) {
    $script:RunMemo.Clear()
    return
  }

  foreach ($k in $Keys) {
    if ($k) { [void]$script:RunMemo.Remove($k) }
  }
}

# -----------------------------
# Repo / context / ledgers
# -----------------------------
function Get-GitDir {
  if ($script:RunMemo.ContainsKey("gitDir")) {
    return $script:RunMemo["gitDir"]
  }

  $gitDir = git rev-parse --git-dir 2>$null
  if (-not $gitDir) { throw "Not inside a git repository." }
  $resolved = (Resolve-Path -LiteralPath $gitDir).Path
  $script:RunMemo["gitDir"] = $resolved
  return $resolved
}

function Get-GitPath {
  param([Parameter(Mandatory)][string]$Path)
  $memoKey = "gitPath:$Path"
  if ($script:RunMemo.ContainsKey($memoKey)) {
    return $script:RunMemo[$memoKey]
  }

  $p = (git rev-parse --git-path $Path 2>$null).Trim()
  if (-not $p) {
    $script:RunMemo[$memoKey] = $null
    return $null
  }

  $script:RunMemo[$memoKey] = $p
  return $p
}

function Test-GitPathExists {
  param([Parameter(Mandatory)][string]$Path)
  $p = Get-GitPath -Path $Path
  if (-not $p) { return $false }
  Test-Path -LiteralPath $p
}

function Remove-StaleRebaseMarkers {
  # Aggressively clean REBASE_HEAD when rebase directories are gone
  # REBASE_HEAD is unreliable - Git leaves it behind after rebase completes
  
  # Only keep REBASE_HEAD if directories exist
  $rbm = Test-GitPathExists -Path "rebase-merge"
  $rba = Test-GitPathExists -Path "rebase-apply"
  
  if ($rbm -or $rba) {
    Write-Verbose "Rebase directories exist, keeping REBASE_HEAD"
    return
  }
  
  # No directories - aggressively clean REBASE_HEAD
  $rebaseHead = Get-GitPath -Path "REBASE_HEAD"
  if ($rebaseHead -and (Test-Path $rebaseHead)) {
    Write-Verbose "Cleaning stale REBASE_HEAD (no rebase directories)"
    Remove-Item -Force -LiteralPath $rebaseHead -ErrorAction SilentlyContinue
  }
}

function Get-GitContext {
  if ($script:RunMemo.ContainsKey("ctx")) {
    return $script:RunMemo["ctx"]
  }

  $ctx = "none"

  # Check merge first
  if (Test-GitPathExists -Path "MERGE_HEAD") { $ctx = "merge" }
  elseif (Test-GitPathExists -Path "CHERRY_PICK_HEAD") { $ctx = "merge" }
  elseif (Test-GitPathExists -Path "REVERT_HEAD") { $ctx = "merge" }
  
  # Check Git's rebase DIRECTORIES ONLY
  elseif (Test-GitPathExists -Path "rebase-merge") { $ctx = "rebase" }
  elseif (Test-GitPathExists -Path "rebase-apply") { $ctx = "rebase" }
  
  $script:RunMemo["ctx"] = $ctx
  return $ctx
}

# Back-compat (some of your functions referenced Get-Context)
function Get-Context { Get-GitContext }

function Get-LedgerPaths {
  $gitDir = Get-GitDir
  [pscustomobject]@{
    Context  = Join-Path $gitDir "ue_binary_conflicts.context"
    Resolved = Join-Path $gitDir "ue_binary_conflicts.resolved"
    Audit    = Join-Path $gitDir "ue_binary_conflicts.audit"
  }
}

function Read-GitFile {
  param([Parameter(Mandatory)][string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return $null }
  (Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue).Trim()
}

function Write-Audit {
  param(
    [Parameter(Mandatory)][string]$Action,
    [Parameter(Mandatory)][string]$Message
  )
  $p = Get-LedgerPaths
  $ts = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
  $user = $env:USERNAME
  $ctx = Get-GitContext
  try {
    Add-Content -LiteralPath $p.Audit -Value "[$ts] [$ctx] [$user] [$Action] $Message" -Encoding UTF8
  }
  catch {
    # Never break resolving due to audit issues
  }
}

# -----------------------------
# Guarded-binary detection
# -----------------------------
function Normalize-RepoPath {
  param([Parameter(Mandatory)][string]$Path)

  $p = "$Path"
  if ($null -eq $p) { return $null }
  $p = $p.Trim()
  if (-not $p) { return $null }
  $p = $p -replace '^[.][\\/]', ''
  $p = $p -replace '\\', '/'
  $p
}

if (-not $script:GuardedAttrCache) {
  $script:GuardedAttrCache = New-Object 'System.Collections.Generic.Dictionary[string,bool]' ([System.StringComparer]::OrdinalIgnoreCase)
}

function Get-GuardedAttrMapForPaths {
  param(
    [AllowEmptyCollection()]
    [string[]]$Paths = @()
  )

  $result = @{}
  $normalized = @(
    $Paths |
    ForEach-Object { if ($_ -ne $null) { Normalize-RepoPath $_ } } |
    Where-Object { $_ } |
    Sort-Object -Unique
  )

  if (-not $normalized -or $normalized.Count -eq 0) { return $result }

  $toQuery = New-Object System.Collections.Generic.List[string]
  foreach ($p in $normalized) {
    if ($script:GuardedAttrCache.ContainsKey($p)) {
      $result[$p] = $script:GuardedAttrCache[$p]
    }
    else {
      $toQuery.Add($p) | Out-Null
    }
  }

  if ($toQuery.Count -gt 0) {
    $chunkSize = 200
    for ($i = 0; $i -lt $toQuery.Count; $i += $chunkSize) {
      $end = [Math]::Min($i + $chunkSize - 1, $toQuery.Count - 1)
      $chunk = @($toQuery[$i..$end])

      $raw = @(
        & git check-attr --cached merge text -- @chunk 2>$null |
        ForEach-Object { "$_".Trim() } |
        Where-Object { $_ }
      )

      $state = @{}
      foreach ($line in $raw) {
        if ($line -match '^(.*):\s+(merge|text):\s+(.*)$') {
          $path = Normalize-RepoPath $Matches[1]
          $attr = $Matches[2]
          $val = $Matches[3].Trim()
          if (-not $path) { continue }
          if (-not $state.ContainsKey($path)) {
            $state[$path] = @{ merge = $null; text = $null }
          }
          $state[$path][$attr] = $val
        }
      }

      foreach ($p in $chunk) {
        $isGuarded = $false
        if ($state.ContainsKey($p)) {
          $m = $state[$p]["merge"]
          $t = $state[$p]["text"]
          $isGuarded = ($m -eq "binary" -and $t -eq "unset")
        }
        $script:GuardedAttrCache[$p] = $isGuarded
        $result[$p] = $isGuarded
      }
    }
  }

  return $result
}

function Get-GuardedPathsFromList {
  param(
    [AllowEmptyCollection()]
    [string[]]$Paths = @()
  )

  $map = Get-GuardedAttrMapForPaths -Paths $Paths
  if (-not $map.Keys -or $map.Keys.Count -eq 0) { return @() }

  @(
    $map.Keys |
    Where-Object { $map[$_] } |
    Sort-Object -Unique
  )
}

function Test-IsGuardedLfsBinary {
  param([Parameter(Mandatory)][string]$Path)

  $p = Normalize-RepoPath $Path
  if (-not $p) { return $false }

  $map = Get-GuardedAttrMapForPaths -Paths @($p)
  if (-not $map.ContainsKey($p)) { return $false }
  [bool]$map[$p]
}

# -----------------------------
# Conflict discovery
# -----------------------------
function Get-ConflictedPaths {
  if ($script:RunMemo.ContainsKey("conflicted")) {
    return @($script:RunMemo["conflicted"])
  }

  $out = @(
    git diff --name-only --diff-filter=U 2>$null |
    Where-Object { $_ -and $_.Trim() -ne "" } |
    ForEach-Object { $_.Trim() }
  )

  $script:RunMemo["conflicted"] = @($out)
  return @($out)
}

function Get-UnmergedPaths {
  if ($script:RunMemo.ContainsKey("unmerged")) {
    return @($script:RunMemo["unmerged"])
  }

  # Robust parse of `git ls-files -u` (path is after tab)
  $raw = git ls-files -u 2>$null
  if (-not $raw) {
    $script:RunMemo["unmerged"] = @()
    return @()
  }

  $paths = New-Object System.Collections.Generic.List[string]
  foreach ($line in ($raw -split "`r?`n")) {
    if (-not $line) { continue }
    $tab = $line.IndexOf("`t")
    if ($tab -lt 0) { continue }
    $p = $line.Substring($tab + 1).Trim()
    if ($p) { $paths.Add($p) | Out-Null }
  }

  $out = @($paths | Sort-Object -Unique)
  $script:RunMemo["unmerged"] = @($out)
  return @($out)
}

# -----------------------------
# Merge/Rebase operation refs
# -----------------------------
function Get-MergeHeadSha {
  $p = Get-GitPath -Path "MERGE_HEAD"
  if (-not $p) { return $null }
  Read-GitFile $p
}

function Get-RebasePatchSha {
  $patches = @(
    (Get-GitPath -Path "rebase-merge/patch"),
    (Get-GitPath -Path "rebase-apply/patch")
  )

  foreach ($p in $patches) {
    if (-not $p) { continue }
    if (-not (Test-Path -LiteralPath $p)) { continue }
    $line = Get-Content -LiteralPath $p -TotalCount 1 -ErrorAction SilentlyContinue
    if ($line -match '^From\s+([0-9a-f]{7,40})\b') { return $Matches[1] }
  }

  return $null
}

function Get-RebasePatchPaths {
  $patches = @(
    (Get-GitPath -Path "rebase-merge/patch"),
    (Get-GitPath -Path "rebase-apply/patch")
  )

  foreach ($p in $patches) {
    if (-not $p) { continue }
    if (-not (Test-Path -LiteralPath $p)) { continue }
    try {
      if ((Get-Item -LiteralPath $p).Length -eq 0) { continue }
    }
    catch { continue }

    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($line in (Get-Content -LiteralPath $p -ErrorAction SilentlyContinue)) {
      if ($line -match '^\+\+\+\s+(.+)$') {
        $f = $Matches[1] -replace '^b/', ''
        if ($f -and $f -ne '/dev/null') { $paths.Add($f) | Out-Null }
        continue
      }
      if ($line -match '^---\s+(.+)$') {
        $f = $Matches[1] -replace '^a/', ''
        if ($f -and $f -ne '/dev/null') { $paths.Add($f) | Out-Null }
        continue
      }
    }

    if ($paths.Count -gt 0) { return ($paths | Sort-Object -Unique) }
  }

  return @()
}

function Get-RebaseSeqCurrentSha {
  $done = Get-GitPath -Path "rebase-merge/done"
  $todo = Get-GitPath -Path "rebase-merge/git-rebase-todo"

  if ($done -and (Test-Path -LiteralPath $done)) {
    $lines = Get-Content -LiteralPath $done -ErrorAction SilentlyContinue |
    Where-Object { $_ -and ($_ -notmatch '^\s*#') }
    if ($lines) {
      $last = $lines | Select-Object -Last 1
      if ($last -match '^\s*\S+\s+([0-9a-fA-F]{7,40})\b') { return $Matches[1] }
    }
  }

  if ($todo -and (Test-Path -LiteralPath $todo)) {
    $lines = Get-Content -LiteralPath $todo -ErrorAction SilentlyContinue |
    Where-Object { $_ -and ($_ -notmatch '^\s*#') }
    if ($lines) {
      $first = $lines | Select-Object -First 1
      if ($first -match '^\s*\S+\s+([0-9a-fA-F]{7,40})\b') { return $Matches[1] }
    }
  }

  return $null
}

function Get-RebaseHeadSha {
  if ($script:RunMemo.ContainsKey("rebaseHead")) {
    return $script:RunMemo["rebaseHead"]
  }

  # Get the SHA of the commit being applied during rebase
  
  # Prefer stopped-sha if rebase is stopped
  try {
    $stoppedFile = Get-GitPath -Path "rebase-merge/stopped-sha"
    if ($stoppedFile -and (Test-Path $stoppedFile -PathType Leaf)) {
      $stopped = (Get-Content $stoppedFile -Raw -ErrorAction SilentlyContinue)
      if ($stopped) {
        $stopped = $stopped.Trim()
        if ($stopped) {
          $script:RunMemo["rebaseHead"] = $stopped
          return $stopped
        }
      }
    }
  }
  catch {
    # Ignore errors
  }

  # Fall back to REBASE_HEAD
  try {
    $sha = (git rev-parse -q --verify REBASE_HEAD 2>$null)
    if ($sha) {
      $sha = $sha.Trim()
      if ($sha) {
        $script:RunMemo["rebaseHead"] = $sha
        return $sha
      }
    }
  }
  catch {
    # Ignore errors
  }

  # Try CHERRY_PICK_HEAD
  $cp = Get-GitPath -Path "CHERRY_PICK_HEAD"
  $sha = if ($cp) { Read-GitFile $cp } else { $null }
  if ($sha) {
    $script:RunMemo["rebaseHead"] = $sha
    return $sha
  }

  # Try patch file
  $sha = Get-RebasePatchSha
  if ($sha) {
    $script:RunMemo["rebaseHead"] = $sha
    return $sha
  }

  # Try sequence file
  $sha = Get-RebaseSeqCurrentSha
  if ($sha) {
    $script:RunMemo["rebaseHead"] = $sha
    return $sha
  }

  # Last resort: orig-head
  $origMerge = Get-GitPath -Path "rebase-merge/orig-head"
  $sha = if ($origMerge) { Read-GitFile $origMerge } else { $null }
  if ($sha) {
    $script:RunMemo["rebaseHead"] = $sha
    return $sha
  }

  $script:RunMemo["rebaseHead"] = $null
  return $null
}

function Get-RebaseOntoSha {
  if ($script:RunMemo.ContainsKey("rebaseOnto")) {
    return $script:RunMemo["rebaseOnto"]
  }

  # During rebase, HEAD moves as commits are applied.
  # Using HEAD for overlap checks causes false positives because it already contains
  # the in-progress commit's changes. The stable "onto" SHA is stored by git.
  
  # Try rebase-merge/onto first
  try {
    $ontoFile = Get-GitPath -Path "rebase-merge/onto"
    if ($ontoFile -and (Test-Path $ontoFile -PathType Leaf)) {
      $onto = (Get-Content $ontoFile -Raw -ErrorAction SilentlyContinue)
      if ($onto) {
        $onto = $onto.Trim()
        if ($onto) {
          $script:RunMemo["rebaseOnto"] = $onto
          return $onto
        }
      }
    }
  }
  catch {
    # Ignore errors
  }

  # Try rebase-apply/onto
  try {
    $ontoFile = Get-GitPath -Path "rebase-apply/onto"
    if ($ontoFile -and (Test-Path $ontoFile -PathType Leaf)) {
      $onto = (Get-Content $ontoFile -Raw -ErrorAction SilentlyContinue)
      if ($onto) {
        $onto = $onto.Trim()
        if ($onto) {
          $script:RunMemo["rebaseOnto"] = $onto
          return $onto
        }
      }
    }
  }
  catch {
    # Ignore errors
  }

  # Soft-fail: return null instead of error
  # This allows calling code to continue with fallback logic
  $script:RunMemo["rebaseOnto"] = $null
  return $null
}

function Get-OtherSideSha {
  $ctx = Get-GitContext
  if ($ctx -eq "merge") { return Get-MergeHeadSha }
  if ($ctx -eq "rebase") { return Get-RebaseHeadSha }
  return $null
}

function Get-MTimeEpoch {
  param([Parameter(Mandatory)][string]$Path)
  try {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    # Unix epoch seconds, stable across TZ
    return [DateTimeOffset]::new($item.LastWriteTimeUtc).ToUnixTimeSeconds()
  }
  catch {
    return $null
  }
}

function Get-OperationStamp {
  $ctx = Get-GitContext
  $gitDir = Get-GitDir

  if ($ctx -eq "merge") {
    # MERGE_HEAD rewritten each time a merge starts
    $s = Get-MTimeEpoch -Path (Join-Path $gitDir "MERGE_HEAD")
    return ($s ? $s.ToString() : "nostamp")
  }

  if ($ctx -eq "rebase") {
    $rm = Join-Path $gitDir "rebase-merge"
    $ra = Join-Path $gitDir "rebase-apply"

    # IMPORTANT: directory mtimes change during --continue.
    # Use stable marker files instead so stamp stays constant for the whole rebase.
    $candidates = @(
      (Join-Path $rm "onto"),
      (Join-Path $rm "head-name"),
      (Join-Path $ra "orig-head")
    )

    foreach ($c in $candidates) {
      $s = Get-MTimeEpoch -Path $c
      if ($s) { return $s.ToString() }
    }

    return "nostamp"
  }

  return $null
}

# -----------------------------
# Context-bound ledger (prevents stale approvals)
# -----------------------------
function Get-OperationContextId {
  if ($script:RunMemo.ContainsKey("ctxId")) {
    return $script:RunMemo["ctxId"]
  }

  $ctx = Get-GitContext
  if ($ctx -eq "none") {
    $script:RunMemo["ctxId"] = $null
    return $null
  }

  $stamp = Get-OperationStamp
  if (-not $stamp) { $stamp = "nostamp" }

  if ($ctx -eq "merge") {
    # Merge: use MERGE_HEAD as the "other" side
    $other = Get-MergeHeadSha
    if (-not $other) {
      $id = "${ctx}:unknown:unknown:${stamp}"
      $script:RunMemo["ctxId"] = $id
      return $id
    }

    $base = (git merge-base HEAD $other 2>$null)
    if ($base) { $base = $base.Trim() }
    if (-not $base) {
      $id = "${ctx}:${other}:nobase:${stamp}"
      $script:RunMemo["ctxId"] = $id
      return $id
    }

    $id = "${ctx}:${other}:${base}:${stamp}"
    $script:RunMemo["ctxId"] = $id
    return $id
  }

  if ($ctx -eq "rebase") {
    # Rebase: scope context to the CURRENT stopped commit so approvals do not
    # carry from stop N to stop N+1 in a multi-commit rebase.
    $onto = Get-RebaseOntoSha
    $current = Get-RebaseHeadSha

    # Fallback for unusual states where rebase head is temporarily unavailable.
    if (-not $current) {
      $origPath = Get-GitPath -Path "rebase-merge/orig-head"
      if (-not $origPath) {
        $origPath = Get-GitPath -Path "rebase-apply/orig-head"
      }
      if ($origPath -and (Test-Path $origPath)) {
        $current = (Get-Content $origPath -Raw -ErrorAction SilentlyContinue)
        if ($current) { $current = $current.Trim() }
      }
    }

    if (-not $onto) { $onto = "unknown" }
    if (-not $current) { $current = "unknown" }

    $id = "${ctx}:${onto}:${current}:${stamp}"
    $script:RunMemo["ctxId"] = $id
    return $id
  }

  # Fallback for other contexts
  $other = Get-OtherSideSha
  if (-not $other) {
    $id = "${ctx}:unknown:unknown:${stamp}"
    $script:RunMemo["ctxId"] = $id
    return $id
  }

  $base = (git merge-base HEAD $other 2>$null)
  if ($base) { $base = $base.Trim() }
  if (-not $base) {
    $id = "${ctx}:${other}:nobase:${stamp}"
    $script:RunMemo["ctxId"] = $id
    return $id
  }

  $id = "${ctx}:${other}:${base}:${stamp}"
  $script:RunMemo["ctxId"] = $id
  return $id
}

function Ensure-ContextBoundLedgers {
  $p = Get-LedgerPaths
  $ctx = Get-GitContext
  $id = Get-OperationContextId

  if ($ctx -eq "none" -or -not $id) {
    # No active operation: clear ledgers
    if (Test-Path -LiteralPath $p.Context) { Remove-Item -Force -LiteralPath $p.Context -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $p.Resolved) { Remove-Item -Force -LiteralPath $p.Resolved -ErrorAction SilentlyContinue }
    Clear-GuardMemo -Keys @("approved", "required", "remaining", "unmerged", "conflicted", "overlap", "guardedOverlap", "ctxId", "rebaseHead", "rebaseOnto")
    return
  }

  $prev = Read-GitFile $p.Context
  if ($prev -and $prev -eq $id) { return }

  # New operation detected → wipe approvals
  Set-Content -LiteralPath $p.Context -Value $id -Encoding UTF8
  if (Test-Path -LiteralPath $p.Resolved) { Remove-Item -Force -LiteralPath $p.Resolved -ErrorAction SilentlyContinue }
  Clear-GuardMemo -Keys @("approved", "required", "remaining", "unmerged", "conflicted", "overlap", "guardedOverlap", "ctxId", "rebaseHead", "rebaseOnto")

  Write-Audit -Action "CTXRESET" -Message "context changed -> reset resolved to prevent stale approvals ($id)"
}

# -----------------------------
# Overlap candidates (covers 'git add .' cases where conflicts disappear)
# -----------------------------

function Get-PathsChangedInCommit {
  param([Parameter(Mandatory)][string]$Commit)

  @(
    (git diff-tree --no-commit-id --name-only -r $Commit 2>$null) -split "`r?`n" |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ }
  ) | Sort-Object -Unique
}
function Get-OverlapCandidates {
  if ($script:RunMemo.ContainsKey("overlap")) {
    return @($script:RunMemo["overlap"])
  }

  $ctx = Get-GitContext
  if ($ctx -eq "none") {
    $script:RunMemo["overlap"] = @()
    return @()
  }

  $other = Get-OtherSideSha
  if (-not $other) {
    $script:RunMemo["overlap"] = @()
    return @()
  }

  # MERGE: keep existing behavior
  if ($ctx -eq "merge") {
    $left = "HEAD"
    $base = (git merge-base $left $other 2>$null)
    if ($base) { $base = $base.Trim() }
    if (-not $base) {
      $script:RunMemo["overlap"] = @()
      return @()
    }

    $a = @((git diff --name-only $base $left  2>$null) -split "`r?`n" | Where-Object { $_ })
    $b = @((git diff --name-only $base $other 2>$null) -split "`r?`n" | Where-Object { $_ })

    if ($a.Count -eq 0 -or $b.Count -eq 0) {
      $script:RunMemo["overlap"] = @()
      return @()
    }

    $setB = @{}
    foreach ($x in $b) { $setB[$x.Trim()] = $true }

    $over = New-Object System.Collections.Generic.List[string]
    foreach ($x in $a) {
      $t = $x.Trim()
      if ($t -and $setB.ContainsKey($t)) { $over.Add($t) | Out-Null }
    }

    $out = @($over | Sort-Object -Unique)
    $script:RunMemo["overlap"] = @($out)
    return @($out)
  }

  # REBASE: per-current-commit overlap (NO CACHING)
  if ($ctx -eq "rebase") {
    $onto = Get-RebaseOntoSha
    $parent = $null
    try {
      $parent = (git rev-parse -q --verify "${other}^" 2>$null)
      if ($parent) { $parent = $parent.Trim() }
    }
    catch {
      # Ignore errors
    }

    # Prefer merge-tree for accurate overlap
    if ($onto -and $parent) {
      Write-Verbose "Rebase overlap: using merge-tree (parent=$parent onto=$onto other=$other)"
      
      $basePaths = @(
        (git diff --name-only $parent $other 2>$null) -split "`r?`n" |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ }
      ) | Sort-Object -Unique
    
      $targetPaths = @(
        (git diff --name-only $parent $onto 2>$null) -split "`r?`n" |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ }
      ) | Sort-Object -Unique
    
      # Only include paths modified on BOTH sides
      $setTarget = @{}
      foreach ($p in $targetPaths) { $setTarget[$p] = $true }
    
      $actualOverlap = @(
        foreach ($p in $basePaths) {
          if ($setTarget.ContainsKey($p)) { $p }
        }
      )
    
      Write-Verbose "Rebase overlap: found $($actualOverlap.Count) actual overlaps"
      $out = @($actualOverlap | Sort-Object -Unique)
      $script:RunMemo["overlap"] = @($out)
      return @($out)
    }

    Write-Verbose "Rebase overlap: using fallback diff"

    # Fallback: diff intersection
    $base = if ($parent) { $parent } else { (git merge-base HEAD $other 2>$null) }
    if ($base) { $base = $base.Trim() }
    if (-not $base) {
      $script:RunMemo["overlap"] = @()
      return @()
    }

    $target = if ($onto) { $onto } else { "HEAD" }
    
    $headPaths = @(
      (git diff --name-only $base $target 2>$null) -split "`r?`n" |
      ForEach-Object { $_.Trim() } |
      Where-Object { $_ }
    ) | Sort-Object -Unique

    $commitPaths = @(
      (git diff --name-only $base $other 2>$null) -split "`r?`n" |
      ForEach-Object { $_.Trim() } |
      Where-Object { $_ }
    ) | Sort-Object -Unique

    if ($headPaths.Count -eq 0 -or $commitPaths.Count -eq 0) {
      $script:RunMemo["overlap"] = @()
      return @()
    }

    $setCommit = @{}
    foreach ($p in $commitPaths) { $setCommit[$p] = $true }

    $over = New-Object System.Collections.Generic.List[string]
    foreach ($p in $headPaths) {
      if ($setCommit.ContainsKey($p)) { $over.Add($p) | Out-Null }
    }

    $out = @($over | Sort-Object -Unique)
    $script:RunMemo["overlap"] = @($out)
    return @($out)
  }

  $script:RunMemo["overlap"] = @()
  return @()
}



function Get-GuardedOverlapCandidates {
  if ($script:RunMemo.ContainsKey("guardedOverlap")) {
    return @($script:RunMemo["guardedOverlap"])
  }

  $overlap = @(Get-OverlapCandidates)
  if (-not $overlap -or $overlap.Count -eq 0) {
    $script:RunMemo["guardedOverlap"] = @()
    return @()
  }

  $out = @(Get-GuardedPathsFromList -Paths $overlap)
  $script:RunMemo["guardedOverlap"] = @($out)
  return @($out)
}

# -----------------------------
# REQUIRED guarded set (recomputed each run)
# -----------------------------
function Get-RequiredGuardedPaths {
  if ($script:RunMemo.ContainsKey("required")) {
    return @($script:RunMemo["required"])
  }

  $ctx = Get-GitContext
  if ($ctx -eq "none") {
    $script:RunMemo["required"] = @()
    return @()
  }

  Ensure-ContextBoundLedgers

  $req = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)

  foreach ($p in @(Get-GuardedPathsFromList -Paths @(Get-UnmergedPaths))) {
    if ($p) { [void]$req.Add($p) }
  }

  foreach ($p in (Get-GuardedOverlapCandidates)) {
    if ($p) { [void]$req.Add(($p -replace '\\', '/').Trim()) }
  }

  $out = @($req) | Sort-Object
  $script:RunMemo["required"] = @($out)
  return @($out)
}

function Get-ApprovedGuardedPaths {
  if ($script:RunMemo.ContainsKey("approved")) {
    return @($script:RunMemo["approved"])
  }

  $p = Get-LedgerPaths
  if (-not (Test-Path -LiteralPath $p.Resolved)) {
    $script:RunMemo["approved"] = @()
    return @()
  }
  $out = @(
    Get-Content -LiteralPath $p.Resolved -ErrorAction SilentlyContinue |
    ForEach-Object { ($_ -replace '\\', '/').Trim() } |
    Where-Object { $_ }
  ) | Sort-Object -Unique
  $script:RunMemo["approved"] = @($out)
  return @($out)
}

function Get-RemainingRequiredGuardedPaths {
  if ($script:RunMemo.ContainsKey("remaining")) {
    return @($script:RunMemo["remaining"])
  }

  $required = Get-RequiredGuardedPaths
  if (-not $required -or $required.Count -eq 0) {
    $script:RunMemo["remaining"] = @()
    return @()
  }

  $approved = Get-ApprovedGuardedPaths
  $set = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  foreach ($a in $approved) { [void]$set.Add($a) }

  $remaining = New-Object System.Collections.Generic.List[string]
  foreach ($r in $required) {
    if (-not $set.Contains($r)) { $remaining.Add($r) | Out-Null }
  }

  $out = @($remaining | Sort-Object -Unique)
  $script:RunMemo["remaining"] = @($out)
  return @($out)
}

# -----------------------------
# Wildcard resolution
# -----------------------------
function Resolve-WildcardsToTargets {
  param([Parameter(Mandatory)][string[]]$Patterns)

  # Always return an array, never $null
  if ($null -eq $Patterns) { return @() }

  $candidates = @(Get-ConflictedPaths)
  if (-not $candidates -or $candidates.Count -eq 0) {
    # After `git add .` conflicts can vanish; fall back to overlap candidates.
    $candidates = @(Get-GuardedOverlapCandidates)
  }
  if (-not $candidates -or $candidates.Count -eq 0) { return @() }

  # Normalize patterns: drop null/whitespace, normalize slashes, strip leading ./ or .\
  $normPatterns = @(
    foreach ($p in @($Patterns)) {
      if ($null -eq $p) { continue }
      $x = "$p"
      if ($null -eq $x) { continue }
      $x = $x.Trim()
      if (-not $x) { continue }
      $x = $x -replace '^[.][\\/]', ''
      $x = $x -replace '\\', '/'
      if ($x) { $x }
    }
  )
  if (-not $normPatterns -or $normPatterns.Count -eq 0) { return @() }

  $matched = New-Object System.Collections.Generic.List[string]

  foreach ($c in @($candidates)) {
    if ($null -eq $c) { continue }

    $cNorm = "$c"
    if ($null -eq $cNorm) { continue }
    $cNorm = ($cNorm -replace '\\', '/').Trim()
    if (-not $cNorm) { continue }

    foreach ($pat in @($normPatterns)) {
      if ($null -eq $pat) { continue }
      if ($cNorm -like $pat) { $matched.Add($cNorm) | Out-Null; break }
    }
  }

  return @($matched | Sort-Object -Unique)
}



# -----------------------------
# Safety checks
# -----------------------------
function Test-HasConflictMarkers {
  param([Parameter(Mandatory)][string]$Path)

  if (-not (Test-Path -LiteralPath $Path)) { return $false }

  try {
    # Read a limited amount of bytes (fast, prevents "git froze" moments on big files)
    $max = 1024 * 1024   # 1 MB
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
      $len = [Math]::Min($fs.Length, $max)
      $buf = New-Object byte[] $len
      [void]$fs.Read($buf, 0, $len)
    }
    finally { $fs.Dispose() }

    # Convert to ASCII-ish text for scanning (conflict markers are ASCII)
    $text = [System.Text.Encoding]::ASCII.GetString($buf)

    return ($text -match '<<<<<<<|=======|>>>>>>>')
  }
  catch {
    # If we can't read it, assume safe (don't brick the workflow),
    # but you can flip this to $true if you want ultra-strict behavior.
    return $false
  }
}

function Test-IsLfsPointerFile {
  param([Parameter(Mandatory)][string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return $false }
  try {
    $first = Get-Content -LiteralPath $Path -TotalCount 1 -ErrorAction Stop
    return ($first -eq "version https://git-lfs.github.com/spec/v1")
  }
  catch { return $false }
}

# -----------------------------
# Unreal bundle (sidecars)
# -----------------------------
function Get-UnrealBundlePaths {
  param([Parameter(Mandatory)][string[]]$Paths)

  $bundle = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

  foreach ($p in $Paths) {
    if (-not $p) { continue }
    $n = Normalize-RepoPath $p
    if (-not $n) { continue }
    [void]$bundle.Add($n)

    if ($n -match '\.(uasset|umap)$') {
      # Keep behavior aligned with shell helper (${p%.*}) to avoid double-dot sidecars.
      $base = ($n -replace '\.[^./\\]+$', '')
      foreach ($ext in @("uexp", "ubulk", "uptnl")) {
        [void]$bundle.Add("$base.$ext")
      }
    }
  }

  return @($bundle) | Sort-Object
}

# -----------------------------
# Choosing "ours/theirs" + existence checks
# -----------------------------
function Get-CheckoutFlagForHumanSide {
  param([Parameter(Mandatory)][ValidateSet("ours", "theirs")]$Side)

  $ctx = Get-GitContext
  # merge:  ours=>--ours,   theirs=>--theirs
  # rebase: ours=>--theirs, theirs=>--ours  (flip)
  if ($ctx -eq "rebase") {
    return ($Side -eq "ours") ? "--theirs" : "--ours"
  }
  return ($Side -eq "ours") ? "--ours" : "--theirs"
}

function Get-IndexStageForCheckoutFlag {
  param([Parameter(Mandatory)][ValidateSet("--ours", "--theirs")]$Flag)
  return ($Flag -eq "--ours") ? 2 : 3
}

function Test-IndexStageExists {
  param([Parameter(Mandatory)][int]$Stage, [Parameter(Mandatory)][string]$Path)
  & git cat-file -e (":${Stage}:${Path}") 2>$null
  return ($LASTEXITCODE -eq 0)
}

function Get-IndexStagePathsSet {
  param([Parameter(Mandatory)][int]$Stage)

  $set = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  $raw = git ls-files -u 2>$null
  if (-not $raw) { return ,$set }

  foreach ($line in ($raw -split "`r?`n")) {
    if (-not $line) { continue }
    $tab = $line.IndexOf("`t")
    if ($tab -lt 0) { continue }

    $meta = $line.Substring(0, $tab).Trim()
    $path = Normalize-RepoPath ($line.Substring($tab + 1).Trim())
    if (-not $path) { continue }

    $parts = @($meta -split '\s+' | Where-Object { $_ })
    if ($parts.Count -lt 3) { continue }

    $lineStage = 0
    if (-not [int]::TryParse($parts[2], [ref]$lineStage)) { continue }
    if ($lineStage -eq $Stage) { [void]$set.Add($path) }
  }

  return ,$set
}

function Get-CommitRefForHumanSide {
  param([Parameter(Mandatory)][ValidateSet("ours", "theirs")]$Side)

  $ctx = Get-GitContext
  if ($ctx -eq "merge") {
    # Human: ours=HEAD, theirs=MERGE_HEAD
    return ($Side -eq "ours") ? "HEAD" : (Get-MergeHeadSha)
  }

  if ($ctx -eq "rebase") {
    # Human: ours=REBASE_HEAD (commit being applied), theirs=HEAD (onto branch)
    return ($Side -eq "ours") ? (Get-RebaseHeadSha) : "HEAD"
  }

  return $null
}

function Test-PathExistsInRef {
  param([Parameter(Mandatory)][string]$Ref, [Parameter(Mandatory)][string]$Path)
  if (-not $Ref) { return $false }
  & git cat-file -e ("$Ref`:$Path") 2>$null
  return ($LASTEXITCODE -eq 0)
}

function Get-RefExistingPathsSet {
  param(
    [Parameter(Mandatory)][string]$Ref,
    [AllowEmptyCollection()][string[]]$Paths = @()
  )

  $set = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  if (-not $Ref) { return ,$set }

  $normalized = @(
    $Paths |
    ForEach-Object { if ($_ -ne $null) { Normalize-RepoPath $_ } } |
    Where-Object { $_ } |
    Sort-Object -Unique
  )
  if (-not $normalized -or $normalized.Count -eq 0) { return ,$set }

  $raw = @(
    & git ls-tree -r --name-only $Ref -- @normalized 2>$null |
    ForEach-Object { Normalize-RepoPath $_ } |
    Where-Object { $_ }
  )
  foreach ($p in $raw) { [void]$set.Add($p) }
  return ,$set
}

# -----------------------------
# Ledger updates
# -----------------------------
function Update-LedgersAfterResolve {
  param([Parameter(Mandatory)][string[]]$ResolvedPaths)

  $p = Get-LedgerPaths

  $resolved = $ResolvedPaths |
  ForEach-Object { ($_ -replace '\\', '/').Trim() } |
  Where-Object { $_ } |
  Sort-Object -Unique

  if (-not (Test-Path -LiteralPath $p.Resolved)) {
    New-Item -ItemType File -Force -Path $p.Resolved | Out-Null
  }

  $existing = @()
  if (Test-Path -LiteralPath $p.Resolved) {
    $existing = @(
      Get-Content -LiteralPath $p.Resolved -ErrorAction SilentlyContinue |
      ForEach-Object { ($_ -replace '\\', '/').Trim() } |
      Where-Object { $_ }
    )
  }

  $merged = @($existing + $resolved) | Sort-Object -Unique
  Set-Content -LiteralPath $p.Resolved -Value ($merged -join "`n") -Encoding UTF8

  # Index/worktree changed; invalidate derived sets for this invocation.
  Clear-GuardMemo -Keys @("approved", "required", "remaining", "unmerged", "conflicted", "overlap", "guardedOverlap", "ctxId", "rebaseHead", "rebaseOnto")
}

# -----------------------------
# Main resolver
# -----------------------------
function Resolve-BinaryConflicts {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][ValidateSet("ours", "theirs")]$Side,
    [Parameter(Mandatory)][string[]]$Patterns,
    [switch]$VerboseMode
  )

  $ctx = Get-GitContext
  if ($ctx -eq "none") { throw "No merge/rebase in progress." }
  $perfTotal = [System.Diagnostics.Stopwatch]::StartNew()

  Ensure-ContextBoundLedgers
  $perfTargets = [System.Diagnostics.Stopwatch]::StartNew()

  $targets = Resolve-WildcardsToTargets -Patterns $Patterns
  $perfTargets.Stop()
  Write-GuardPerf ("resolve-targets={0}ms matched={1}" -f $perfTargets.ElapsedMilliseconds, @($targets).Count)
  if (-not $targets -or $targets.Count -eq 0) {
    Write-Host "[conflicts] No candidates matched your pattern(s)." -ForegroundColor Yellow
    Write-Host "  Patterns: $($Patterns -join ', ')" -ForegroundColor Yellow

    $cand = Get-GuardedOverlapCandidates
    if ($cand.Count -gt 0) {
      Write-Host "[conflicts] Guarded overlap candidates:" -ForegroundColor Cyan
      $cand | ForEach-Object { Write-Host "  - $_" }
    }
    return
  }

  $perfBundle = [System.Diagnostics.Stopwatch]::StartNew()
  $bundleTargets = Get-UnrealBundlePaths -Paths $targets
  $perfBundle.Stop()
  Write-GuardPerf ("bundle-expand={0}ms bundle={1}" -f $perfBundle.ElapsedMilliseconds, @($bundleTargets).Count)

  # Act only on guarded binaries that are either currently conflicted OR overlap candidates
  $conflictedSet = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  foreach ($p in @(Get-ConflictedPaths)) {
    $n = Normalize-RepoPath $p
    if ($n) { [void]$conflictedSet.Add($n) }
  }

  $overlapSet = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  # Fast path: when true conflicts still exist, overlap computation is not needed.
  # Overlap is only required after conflict stages are gone (e.g. user ran git add .).
  if ($conflictedSet.Count -eq 0) {
    foreach ($p in @(Get-GuardedOverlapCandidates)) {
      $n = Normalize-RepoPath $p
      if ($n) { [void]$overlapSet.Add($n) }
    }
  }

  $guardedMap = Get-GuardedAttrMapForPaths -Paths $bundleTargets
  $filteredTargets = New-Object System.Collections.Generic.List[string]
  foreach ($f in @($bundleTargets)) {
    $n = Normalize-RepoPath $f
    if (-not $n) { continue }
    if (-not $guardedMap.ContainsKey($n) -or -not [bool]$guardedMap[$n]) { continue }
    if ($conflictedSet.Contains($n) -or $overlapSet.Contains($n)) {
      $filteredTargets.Add($n) | Out-Null
    }
  }
  $bundleTargets = @($filteredTargets | Sort-Object -Unique)

  if ($bundleTargets.Count -eq 0) {
    Write-Host "[conflicts] No guarded files to resolve after bundle expansion." -ForegroundColor Yellow
    $perfTotal.Stop()
    Write-GuardPerf ("resolve-total={0}ms (no guarded targets)" -f $perfTotal.ElapsedMilliseconds)
    return
  }

  $flag = Get-CheckoutFlagForHumanSide -Side $Side
  $stage = Get-IndexStageForCheckoutFlag -Flag $flag
  $chosenRef = Get-CommitRefForHumanSide -Side $Side
  if (-not $chosenRef) { throw "Could not determine chosen ref for $Side ($ctx)." }
  $stagePathSet = Get-IndexStagePathsSet -Stage $stage
  $refPathSet = Get-RefExistingPathsSet -Ref $chosenRef -Paths $bundleTargets

  if ($VerboseMode) {
    Write-Host "[conflicts] Context: $ctx  Side: $Side" -ForegroundColor Cyan
    Write-Host "[conflicts] Prefer: git checkout $flag  (fallback: git checkout $chosenRef)" -ForegroundColor DarkGray
    $bundleTargets | ForEach-Object { Write-Host "  - $_" }
  }


  $resolvedNow = New-Object System.Collections.Generic.List[string]

  foreach ($f in $bundleTargets) {
    $used = $false

    # Stage-based (only if stage blob exists)
    if ($stagePathSet.Contains($f)) {
      & git checkout $flag -- $f
      if ($LASTEXITCODE -ne 0) { throw "git checkout $flag failed for $f" }
      & git add -- $f
      if ($LASTEXITCODE -ne 0) { throw "git add failed for $f" }
      $used = $true
    }

    if (-not $used) {
      # Commit-ref fallback (works after 'git add .' when :2/:3 are gone)
      if ($refPathSet.Contains($f)) {
        & git checkout $chosenRef -- $f
        if ($LASTEXITCODE -ne 0) { throw "git checkout $chosenRef failed for $f" }
        & git add -- $f
        if ($LASTEXITCODE -ne 0) { throw "git add failed for $f" }
      }
      else {
        # Only remove if chosen side truly doesn't contain it.
        & git rm -f --ignore-unmatch -- $f | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "git rm failed for $f" }
      }
    }

    $resolvedNow.Add($f) | Out-Null
  }

  $perfTotal.Stop()
  Write-GuardPerf ("resolve-total={0}ms resolved={1}" -f $perfTotal.ElapsedMilliseconds, $resolvedNow.Count)

  # Safety checks (keep your behavior)
  foreach ($f in $resolvedNow) {
    if (Test-HasConflictMarkers $f) {
      throw "Unsafe: conflict markers detected in $f. Re-run helper and choose a side again."
    }
    if (Test-IsLfsPointerFile $f) {
      throw "Unsafe: $f is an LFS pointer file. Run: git lfs pull; git lfs checkout -- $f; then re-run helper."
    }
  }

  Update-LedgersAfterResolve -ResolvedPaths $resolvedNow.ToArray()
  Write-Audit -Action "RESOLVE" -Message ("{0} patterns=[{1}] files=[{2}]" -f $Side, ($Patterns -join ';'), ($resolvedNow -join ';'))

  $resolvedCount = $resolvedNow.Count
  Write-Host ("[conflicts] {0} {1}: resolved {2} file(s)" -f $ctx, $Side, $resolvedCount) -ForegroundColor Green
  $resolvedNow | ForEach-Object { Write-Host "  - $_" -ForegroundColor Gray }

  $required = @(Get-RequiredGuardedPaths)
  $remaining = @(Get-RemainingRequiredGuardedPaths)

  if ($remaining.Count -gt 0) {
    Write-Host ("[conflicts] approvals: MISSING ({0}/{1})" -f ($required.Count - $remaining.Count), $required.Count) -ForegroundColor Yellow
    if ($VerboseMode) {
      $remaining | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    }
  }
  else {
    Write-Host ("[conflicts] approvals: OK ({0}/{1})" -f $required.Count, $required.Count) -ForegroundColor Green
  }

  if ($VerboseMode) {
    Show-ConflictStatus -VerboseMode
  }
}

# -----------------------------
# Status
# -----------------------------
function Show-ConflictStatus {
  param([switch]$VerboseMode)
  Ensure-ContextBoundLedgers
  $ctx = Get-GitContext
  $cid = Get-OperationContextId

  Write-Host "[conflicts] Context: $ctx" -ForegroundColor Cyan
  if ($VerboseMode -and $cid) {
    Write-Host "[conflicts] ContextId: $cid" -ForegroundColor DarkGray
  }

  $unmerged = Get-UnmergedPaths
  Write-Host "[conflicts] Conflicted: $($unmerged.Count)" -ForegroundColor $( if ($unmerged.Count -gt 0) { "Red" } else { "Green" } )
  $unmerged | ForEach-Object { Write-Host "  - $_" }

  $approved = Get-ApprovedGuardedPaths
  Write-Host "[conflicts] Resolved: $($approved.Count)" -ForegroundColor Green
  $approved | ForEach-Object { Write-Host "  - $_" }

  if ($VerboseMode) {
    $required = Get-RequiredGuardedPaths
    if ($required.Count -gt 0) {
      Write-Host "[conflicts] Guarded: $($required.Count)" -ForegroundColor Cyan
      $required | ForEach-Object { Write-Host "  - $_" }
    }
  }

  $remaining = Get-RemainingRequiredGuardedPaths
  if ($remaining.Count -gt 0) {
    Write-Host "[conflicts] Remaining files that require resolution: $($remaining.Count)" -ForegroundColor Yellow
    $remaining | ForEach-Object { Write-Host "  - $_" }
  }
  else {
    Write-Host "[conflicts] All conflicts resolved." -ForegroundColor Green
  }
}


function Show-ConflictSummary {
  Ensure-ContextBoundLedgers

  $ctx = Get-GitContext
  $unmerged = Get-UnmergedPaths
  $required = Get-RequiredGuardedPaths
  $approved = Get-ApprovedGuardedPaths
  $remaining = Get-RemainingRequiredGuardedPaths

  $unmergedCount = @($unmerged).Count
  $requiredCount = @($required).Count
  $approvedCount = @($approved).Count
  $remainingCount = @($remaining).Count

  Write-Host ("[conflicts] Context: {0} | Conflicted: {1} | Guarded: {2} | Resolved: {3} | Remaining: {4}" -f `
      $ctx, $unmergedCount, $requiredCount, $approvedCount, $remainingCount) `
    -ForegroundColor $(if ($remainingCount -gt 0) { "Yellow" } else { "Green" })
}

# -----------------------------
# Abort / Restart
# -----------------------------
function Abort-ConflictOperation {
  $ctx = Get-GitContext
  if ($ctx -eq "merge") {
    Write-Host "[conflicts] Aborting merge..." -ForegroundColor Yellow
    git merge --abort | Out-Host
    Write-Audit -Action "ABORT" -Message "merge"
    return
  }

  if ($ctx -eq "rebase") {
    Write-Host "[conflicts] Aborting rebase..." -ForegroundColor Yellow
    git rebase --abort | Out-Host
    Write-Audit -Action "ABORT" -Message "rebase"
    return
  }

  Write-Host "[conflicts] No merge/rebase in progress." -ForegroundColor Green
}

function Restart-ConflictOperation {
  $gitDir = Get-GitDir
  $ctx = Get-GitContext

  if ($ctx -eq "merge") {
    $mergeHead = Read-GitFile (Join-Path $gitDir "MERGE_HEAD")
    Write-Host "[conflicts] Restarting merge..." -ForegroundColor Yellow
    git merge --abort | Out-Host
    
    if ($mergeHead) {
      Write-Host "[conflicts] Re-running: git merge $mergeHead" -ForegroundColor Cyan
      git merge $mergeHead | Out-Host
      Write-Audit -Action "RESTART" -Message "merge head=$mergeHead"
    }
    else {
      Write-Host "[conflicts] Could not read MERGE_HEAD; run your merge again manually." -ForegroundColor Red
      Write-Audit -Action "RESTART" -Message "merge failed (no MERGE_HEAD)"
    }
    return
  }

  if ($ctx -eq "rebase") {
    # ... existing rebase restart logic ...
    Write-Host "[conflicts] Restarting rebase..." -ForegroundColor Yellow
    git rebase --abort | Out-Host
    # ... rest stays same ...
  }

  Write-Host "[conflicts] No merge/rebase in progress." -ForegroundColor Green
}

function Continue-RebaseWithGuard {
  param(
    [switch]$SkipEditor
  )
  
  $ctx = Get-GitContext
  
  if ($ctx -ne "rebase") {
    Write-Host "[conflicts] No rebase in progress." -ForegroundColor Yellow
    exit 1
  }

  # Enforce guard BEFORE continuing
  $perfGuard = [System.Diagnostics.Stopwatch]::StartNew()
  Ensure-ContextBoundLedgers
  
  $remaining = @(Get-RemainingRequiredGuardedPaths)
  $perfGuard.Stop()
  Write-GuardPerf ("continue-guard-check={0}ms remaining={1}" -f $perfGuard.ElapsedMilliseconds, $remaining.Count)
  
  if ($remaining.Count -gt 0) {
    Write-Host "[conflicts] BLOCKED: Guarded binary file(s) require helper approval before continuing." -ForegroundColor Red
    $remaining | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    Write-Host ""
    Write-Host "Resolve these files first using:" -ForegroundColor Cyan
    Write-Host '  git ours   "<pattern>"' -ForegroundColor Gray
    Write-Host '  git theirs "<pattern>"' -ForegroundColor Gray
    Write-Host ""
    Write-Host "Then run: git conflicts continue" -ForegroundColor Cyan
    Write-Audit -Action "CONTINUE_BLOCKED" -Message "rebase continue blocked: $($remaining.Count) files require approval"
    exit 1
  }

  # Guard passed - continue rebase
  Write-Host "[conflicts] Guard passed - continuing rebase..." -ForegroundColor Green
  Write-Audit -Action "CONTINUE" -Message "rebase continue allowed (all guarded files approved)"
  
  $perfContinue = [System.Diagnostics.Stopwatch]::StartNew()
  if ($SkipEditor) {
    # Non-interactive mode: skip commit message editor
    git -c core.editor=true -c sequence.editor=true rebase --continue | Out-Host
  }
  else {
    # Normal mode: allow commit message editing
    git rebase --continue | Out-Host
  }
  
  $exitCode = $LASTEXITCODE
  $perfContinue.Stop()
  Write-GuardPerf ("continue-rebase={0}ms exit={1}" -f $perfContinue.ElapsedMilliseconds, $exitCode)
  
  if ($exitCode -eq 0) {
    Write-Host "[conflicts] Rebase continued successfully." -ForegroundColor Green
  }
  else {
    Write-Host "[conflicts] Rebase continue failed (exit code: $exitCode)." -ForegroundColor Red
  }
  
  exit $exitCode
}

# -----------------------------
# Back-compat stub
# -----------------------------
function Sync-BinaryConflictLock {
  # Old behavior returned lock contents; now return the required set
  return @(Get-RequiredGuardedPaths)
}
