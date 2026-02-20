# Scripts/Tests/BinaryGuard-Test-Functions.ps1
# Shared helpers for Binary Guard tests.

# ----------------------------
# Resolve real executables (prevents wrapper/alias noise like RemoteException)
# ----------------------------
function Resolve-ExePath([string]$name) {
  try {
    $cmd = Get-Command $name -ErrorAction Stop
    if ($cmd.CommandType -eq [System.Management.Automation.CommandTypes]::Application) { return $cmd.Source }
    $app = Get-Command $name -CommandType Application -ErrorAction Stop
    return $app.Source
  }
  catch { return $name }
}

function Initialize-BinaryGuardTestState {
  param(
    [switch]$NoCleanup,
    [string]$ReturnBranch
  )
  $script:GitExe = Resolve-ExePath "git"
  $script:PwshExe = Resolve-ExePath "pwsh"

  # ----------------------------
  # Repo + Results Paths
  # ----------------------------
  $script:repoRoot = ( & $script:GitExe rev-parse --show-toplevel 2>$null ).Trim()
  if (-not $script:repoRoot) { throw "Not inside a git repository." }
  Set-Location $script:repoRoot

  $resultsDir = Join-Path $script:repoRoot "Scripts\Tests\Test-BinaryGuard-FixesResults"
  New-Item -ItemType Directory -Force $resultsDir | Out-Null
  $stamp = (Get-Date).ToString("yyyyMMdd-HHmmss")
  $script:logPath = Join-Path $resultsDir "BinaryGuardTest-$stamp.log"

  # ----------------------------
  # Non-interactive git for automation (prevents editor prompts)
  # ----------------------------
  $script:PrevGitEnv = @{
    GIT_EDITOR          = $env:GIT_EDITOR
    GIT_SEQUENCE_EDITOR = $env:GIT_SEQUENCE_EDITOR
    EDITOR              = $env:EDITOR
    VISUAL              = $env:VISUAL
  }
  $env:GIT_EDITOR = "true"
  $env:GIT_SEQUENCE_EDITOR = "true"
  $env:EDITOR = "true"
  $env:VISUAL = "true"

  # ----------------------------
  # Replay / Debug-on-fail
  # ----------------------------
  $script:PrevStepReplayIndex = 0
  $script:CurrStepReplayIndex = 0
  $script:CurrStepName = ""
  $script:LastCmdDisplay = ""
  $script:BaselineRef = (& $script:GitExe rev-parse HEAD 2>$null).Trim()
  $script:LastReplayIndex = -1
  $script:IsReplaying = $false
  $script:IsDebugRewind = $false
  $script:CleanupRan = $false
  $script:NoCleanup = $NoCleanup.IsPresent
  $script:AbortAll = $false
  $script:WasCancelled = $false
  $script:Results = New-Object System.Collections.Generic.List[object]
  $script:TestBranches = New-Object System.Collections.Generic.HashSet[string]
  $script:Replay = New-Object System.Collections.Generic.List[object]
  # ----------------------------
  # Dynamic planned tests + unique names
  # ----------------------------
  $script:PlannedSet = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  $script:RanSet = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
  $script:NameCounts = @{}

  # ----------------------------
  # Ctrl+C cleanup hook
  # ----------------------------
  $script:ReturnBranch = $ReturnBranch
  $script:CancelEventSub = Register-ObjectEvent -InputObject ([Console]) -EventName CancelKeyPress -Action {
    try {
      $script:WasCancelled = $true
      $eventArgs.Cancel = $true
      if (Get-Command Cleanup -ErrorAction SilentlyContinue && -not $script:NoCleanup) { Cleanup }
    }
    catch { }
  }
}

function New-TestName([string]$base) {
  $base = ($base ?? "").Trim()
  if (-not $base) { $base = "Unnamed test" }

  if (-not $script:NameCounts.ContainsKey($base)) { $script:NameCounts[$base] = 0 }
  $script:NameCounts[$base]++

  $n = $script:NameCounts[$base]
  $final = if ($n -eq 1) { $base } else { "{0} (run {1})" -f $base, $n }

  [void]$script:PlannedSet.Add($final)
  return $final
}

function Set-Ran([string]$name) { if ($name) { [void]$script:RanSet.Add($name) } }

# ----------------------------
# Logging helpers
# ----------------------------
function Log([string]$msg, [ConsoleColor]$color = [ConsoleColor]::DarkGray) {
  Add-Content -Path $logPath -Value $msg
  Write-Host $msg -ForegroundColor $color
}

function Pass([string]$name, [string]$detail) {
  Set-Ran $name
  $script:Results.Add([pscustomobject]@{ Test = $name; Status = "PASS"; Detail = $detail }) | Out-Null
  Log "[PASS] $name - $detail" Green
}

function Warn([string]$name, [string]$detail) {
  Set-Ran $name
  $script:Results.Add([pscustomobject]@{ Test = $name; Status = "WARN"; Detail = $detail }) | Out-Null
  Log "[WARN] $name - $detail" Yellow
}

