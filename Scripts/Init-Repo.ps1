# Scripts/Init-Repo.ps1
# PowerShell 7+ repo bootstrap for Ghost_Game (Scare B&B)
# Usage (from anywhere inside the repo):
#   pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/Init-Repo.ps1
#
# Optional:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/Init-Repo.ps1 -SkipUnrealSync
#   pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/Init-Repo.ps1 -NoBuild
#   pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/Init-Repo.ps1 -SkipLfsPull

[CmdletBinding()]
param(
  [switch]$SkipLfsPull,
  [switch]$SkipUnrealSync,
  [switch]$NoBuild,
  [switch]$NoRegen,

  [ValidateSet("Development", "Debug")]
  [string]$Config = "Development",

  [ValidateSet("Win64")]
  [string]$Platform = "Win64"
)

$ErrorActionPreference = "Stop"

function Info($m) { Write-Host "[Init] $m" -ForegroundColor Cyan }
function Warn($m) { Write-Host "[Init] $m" -ForegroundColor Yellow }
function Err ($m) { Write-Host "[Init] $m" -ForegroundColor Red }
function Ok  ($m) { Write-Host "[Init] $m" -ForegroundColor Green }

# --- Require PowerShell 7+ ---
if ($PSVersionTable.PSVersion.Major -lt 7) {
  throw "PowerShell 7+ is required. Current: $($PSVersionTable.PSVersion)"
}

# --- Optional: ensure ANSI is not downgraded in nested calls ---
# This helps if a user has changed OutputRendering elsewhere.
try { $PSStyle.OutputRendering = 'Host' } catch { }

# --- Require git ---
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
  throw "git not found. Install Git for Windows and try again."
}

# --- Find repo root and move there ---
$repoRoot = (git rev-parse --show-toplevel 2>$null).Trim()
if (-not $repoRoot) { throw "Not inside a git repository (git rev-parse failed)." }

Set-Location $repoRoot
Info "Repo root: $repoRoot"

# --- Warn if folder name isn't Ghost_Game (UE sensitivity) ---
$leaf = Split-Path $repoRoot -Leaf
if ($leaf -ne "Ghost_Game") {
  Warn "Repo folder name is '$leaf' but Unreal project/module expects 'Ghost_Game'."
  Warn "If this is a fresh clone from GitHub (Ghost-Game), rename the folder to Ghost_Game."
}

# --- Ensure Git LFS is available and initialized ---
if (-not (Get-Command git-lfs -ErrorAction SilentlyContinue)) {
  throw "git-lfs not found. Install Git LFS and try again."
}

# IMPORTANT:
# - We do NOT want git lfs to attempt to install hook files into the repo because we commit our own hooks.
# - --skip-repo installs/configures LFS filters but skips repo hook installation.  (see git-lfs-install(1))
Info "Initializing Git LFS filters for this repo (skipping repo hook install)..."
& git lfs install --local --skip-repo
if ($LASTEXITCODE -ne 0) { throw "git lfs install failed (exit $LASTEXITCODE)." }

if (-not $SkipLfsPull) {
  Info "Pulling LFS content (this may take a while on first run)..."
  & git lfs pull
  if ($LASTEXITCODE -ne 0) { throw "git lfs pull failed (exit $LASTEXITCODE)." }
}
else {
  Warn "Skipping 'git lfs pull' (SkipLfsPull set)."
}

# --- Configure recommended repo-local git settings ---
Info "Applying recommended repo-local git config..."
& git config --local core.hooksPath .githooks
& git config --local fetch.prune true
& git config --local pull.ff only
& git config --local core.autocrlf false
& git config --local core.eol lf
& git config --local core.safecrlf true
& git config --local advice.mergeConflict false

Ok "Git config applied:"
& git config --local --get core.hooksPath | ForEach-Object { Write-Host "  core.hooksPath=$_" }
& git config --local --get pull.ff        | ForEach-Object { Write-Host "  pull.ff=$_" }
& git config --local --get fetch.prune    | ForEach-Object { Write-Host "  fetch.prune=$_" }
& git config --local --get core.autocrlf  | ForEach-Object { Write-Host "  core.autocrlf=$_" }
& git config --local --get core.eol       | ForEach-Object { Write-Host "  core.eol=$_" }
& git config --local --get core.safecrlf  | ForEach-Object { Write-Host "  core.safecrlf=$_" }
& git config --local --get advice.mergeConflict | ForEach-Object { Write-Host "  advice.mergeConflict=$_" }
# --- Ensure hook scripts exist ---
$requiredHooks = @(
  ".githooks\pre-commit",
  ".githooks\pre-push",
  ".githooks\post-checkout",
  ".githooks\post-merge",
  ".githooks\post-commit",
  ".githooks\post-rewrite"
)

$requiredShared = @(
  "Scripts\git-hooks\colors.sh",
  "Scripts\git-hooks\hook-common.sh"
)