function Skip([string]$name, [string]$detail) {
  Set-Ran $name
  $script:Results.Add([pscustomobject]@{ Test = $name; Status = "SKIP"; Detail = $detail }) | Out-Null
  Log "[SKIP] $name - $detail" DarkGray
}

function Step([string]$msg) {
  if ($script:IsReplaying -or $script:IsDebugRewind) {
    $script:CurrStepName = $msg
  }
  else {
    $script:PrevStepReplayIndex = [int]$script:CurrStepReplayIndex
    $script:CurrStepReplayIndex = [int]$script:Replay.Count
    $script:CurrStepName = $msg
  }

  Log "" Cyan
  Log "============================================================" Cyan
  Log $msg Cyan
  Log "============================================================" Cyan
}

function Get-RecordedGitAddDot([Nullable[int]]$idx) {
  if (-not $idx.HasValue) { return $false }
  $i = [int]$idx.Value
  if ($i -lt 0 -or $i -ge $script:Replay.Count) { return $false }
  $e = $script:Replay[$i]
  if ($e.Kind -ne "RunArgs") { return $false }
  if ($e.Exe -ne "git") { return $false }
  $a = @($e.Args)
  return ($a.Count -ge 2 -and $a[0] -eq "add" -and $a[1] -eq ".")
}

function Move-ToIndex([int]$count) {
  if ($count -le 0) { return }
  if ($count -gt $script:Replay.Count) { $count = $script:Replay.Count }

  $script:IsReplaying = $true
  try {
    for ($i = 0; $i -lt $count; $i++) {
      Set-Location $repoRoot
      $entry = $script:Replay[$i]
      $script:LastCmdDisplay = $entry.Display

      if ($entry.Kind -eq "RunArgs") {
        $exe = $entry.Exe
        $_args = @($entry.Args)

        # Avoid replaying `git add .` during debug rewind (stages conflicts away)
        if ($exe -eq "git" -and $_args.Count -ge 1 -and $_args[0] -eq "add") {
          if ($_args -contains "." -or $_args -contains "-A" -or $_args -contains "--all") { continue }
        }

        # During replay, don't let hooks break reconstruction
        if ($exe -eq "git" -and $_args.Count -ge 1 -and $_args[0] -eq "commit") {
          if (-not ($_args -contains "--no-verify")) {
            $_args = @("commit", "--no-verify") + $_args[1..($_args.Count - 1)]
          }
        }

        & $exe @_args | Out-Null
        if ($LASTEXITCODE -ne 0) {
          Log "[DEBUG] Command failed at replay index ${i}: $($entry.Display)" Yellow
          Log "[DEBUG] Continuing replay..." Yellow
        }
      }
      elseif ($entry.Kind -eq "RunCmd") {
        Invoke-Expression $entry.Cmd | Out-Null
        if ($LASTEXITCODE -ne 0) {
          Log "[DEBUG] Command failed at replay index ${i}: $($entry.Display)" Yellow
          Log "[DEBUG] Continuing replay..." Yellow
        }
      }
    }
  }
  finally { $script:IsReplaying = $false }
}

function Invoke-AbortInProgress {
  & git merge --abort 2>$null | Out-Null
  & git rebase --abort 2>$null | Out-Null
}

function Reset-ToBaseline {
  Invoke-AbortInProgress
  RunArgs -NoReplay git @("checkout", "--detach", $script:BaselineRef) | Out-Null
  RunArgs -NoReplay git @("reset", "--hard") | Out-Null
  RunArgs -NoReplay git @("clean", "-fd") | Out-Null
  Remove-Item -Recurse -Force (Join-Path $repoRoot "Content\Test") -ErrorAction SilentlyContinue
}

function Remove-TestBranches {
  foreach ($b in ($script:TestBranches | Sort-Object)) {
    $cur = (& $script:GitExe rev-parse --abbrev-ref HEAD 2>$null).Trim()
    if ($cur -eq $b) {
      RunArgs -NoReplay git @("checkout", "--detach", $script:BaselineRef) | Out-Null
    }
    RunArgs -NoReplay git @("branch", "-D", $b) | Out-Null
  }
}

function Enter-DebugOnFail([string]$testName, [string]$detail, [Nullable[int]]$FailReplayIndex = $null) {
  if (-not $PauseOnFail) { return }

  Log "" Yellow
  Log "==================== DEBUG-ON-FAIL ====================" Yellow
  Log "Test failed: $testName" Yellow
  Log "Detail    : $detail" Yellow
  Log "Step      : $script:CurrStepName" Yellow
  Log "" Yellow
  Log "Rewinding to baseline, rebuilding to failing point, then pausing..." Yellow

  $fallback = [Math]::Max(0, [int]$script:Replay.Count)
  $stopAt = if ($FailReplayIndex.HasValue) { [int]$FailReplayIndex.Value } else { $fallback }

  if (Get-RecordedGitAddDot $FailReplayIndex) { $stopAt = [Math]::Max(0, $stopAt) }
  $prevBoundary = [int]($script:PrevStepReplayIndex)
  $stopAt = [Math]::Max($prevBoundary, $stopAt)

  $script:IsDebugRewind = $true
  try {
    Reset-ToBaseline
    Remove-TestBranches
    Move-ToIndex $stopAt
  }
  finally { $script:IsDebugRewind = $false }

  Log "" Yellow
  Log "You are now positioned just BEFORE the failing moment." Yellow
  if ($script:LastCmdDisplay) { Log "Last replayed command: $script:LastCmdDisplay" Yellow }
  Log "Helpful: git status, git diff, git diff --cached, git lfs status" Yellow
  Log "=======================================================" Yellow
  Read-Host "Paused for manual debug. Press Enter to continue (or Ctrl+C to stop)" | Out-Null
}

function Fail([string]$name, [string]$detail, [Nullable[int]]$FailReplayIndex = $null) {
  Set-Ran $name
  $script:Results.Add([pscustomobject]@{ Test = $name; Status = "FAIL"; Detail = $detail }) | Out-Null
  Log "[FAIL] $name - $detail" Red

  Enter-DebugOnFail -testName $name -detail $detail -FailReplayIndex $FailReplayIndex

  if ($FailFast) {
    $script:AbortAll = $true
    throw "FAILFAST"
  }
}

# ----------------------------
# Process helpers
# ----------------------------
function Format-Args([object[]]$ArgsList) {
  if ($ArgsList.Count -eq 1 -and $ArgsList[0] -is [System.Array]) { return @($ArgsList[0]) }
  return @($ArgsList)
}

function RunArgs {
  param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$ArgsList,
    [switch]$NoReplay
  )

  $ArgsList = Format-Args $ArgsList
  $ArgsList = @($ArgsList | ForEach-Object { "$_" })

  $exePath = $Exe
  if ($Exe -eq "git") { $exePath = $script:GitExe }
  if ($Exe -eq "pwsh") { $exePath = $script:PwshExe }

  $display = ($ArgsList | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join " "
  Log ">> $Exe $display" DarkGray

  if (-not $NoReplay) {
    $script:Replay.Add([pscustomobject]@{
        Kind    = "RunArgs"
        Exe     = $Exe
        Args    = @($ArgsList)
        Display = ">> $Exe $display"
      }) | Out-Null
    $script:LastReplayIndex = $script:Replay.Count - 1
  }

  $output = @()
  $code = 0
  try {
    $output = & $exePath @ArgsList 2>&1
    $code = [int]$LASTEXITCODE
  }
  catch {
    Log "WARNING: threw: $Exe $display" Yellow
    Log "$_" Yellow
    $code = 1
  }

  foreach ($line in @($output)) {
    $s = $null
    if ($line -is [System.Management.Automation.ErrorRecord]) {
      $s = $line.Exception.Message
      if (-not $s) { $s = $line.ToString() }
    }
    else {
      $s = [string]$line
    }

    $s = ($s ?? "").TrimEnd()
    if ($s -and $s -ne "System.Management.Automation.RemoteException") { Log "   $s" DarkGray }
  }

  # Critical test invariant: helper aliases must not fail silently.
  if ($code -ne 0 -and $Exe -eq "git" -and $ArgsList.Count -gt 0) {
    $sub = ("$($ArgsList[0])").ToLowerInvariant()
    if ($sub -eq "ours" -or $sub -eq "theirs") {
      $tn = New-TestName "Critical helper alias succeeds: git $sub"
      Fail $tn "git $sub failed (exit=$code)" ([Nullable[int]]$script:Replay.Count)
    }
  }

  return $code
}

function RunArgsCapture {
  param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(ValueFromRemainingArguments = $true)][object[]]$ArgsList,
    [switch]$NoReplay
  )

  $ArgsList = Format-Args $ArgsList
  $ArgsList = @($ArgsList | ForEach-Object { "$_" })

  $display = ($ArgsList | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join " "
  Log ">> $Exe $display" DarkGray

  if (-not $NoReplay) {
    $script:Replay.Add([pscustomobject]@{
        Kind    = "RunArgs"
        Exe     = $Exe
        Args    = @($ArgsList)
        Display = ">> $Exe $display"
      }) | Out-Null
    $script:LastReplayIndex = $script:Replay.Count - 1
  }

  $output = @()
  $code = 0
  try {
    $exePath = $Exe
    if ($Exe -eq "git") { $exePath = $script:GitExe }
    if ($Exe -eq "pwsh") { $exePath = $script:PwshExe }
    $output = & $exePath @ArgsList 2>&1
    $code = [int]$LASTEXITCODE
  }
  catch {
    $output = @("WARNING: threw: $Exe $display", "$_")
    $code = 1
  }

  foreach ($line in @($output)) {
    $s = $null
    if ($line -is [System.Management.Automation.ErrorRecord]) {
      $s = $line.Exception.Message
      if (-not $s) { $s = $line.ToString() }
    }
    else {
      $s = [string]$line
    }

    $s = ($s ?? "").TrimEnd()
    if ($s -and $s -ne "System.Management.Automation.RemoteException") { Log "   $s" DarkGray }
  }

  [pscustomobject]@{
    Code   = $code
    Output = @($output)
  }
}