$requiredHelpers = @(
  "Scripts\git-tools\conflicts.ps1",
  "Scripts\git-tools\GitConflictHelpers.ps1"
)

$requiredTests = @(
  "Scripts\git-hooks\Test-Hooks.ps1"
)

$missing = @()
foreach ($p in @($requiredHooks + $requiredShared + $requiredHelpers + $requiredTests)) {
  if (-not (Test-Path (Join-Path $repoRoot $p))) { $missing += $p }
}

if ($missing.Count -gt 0) {
  Err "Missing required file(s):"
  $missing | Sort-Object -Unique | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
  throw "Required files are missing. Pull latest changes and re-run Init-Repo."
}

# --- Mark hook scripts and shared sh scripts executable in the index (best-effort) ---
Info "Ensuring hooks + shared hook scripts are marked executable in git index..."
$chmodPaths = @(
  ".githooks/pre-commit",
  ".githooks/pre-push",
  ".githooks/post-checkout",
  ".githooks/post-merge",
  ".githooks/post-commit",
  ".githooks/post-rewrite",
  "Scripts/git-hooks/colors.sh",
  "Scripts/git-hooks/hook-common.sh"
)

foreach ($p in $chmodPaths) {
  & git update-index --chmod=+x -- $p 2>$null | Out-Null
}
Ok "Exec bits updated (best-effort)."

# --- Run hook enable script (idempotent) ---
$enableHooks = Join-Path $repoRoot "Scripts\git-hooks\Enable-GitHooks.ps1"
if (Test-Path $enableHooks) {
  Info "Running Enable-GitHooks.ps1..."
  & pwsh -NoProfile -ExecutionPolicy Bypass -File $enableHooks
  if ($LASTEXITCODE -ne 0) { throw "Enable-GitHooks.ps1 failed (exit $LASTEXITCODE)." }
}
else {
  Warn "Enable-GitHooks.ps1 not found at Scripts\git-hooks\Enable-GitHooks.ps1 (skipping)."
}

# --- Configure git aliases for conflict helpers ---
Info "Configuring git aliases: ours / theirs / conflicts ..."
& git config --local alias.ours     '!pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/git-tools/conflicts.ps1 ours'
& git config --local alias.theirs   '!pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/git-tools/conflicts.ps1 theirs'
& git config --local alias.conflicts '!pwsh -NoProfile -ExecutionPolicy Bypass -File Scripts/git-tools/conflicts.ps1'

if ($LASTEXITCODE -ne 0) { throw "Failed to configure conflict helper aliases." }

Ok "Aliases configured:"
& git config --local --get alias.ours      | ForEach-Object { Write-Host "  alias.ours=$_" -ForegroundColor Green }
& git config --local --get alias.theirs    | ForEach-Object { Write-Host "  alias.theirs=$_" -ForegroundColor Green }
& git config --local --get alias.conflicts | ForEach-Object { Write-Host "  alias.conflicts=$_" -ForegroundColor Green }
Write-Host "  usage: git ours <patterns...>" -ForegroundColor Green
Write-Host "  usage: git theirs <patterns...>" -ForegroundColor Green
Write-Host "  usage: git conflicts <status|sync|continue|abort|restart|help>" -ForegroundColor Green

# --- Hook self-test ---
$hookTest = Join-Path $repoRoot "Scripts\git-hooks\Test-Hooks.ps1"
Info "Running hook self-test..."
& pwsh -NoProfile -ExecutionPolicy Bypass -File $hookTest
if ($LASTEXITCODE -ne 0) { throw "Hook self-test failed (exit $LASTEXITCODE)." }
Ok "Hook self-test completed."

# --- Optional: run UnrealSync once for first-time setup ---
$unrealSync = Join-Path $repoRoot "Scripts\Unreal\UnrealSync.ps1"
if (-not $SkipUnrealSync -and (Test-Path $unrealSync)) {
  Info "Running UnrealSync for first-time setup..."

  $_args = @(
    "-NoProfile", "-ExecutionPolicy", "Bypass",
    "-File", $unrealSync,
    "-Force",
    "-Config", $Config,
    "-Platform", $Platform
  )
  if ($NoBuild) { $_args += "-NoBuild" }
  if ($NoRegen) { $_args += "-NoRegen" }

  & pwsh @_args
  if ($LASTEXITCODE -ne 0) { throw "UnrealSync failed (exit $LASTEXITCODE)." }

  Ok "UnrealSync completed."
}
elseif ($SkipUnrealSync) {
  Warn "Skipping UnrealSync (SkipUnrealSync set)."
}
else {
  Warn "UnrealSync script not found at Scripts/Unreal/UnrealSync.ps1 (skipping)."
}

Ok "Repo initialization complete."
Info "Next steps:"
Write-Host "  - Open repo folder in VS Code" -ForegroundColor Cyan
Write-Host "  - Verify hooks by attempting a small commit" -ForegroundColor Cyan
Write-Host "  - During merge/rebase conflicts, use: git ours / git theirs" -ForegroundColor Cyan