function RunCmd {
  param(
    [Parameter(Mandatory)][string]$cmd,
    [switch]$NoReplay
  )

  Log ">> $cmd" DarkGray

  if (-not $NoReplay) {
    $script:Replay.Add([pscustomobject]@{
        Kind    = "RunCmd"
        Cmd     = $cmd
        Display = ">> $cmd"
      }) | Out-Null
    $script:LastReplayIndex = $script:Replay.Count - 1
  }

  $output = @()
  $code = 0
  try {
    $output = Invoke-Expression $cmd 2>&1
    $code = if ($LASTEXITCODE -ne $null) { [int]$LASTEXITCODE } else { 0 }
  }
  catch {
    Log "WARNING: threw: $cmd" Yellow
    Log "$_" Yellow
    $code = 1
  }

  foreach ($line in @($output)) {
    $s = $null
    if ($line -is [System.Management.Automation.ErrorRecord]) {
      $s = $line.Exception.Message
      if (-not $s) { $s = $line.ToString() }
    }
    else {
      $s = [string]$line
    }

    $s = ($s ?? "").TrimEnd()
    if ($s -and $s -ne "System.Management.Automation.RemoteException") { Log "   $s" DarkGray }
  }

  return $code
}

# ----------------------------
# Test infra
# ----------------------------
function Confirm-SystemDrawing {
  try { Add-Type -AssemblyName System.Drawing -ErrorAction Stop } catch { throw "System.Drawing could not be loaded." }
}

function Write-LabeledPng {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Label,
    [ValidateSet("White", "Red", "Blue", "Green", "Yellow", "Black")][string]$Bg = "White",
    [int]$Size = 512
  )
  Confirm-SystemDrawing
  $bmp = New-Object System.Drawing.Bitmap $Size, $Size
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::$Bg)

  $fontBig = New-Object System.Drawing.Font "Arial", ([float]([Math]::Max(24, [Math]::Floor($Size / 5))))
  $fontSm = New-Object System.Drawing.Font "Arial", ([float]([Math]::Max(12, [Math]::Floor($Size / 18))))

  $brushMain = [System.Drawing.Brushes]::White
  $brushSub = if ($Bg -eq "White" -or $Bg -eq "Yellow") { [System.Drawing.Brushes]::Black } else { [System.Drawing.Brushes]::White }

  $g.DrawString("GuardTest", $fontSm, $brushSub, 24, 24)
  $g.DrawString($Label, $fontBig, $brushMain, 24, [int]([Math]::Floor($Size * 0.35)))

  $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}

# Creates a LARGE, hard-to-compress PNG (random noise) so it’s reliably multi-megabytes for Unreal texture import tests.
function Write-NoisyPng {
  param(
    [Parameter(Mandatory)][string]$Path,
    [int]$Width = 4096,
    [int]$Height = 4096,
    [int]$Seed = 1337
  )

  Confirm-SystemDrawing
  Add-Type -AssemblyName System.Runtime.InteropServices | Out-Null

  $pf = [System.Drawing.Imaging.PixelFormat]::Format24bppRgb
  $bmp = New-Object System.Drawing.Bitmap -ArgumentList @($Width, $Height, $pf)
  $rect = New-Object System.Drawing.Rectangle 0, 0, $Width, $Height
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)

  try {
    $bytes = [Math]::Abs($data.Stride) * $Height
    $buf = New-Object byte[] $bytes
    $rng = [System.Random]::new($Seed)
    $rng.NextBytes($buf)

    [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $bytes)
  }
  finally {
    $bmp.UnlockBits($data)
  }

  # Add a quick label overlay so humans can visually distinguish versions if opened
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  try {
    $font = New-Object System.Drawing.Font "Arial", 96
    $g.DrawString("NOISY", $font, [System.Drawing.Brushes]::White, 40, 40)
  }
  finally { $g.Dispose() }

  $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}

function OpenIfVerify([string]$path) {
  if (-not $VerifyPngs) { return }
  if (Test-Path $path) { Start-Process $path | Out-Null }
}

function Confirm-BranchExists([string]$name) {
  & $script:GitExe show-ref --verify --quiet "refs/heads/$name"
  return ($LASTEXITCODE -eq 0)
}

function Invoke-SafeCheckoutReturnBranch {
  if (Confirm-BranchExists $script:ReturnBranch) { RunArgs git @("checkout", $script:ReturnBranch) | Out-Null; return }
  RunArgs git @("checkout", "main") | Out-Null
}

function Add-TestBranch([string]$b) { [void]$script:TestBranches.Add($b) }

function Cleanup {
  if ($script:CleanupRan) { return }
  $script:CleanupRan = $true

  Write-Host ""
  Write-Host "============================================================" -ForegroundColor DarkGray
  Write-Host "Cleanup" -ForegroundColor DarkGray
  Write-Host "============================================================" -ForegroundColor DarkGray

  try { Invoke-AbortInProgress } catch { }

  try { RunArgs git @("reset", "--hard") | Out-Null } catch { }
  try { RunArgs git @("clean", "-fd") | Out-Null } catch { }

  try { Invoke-SafeCheckoutReturnBranch } catch { }

  if ($script:TestBranches -and $script:TestBranches.Count -gt 0) {
    foreach ($b in ($script:TestBranches | Sort-Object)) {
      try { RunCmd "git branch -D -- $b" | Out-Null } catch { }
    }
  }
  else {
    try { RunCmd "git deltest" } catch {}
  }

  try { Remove-Item -Recurse -Force (Join-Path $repoRoot "Content\Test") -ErrorAction SilentlyContinue } catch { }

  if ($script:PrevGitEnv) {
    foreach ($k in $script:PrevGitEnv.Keys) {
      $v = $script:PrevGitEnv[$k]
      if ($null -eq $v) {
        try { Remove-Item -LiteralPath ("Env:{0}" -f $k) -ErrorAction SilentlyContinue } catch { }
      }
      else {
        try { Set-Item -LiteralPath ("Env:{0}" -f $k) -Value $v -ErrorAction SilentlyContinue } catch { }
      }
    }
  }

  Write-Host "[cleanup] done" -ForegroundColor DarkGray
}

# ----------------------------
# Assertions (names MUST be unique per callsite)
# ----------------------------
function Assert-True([bool]$condition, [string]$testName) {
  if ($condition) { Pass $testName "condition is true"; return $true }
  Fail $testName "condition is false" ([Nullable[int]]$script:Replay.Count)
  return $false
}

function Assert-Conflicts([string]$testName, [int]$expectedAtLeast = 1) {
  $u = & $script:GitExe diff --name-only --diff-filter=U 2>$null
  $u = @($u) | ForEach-Object { $_.Trim() } | Where-Object { $_ }
  if ($u.Count -ge $expectedAtLeast) { Pass $testName "conflicts present ($($u.Count))"; return $true }
  Fail $testName "expected conflicts but found none" ([Nullable[int]]$script:Replay.Count)
  return $false
}

function Assert-NoConflicts([string]$testName) {
  $u = & $script:GitExe diff --name-only --diff-filter=U 2>$null
  $u = @($u) | ForEach-Object { $_.Trim() } | Where-Object { $_ }
  if ($u.Count -eq 0) { Pass $testName "no conflicts"; return $true }
  Fail $testName "still has conflicts ($($u.Count))" ([Nullable[int]]$script:Replay.Count)
  return $false
}

function Assert-ConflictsIncludeAll {
  param(
    [Parameter(Mandatory)][string]$testName,
    [Parameter(Mandatory)][string[]]$relPaths
  )

  $u = & $script:GitExe diff --name-only --diff-filter=U 2>$null
  $u = @($u) | ForEach-Object { $_.Trim() } | Where-Object { $_ }

  $missing = @()
  foreach ($p in $relPaths) {
    if (-not ($u -contains $p)) { $missing += $p }
  }

  if ($missing.Count -eq 0) {
    Pass $testName "conflicts include all expected files ($($relPaths.Count))"
    return $true
  }

  Fail $testName ("missing conflicted files: {0} (had: {1})" -f ($missing -join ", "), ($u -join ", ")) ([Nullable[int]]$script:Replay.Count)
  return $false
}

function Assert-CommitBlocked([string]$testName) {
  $failIndex = [int]$script:Replay.Count
  $code = RunArgs -NoReplay git @("commit", "-m", "should be blocked")
  if ($code -ne 0) { Pass $testName "commit blocked (exit=$code)"; return $true }
  Fail $testName "commit unexpectedly succeeded" $failIndex
  return $false
}

function Assert-CommitSucceeds([string]$testName, [string]$msg) {
  $failIndex = [int]$script:Replay.Count
  $code = RunArgs git @("commit", "-m", $msg)
  if ($code -eq 0) { Pass $testName "commit succeeded"; return $true }
  Fail $testName "commit failed (exit=$code)" $failIndex
  return $false
}

function Assert-RebaseContinueSucceeds([string]$testName) {
  $failIndex = [int]$script:Replay.Count
  $code = RunArgs git @("-c", "core.editor=true", "-c", "sequence.editor=true", "rebase", "--continue")
  if ($code -eq 0) { Pass $testName "rebase --continue succeeded"; return $true }
  Fail $testName "rebase --continue failed (exit=$code)" $failIndex
  return $false
}

function Assert-ConflictsContinueSucceedsNoGuardWarning([string]$testName) {
  $failIndex = [int]$script:Replay.Count
  
  # Use git alias directly
  $res = RunArgsCapture git @("conflicts", "continue", "--skip-editor")
  
  if ($res.Code -ne 0) {
    Fail $testName "conflicts continue failed (exit=$($res.Code))" $failIndex
    return $false
  }

  $joined = ($res.Output -join "`n")
  $bad = @(
    'BLOCKED: Guarded binary file\(s\) require helper approval',
    'Commit blocked: guarded binary file\(s\) require helper approval',
    'Commit blocked: unsafe guarded binary file',
    'Resolve these files first using'
  )

  foreach ($pat in $bad) {
    if ($joined -match $pat) {
      Fail $testName "unexpected Binary Guard messaging during conflicts continue (matched: $pat)" $failIndex
      return $false
    }
  }

  Pass $testName "conflicts continue succeeded (no Binary Guard warnings)"
  return $true
}

function Assert-RebaseContinueBlockedByGuard([string]$testName) {
  $failIndex = [int]$script:Replay.Count
  $res = RunArgsCapture -NoReplay git @("rebase", "--continue")
  if ($res.Code -eq 0) {
    Fail $testName "rebase --continue unexpectedly succeeded" $failIndex
    return $false
  }

  $joined = ($res.Output -join "`n")
  $good = @(
    'Guarded binary conflicts detected',
    'Commit blocked: guarded binary file',
    'Resolve guarded binary conflicts ONLY with'
  )

  foreach ($pat in $good) {
    if ($joined -match $pat) {
      Pass $testName "rebase --continue blocked by Binary Guard (matched: $pat)"
      return $true
    }
  }

  Fail $testName "rebase --continue failed, but did not show Binary Guard messaging (exit=$($res.Code))" $failIndex
  return $false
}

function Assert-RebaseContinueBlockedByGuardOrDetectBypass([string]$testName) {
  $failIndex = [int]$script:Replay.Count

  $headBefore = (& $script:GitExe rev-parse HEAD).Trim()
  $res = RunArgsCapture -NoReplay git @("rebase", "--continue")
  $joined = ($res.Output -join "`n")

  $guardPatterns = @(
    'Guarded binary conflicts detected',
    'Commit blocked: guarded binary file',
    'Resolve guarded binary conflicts ONLY with'
  )

  foreach ($pat in $guardPatterns) {
    if ($joined -match $pat) {
      Pass $testName "rebase --continue blocked by Binary Guard (matched: $pat)"
      return $true
    }
  }

  # If it *didn't* show guard messaging, check if it advanced anyway (the bypass bug)
  $headAfter = (& $script:GitExe rev-parse HEAD).Trim()

  $advancedSignals = @(
    'Rebasing \(\d+/\d+\)',
    'could not apply',
    'CONFLICT \(content\): Merge conflict',
    'Cannot merge binary files'
  )

  $looksLikeAdvanced = $false
  foreach ($pat in $advancedSignals) {
    if ($joined -match $pat) { $looksLikeAdvanced = $true; break }
  }

  if ($res.Code -ne 0 -and $looksLikeAdvanced) {
    Fail $testName "BUG DETECTED: rebase --continue advanced instead of being blocked by guard (exit=$($res.Code))" $failIndex
    return $false
  }

  Fail $testName "rebase --continue failed without Binary Guard messaging, and did not clearly show advance (exit=$($res.Code))" $failIndex
  return $false
}


function Is-LfsPointer([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $false }
  try {
    $first = Get-Content -LiteralPath $path -TotalCount 1 -ErrorAction Stop
    return ($first -eq "version https://git-lfs.github.com/spec/v1")
  }
  catch { return $false }
}

function Assert-NotPointer([string]$testName, [string]$absPath) {
  if (Is-LfsPointer $absPath) {
    Fail $testName "file is an LFS pointer (corrupt working tree)" ([Nullable[int]]$script:Replay.Count)
    return $false
  }
  Pass $testName "file is not an LFS pointer"
  return $true
}

function Get-Hashes {
  param([Parameter(Mandatory)][string[]]$RelPaths)
  $map = @{}
  foreach ($rp in $RelPaths) {
    $abs = Join-Path $repoRoot $rp
    if (-not (Test-Path $abs)) { throw "Missing file for hash: $rp" }
    if (Is-LfsPointer $abs) { throw "File is LFS pointer in working tree: $rp" }
    $map[$rp] = (Get-FileHash -Algorithm SHA256 -LiteralPath $abs).Hash
  }
  return $map
}

function Assert-HashesEqual {
  param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][hashtable]$Expected,
    [Parameter(Mandatory)][string[]]$RelPaths
  )

  $okAll = $true
  foreach ($rp in $RelPaths) {
    $abs = Join-Path $repoRoot $rp
    if (-not (Test-Path $abs)) { $okAll = $false; Log "[FAIL] $Name missing: $rp" Red; continue }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $abs).Hash
    if ($actual -ne $Expected[$rp]) {
      $okAll = $false
      Log "[FAIL] $Name hash mismatch: $rp" Red
      Log "       expected=$($Expected[$rp])" DarkGray
      Log "       actual  =$actual" DarkGray
    }
  }

  if ($okAll) { Pass $Name "hashes match expected set" } else { Fail $Name "one or more files mismatched" ([Nullable[int]]$script:Replay.Count) }
  return $okAll
}

# ----------------------------
# Text helpers (LF deterministic)
# ----------------------------
function Write-TextFile([string]$relPath, [string]$content) {
  $abs = Join-Path $repoRoot $relPath
  $dir = Split-Path $abs -Parent
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

  $lf = ($content -replace "`r?`n", "`n")
  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($abs, $lf, $utf8NoBom)
}

function Resolve-TextConflict-Ours([string]$relPath) {
  RunArgs git @("checkout", "--ours", "--", $relPath) | Out-Null
  RunArgs git @("add", "--", $relPath) | Out-Null
}

function Resolve-TextConflict-Theirs([string]$relPath) {
  RunArgs git @("checkout", "--theirs", "--", $relPath) | Out-Null
  RunArgs git @("add", "--", $relPath) | Out-Null
}

# ----------------------------
# Synthetic sidecar bundle helpers (fully automated)
# ----------------------------
function Write-DeterministicBinaryBlob {
  param(
    [Parameter(Mandatory)][string]$RelPath,
    [Parameter(Mandatory)][string]$Tag,
    [int]$SizeBytes = 8192
  )

  $abs = Join-Path $repoRoot $RelPath
  $dir = Split-Path $abs -Parent
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

  $seed = [System.Text.Encoding]::UTF8.GetBytes($Tag)
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $digest = $sha.ComputeHash($seed)
  }
  finally {
    $sha.Dispose()
  }

  $buf = New-Object byte[] $SizeBytes
  for ($i = 0; $i -lt $SizeBytes; $i++) {
    $buf[$i] = [byte](($digest[$i % $digest.Length] + ($i % 251)) -band 0xFF)
  }

  # Make content unambiguously binary.
  if ($SizeBytes -ge 3) {
    $buf[0] = 0
    $buf[1] = 255
    $buf[2] = 13
  }

  [System.IO.File]::WriteAllBytes($abs, $buf)
}

function Write-SyntheticSidecarBundle {
  param(
    [Parameter(Mandatory)][string]$StemRel,
    [Parameter(Mandatory)][string]$Tag
  )

  Write-DeterministicBinaryBlob -RelPath "$StemRel.uasset" -Tag "$Tag|uasset" -SizeBytes 16384
  Write-DeterministicBinaryBlob -RelPath "$StemRel.uexp" -Tag "$Tag|uexp" -SizeBytes 8192
  Write-DeterministicBinaryBlob -RelPath "$StemRel.ubulk" -Tag "$Tag|ubulk" -SizeBytes 24576
}

# ----------------------------
# Unreal manual + sidecar helpers
# ----------------------------
function Test-UnrealEditorRunning {
  try {
    $p = Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue
    return ($null -ne $p)
  }
  catch { return $false }
}

function Wait-ForUnrealEditorClosed([string]$reason) {
  while (Test-UnrealEditorRunning) {
    Log "" Yellow
    Log "Unreal Editor is still running." Yellow
    Log "Reason: $reason" Yellow
    Log "Close Unreal Editor completely, then press Enter to continue." Yellow
    Read-Host "Press Enter after closing Unreal Editor" | Out-Null
  }
}

function Pause-ForUnrealAssetCreation {
  param(
    [Parameter(Mandatory)][string]$Title,
    [Parameter(Mandatory)][string[]]$Instructions,
    [Parameter(Mandatory)][string]$FolderRel,
    [int]$MinUasset = 1
  )

  Step $Title
  Wait-ForUnrealEditorClosed "This test needs Unreal closed before starting (avoid locks / nondeterministic writes)."

  $folderAbs = Join-Path $repoRoot $FolderRel
  New-Item -ItemType Directory -Force $folderAbs | Out-Null

  $ueVirtualPath = "/Game/" + (($FolderRel -replace '^Content/', '') -replace '\\', '/' )

  Log "" Cyan
  Log "==================== MANUAL UNREAL STEP ====================" Cyan
  Log "You must create/modify Unreal assets now." Cyan
  Log "Content folder path on disk : $FolderRel" Cyan
  Log "Content Browser path in UE  : $ueVirtualPath" Cyan
  Log "" Cyan
  foreach ($line in $Instructions) { Log " - $line" Cyan }
  Log "" Cyan
  Log "IMPORTANT: after you finish, CLOSE Unreal Editor completely (File -> Exit)." Cyan
  Log "============================================================" Cyan

  Read-Host "After you created the assets AND closed Unreal Editor, press Enter to continue" | Out-Null
  Wait-ForUnrealEditorClosed "We can't proceed until Unreal is closed."

  $ueFiles = Get-ChildItem -LiteralPath $folderAbs -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Extension -in @(".uasset", ".umap", ".uexp", ".ubulk", ".uptnl", ".ucas", ".utoc") } |
  ForEach-Object { $_.FullName }

  $uassetCount = ($ueFiles | Where-Object { $_ -like "*.uasset" -or $_ -like "*.umap" }).Count
  $t1 = New-TestName "Unreal manual step produced at least $MinUasset primary Unreal asset file(s) in $FolderRel"
  if ($uassetCount -ge $MinUasset) { Pass $t1 "found primary UE assets: $uassetCount" }
  else { Fail $t1 "expected >= $MinUasset primary UE assets, found $uassetCount"; return @() }

  $t2 = New-TestName "Unreal manual step detected UE files in $FolderRel"
  if ($ueFiles.Count -ge $MinUasset) { Pass $t2 "found UE files total: $($ueFiles.Count)" }
  else { Fail $t2 "expected UE files but found none"; return @() }

  $sidecarFiles = @(
    $ueFiles | Where-Object {
      $_.EndsWith(".uexp") -or
      $_.EndsWith(".ubulk") -or
      $_.EndsWith(".uptnl") -or
      $_.EndsWith(".ucas") -or
      $_.EndsWith(".utoc")
    }
  )
  $t3 = New-TestName "Unreal manual step sidecar file detection in $FolderRel is informational only"
  if ($sidecarFiles.Count -gt 0) {
    Pass $t3 "detected sidecar-like UE files: $($sidecarFiles.Count)"
  }
  else {
    Warn $t3 "no sidecar files detected (valid in UE 5.x depending on asset type/workflow)"
  }

  $rel = @()
  foreach ($f in $ueFiles) {
    $r = Resolve-Path -LiteralPath $f -ErrorAction SilentlyContinue
    if (-not $r) { continue }
    $full = $r.Path
    $relPath = $full.Substring($repoRoot.Length).TrimStart('\', '/')
    $rel += ($relPath -replace '\\', '/')
  }
  return @($rel | Sort-Object -Unique)
}

function Get-UEPrimaryPaths([string[]]$ueRelPaths) {
  return @($ueRelPaths | Where-Object { $_.EndsWith(".uasset") -or $_.EndsWith(".umap") })
}

function Assert-GuardedConflictsIncludeAnyOf {
  param(
    [Parameter(Mandatory)][string]$testName,
    [Parameter(Mandatory)][string[]]$candidateRelPaths
  )

  $u = & $script:GitExe diff --name-only --diff-filter=U 2>$null
  $u = @($u) | ForEach-Object { $_.Trim() } | Where-Object { $_ }

  $hit = $false
  foreach ($p in $candidateRelPaths) {
    if ($u -contains $p) { $hit = $true; break }
  }

  if ($hit) { Pass $testName "conflicts include at least one expected guarded UE file"; return $true }
  Fail $testName "conflicts did not include any expected UE files"
  return $false
}

function Write-GlobSet {
  param(
    [Parameter(Mandatory)][string]$LabelPrefix,
    [Parameter(Mandatory)][ValidateSet("Red", "Blue", "Green")][string]$Bg
  )
  New-Item -ItemType Directory -Force (Join-Path $repoRoot $globDirRel) | Out-Null
  $i = 1
  foreach ($rp in $globPaths) {
    $abs = Join-Path $repoRoot $rp
    Write-LabeledPng -Path $abs -Label "$LabelPrefix-$i" -Bg $Bg -Size 384
    $i++
  }
}

function Assert-ConflictsContinueSucceeds {
  param(
    [Parameter(Mandatory)][string]$testName,
    [switch]$AllowStopOnNextConflict
  )

  $failIndex = [int]$script:Replay.Count
  
  # Use git alias directly
  $res = RunArgsCapture git @("conflicts", "continue", "--skip-editor")
  $code = $res.Code
  
  if ($code -eq 0) { Pass $testName "conflicts continue succeeded"; return $true }

  if ($AllowStopOnNextConflict) {
    $joined = ($res.Output -join "`n")
    $advancePatterns = @(
      'Rebasing \(\d+/\d+\)',
      'could not apply',
      'CONFLICT \(content\): Merge conflict',
      'Cannot merge binary files'
    )

    foreach ($pat in $advancePatterns) {
      if ($joined -match $pat) {
        Pass $testName "conflicts continue advanced and stopped at next expected conflict"
        return $true
      }
    }
  }

  Fail $testName "conflicts continue failed (exit=$code)" $failIndex
  return $false
}

function Assert-ConflictsContinueBlocked([string]$testName) {
  $failIndex = [int]$script:Replay.Count
  
  # Use git alias directly
  $res = RunArgsCapture git @("conflicts", "continue", "--skip-editor")
  
  if ($res.Code -eq 0) {
    Fail $testName "conflicts continue unexpectedly succeeded" $failIndex
    return $false
  }

  $joined = ($res.Output -join "`n")
  $good = @(
    'BLOCKED: Guarded binary file\(s\) require helper approval',
    'Resolve these files first using',
    'git ours',
    'git theirs'
  )

  foreach ($pat in $good) {
    if ($joined -match $pat) {
      Pass $testName "conflicts continue blocked by guard (matched: $pat)"
      return $true
    }
  }

  Fail $testName "conflicts continue failed but did not show expected guard messaging (exit=$($res.Code))" $failIndex
  return $false
}
