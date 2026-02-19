[CmdletBinding()]
param(
  # Hook parameters (optional for manual use)
  [string]$OldRev,
  [string]$NewRev,
  [int]$Flag = 1,

  # Manual / control flags
  [switch]$Force,           # Always run regen+build even if no structural triggers detected
  [switch]$CleanSaved,      # Delete the Saved directory
  [switch]$CleanCache,      # Delete the DerivedDataCache directory
  [switch]$NoRegen,         # Skip generating project files
  [switch]$NoBuild,         # Skip building
  [switch]$NonInteractive,  # Tells the script to avoid prompting the user
  [switch]$DryRun,          # Validate detection/prompt flow without cleanup/build

  # Optional: explicitly point at a .code-workspace file
  [string]$WorkspacePath,

  [ValidateSet("Development", "Debug")]
  [string]$Config = "Development",

  [ValidateSet("Win64")]
  [string]$Platform = "Win64"
)

$ErrorActionPreference = "Stop"

function Info($msg) { Write-Host "[UE Sync] $msg" -ForegroundColor Cyan }
function Warn($msg) { Write-Host "[UE Sync] $msg" -ForegroundColor Yellow }
function Err ($msg) { Write-Host "[UE Sync] $msg" -ForegroundColor Red }
function Success($msg) { Write-Host "[UE Sync] $msg" -ForegroundColor Green }

function Test-IsInteractiveConsole {
  try {
    if (-not [Environment]::UserInteractive) { return $false }
    if ($env:CI -or $env:GITHUB_ACTIONS -or $env:TF_BUILD -or $env:JENKINS_URL) { return $false }
    if ($Host.Name -eq "ServerRemoteHost") { return $false }

    # Preferred signal: real console host with usable RawUI and non-redirected input.
    if ($Host.UI -and $Host.UI.RawUI -and -not [Console]::IsInputRedirected) {
      return $true
    }

    # Fallback for hook contexts launched from an interactive shell (TTY can be hidden).
    if ($env:TERM -and $env:TERM -ne "dumb") { return $true }

    return $false
  }
  catch { return $false }
}

function Test-CanPrompt {
  try {
    if (-not [Environment]::UserInteractive) { return $false }
    if (-not $Host.UI -or -not $Host.UI.RawUI) { return $false }
    if ([Console]::IsInputRedirected) { return $false }
    if ([Console]::IsOutputRedirected) { return $false }
    return $true
  }
  catch { return $false }
}


function Remove-IfExists {
  param(
    [Parameter(Mandatory)][string]$Path,
    [switch]$NonInteractive,
    [int]$MaxAutoRetries = 2
  )

  if (-not (Test-Path $Path)) { return $true }

  $attempt = 0
  while ($true) {
    try {
      Remove-Item $Path -Recurse -Force -ErrorAction Stop
      return $true
    }
    catch {
      $attempt++
      $errMsg = $_.Exception.Message

      if ($attempt -le $MaxAutoRetries) {
        Start-Sleep -Milliseconds 250
        continue
      }

      if ($NonInteractive) {
        Warn "Could not clean '$Path' because a file is in use. Continuing without deleting this folder."
        Warn $errMsg
        return $false
      }

      Warn "Could not clean '$Path' because a file is in use."
      Warn "Close apps/files using this path (for example VS Code tab, indexer, compiler), then choose Retry."
      $choice = (Read-Host "[UE Sync] Cleanup failed. [R]etry / [S]kip cleanup / [A]bort").Trim().ToLowerInvariant()

      switch ($choice) {
        { $_ -in @("", "r", "retry") } {
          $attempt = 0
          continue
        }
        { $_ -in @("s", "skip") } {
          Warn "Skipping cleanup for '$Path'."
          return $false
        }
        default {
          throw "Aborted by user during cleanup of '$Path'."
        }
      }
    }
  }
}

function Get-UProjectPath {
  $uproject = Get-ChildItem -Path (Get-Location) -Filter *.uproject -File | Select-Object -First 1
  if (-not $uproject) { throw "No .uproject found in the current directory. Run this from the project root." }
  $uproject.FullName
  
}

function Get-ProjectName([string]$uprojectPath) {
  [IO.Path]::GetFileNameWithoutExtension($uprojectPath)
}

function Test-EngineRoot([string]$root) {
  if (-not $root) { return $false }
  if (-not (Test-Path $root)) { return $false }
  Test-Path (Join-Path $root "Engine\Build\BatchFiles\Build.bat")
}

function Resolve-PathRelativeTo([string]$baseDir, [string]$path) {
  if (-not $path) { return $null }
  if ([IO.Path]::IsPathRooted($path)) { return $path }
  Join-Path $baseDir $path
}

function Get-EngineRootFromWorkspace([string]$repoRoot, [string]$explicitWorkspacePath) {
  $wsFile = $null

  if ($explicitWorkspacePath) {
    $wsFile = Get-Item -LiteralPath $explicitWorkspacePath -ErrorAction SilentlyContinue
  }
  else {
    $wsFile = Get-ChildItem -Path $repoRoot -Filter *.code-workspace -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
  }

  if (-not $wsFile) { return $null }

  try { $json = Get-Content $wsFile.FullName -Raw | ConvertFrom-Json }
  catch { return $null }

  $folders = @($json.folders)
  if (-not $folders -or $folders.Count -eq 0) { return $null }

  # Prefer folders named UE5 / UE*
  $preferred = $folders | Where-Object { $_.name -and $_.name -match '^UE' } | Select-Object -First 1
  if ($preferred) {
    $p = Resolve-PathRelativeTo $repoRoot $preferred.path
    if (Test-EngineRoot $p) { return $p }
  }

  # Otherwise, find any folder path that looks like UE_5.x and validates
  foreach ($f in $folders) {
    $p = Resolve-PathRelativeTo $repoRoot $f.path
    if (-not $p) { continue }

    if ($p -match 'UE_\d' -and (Test-EngineRoot $p)) { return $p }

    # If path points inside ...\Engine\..., walk up to root
    $idx = $p.ToLower().IndexOf("\engine\")
    if ($idx -ge 0) {
      $root = $p.Substring(0, $idx)
      if (Test-EngineRoot $root) { return $root }
    }

    # Path points to ...\Engine
    if ($p.ToLower().EndsWith("\engine")) {
      $root = Split-Path $p -Parent
      if (Test-EngineRoot $root) { return $root }
    }
  }

  return $null
}

function Resolve-EngineRootForBuild([string]$workspacePathOverride) {
  $repoRoot = (Get-Location).Path

  # Primary: read the UE install from local workspace (your team standard)
  $wsRoot = Get-EngineRootFromWorkspace $repoRoot $workspacePathOverride
  if ($wsRoot) { return $wsRoot }

  # Optional last resort: allow UE_ENGINE_DIR (won't be required, but helpful if workspace is missing)
  if ($env:UE_ENGINE_DIR -and (Test-EngineRoot $env:UE_ENGINE_DIR)) { return $env:UE_ENGINE_DIR }

  throw @"
Could not resolve Unreal Engine install path for BUILD.

Tried:
- *.code-workspace folders[] (UE5/UE_* entry)
- UE_ENGINE_DIR env var (fallback)

Fix:
- Re-generate the VS Code workspace in Unreal so it contains the UE install folder under folders[], or
- Set UE_ENGINE_DIR for this machine.
"@
}

function Get-UProjectProgId {
  # Example: ".uproject=Unreal.ProjectFile" -> returns "Unreal.ProjectFile"
  $assoc = cmd /c assoc .uproject 2>$null
  if (-not $assoc) { return $null }
  ($assoc -replace '^.*=').Trim()
}

function Get-UVSPathFromRegistry {
  # Reads the same command Explorer runs for right-click "Generate Visual Studio project files"
  # and extracts UnrealVersionSelector.exe path from it.
  $progId = Get-UProjectProgId
  $candidateShellRoots = @()

  if ($progId) {
    $candidateShellRoots += "Registry::HKEY_CLASSES_ROOT\$progId\shell"
  }
  # common fallback ProgID
  $candidateShellRoots += "Registry::HKEY_CLASSES_ROOT\Unreal.ProjectFile\shell"
  $candidateShellRoots += "Registry::HKEY_CLASSES_ROOT\.uproject\shell"

  foreach ($shellRoot in $candidateShellRoots | Select-Object -Unique) {
    if (-not (Test-Path $shellRoot)) { continue }

    foreach ($verbKey in Get-ChildItem $shellRoot -ErrorAction SilentlyContinue) {
      $cmdKey = Join-Path $verbKey.PSPath "command"
      if (-not (Test-Path $cmdKey)) { continue }

      $cmd = (Get-ItemProperty $cmdKey -ErrorAction SilentlyContinue).'(default)'
      if (-not $cmd) { continue }

      # We only care about the verb that runs UnrealVersionSelector.exe /projectfiles "%1"
      if ($cmd -match 'UnrealVersionSelector\.exe"\s+/projectfiles\s+"%1"' -or
        $cmd -match 'UnrealVersionSelector\.exe"\s+/projectfiles') {

        # Extract the exe path between quotes
        if ($cmd -match '^"(?<exe>[^"]+UnrealVersionSelector\.exe)"') {
          $exe = $Matches.exe
          if (Test-Path $exe) { return $exe }
        }
      }
    }
  }

  return $null
}

function Get-ChangedFiles([string]$oldrev, [string]$newrev) {
  if ([string]::IsNullOrWhiteSpace($oldrev) -or [string]::IsNullOrWhiteSpace($newrev)) { return @() }
  $out = git diff --name-only $oldrev $newrev 2>$null
  if ($LASTEXITCODE -ne 0) { return @() }
  @($out)
}

function Get-RebuildTriggers([string[]]$ChangedFiles) {
  if (-not $ChangedFiles -or $ChangedFiles.Count -eq 0) {
    return @()
  }

  $triggers = @()

  foreach ($f in $ChangedFiles) {
    if ($f -match '^Source/.*\.(h|hpp|cpp|inl)$') { $triggers += $f; continue }
    if ($f -match '\.Build\.cs$') { $triggers += $f; continue }
    if ($f -match '\.Target\.cs$') { $triggers += $f; continue }
    if ($f -match '\.uproject$') { $triggers += $f; continue }
    if ($f -match '^Plugins/.*\.(uplugin|Build\.cs|Target\.cs|h|hpp|cpp)$') {
      $triggers += $f
      continue
    }
  }

  return ($triggers | Sort-Object -Unique)
}

function Show-RebuildTriggers([string[]]$Triggers) {
  if (-not $Triggers -or $Triggers.Count -eq 0) {
    return
  }

  Warn "Structural C++ changes detected in the following files:"
  foreach ($t in $Triggers) {
    Warn " - $t"
  }
}

function Invoke-Regenerate-ProjectFiles([string]$uprojectPath, [string]$engineRootForFallback) {
  $uvs = Get-UVSPathFromRegistry
  if (-not $uvs) {
    Warn "UVS not found via registry; using RunUBT fallback..."
  }
  else {
    Warn "Regenerating project files (context menu UVS)..."
    & $uvs "/projectfiles" $uprojectPath | Out-Host

    if ($LASTEXITCODE -eq 0) { return }

    Warn "UVS failed (exit $LASTEXITCODE). Falling back to RunUBT..."
  }

  if (-not $engineRootForFallback) {
    throw "Cannot fallback to RunUBT: engine root not provided."
  }

  $runUbt = Join-Path $engineRootForFallback "Engine\Build\BatchFiles\RunUBT.bat"
  if (-not (Test-Path $runUbt)) {
    throw "RunUBT.bat not found at expected path: $runUbt"
  }

  Warn "Regenerating project files (RunUBT -projectfiles -vscode)..."
  & $runUbt `
    -projectfiles `
    -vscode `
    "-project=$uprojectPath" `
    -game `
    -engine `
    -dotnet | Out-Host

  if ($LASTEXITCODE -ne 0) {
    throw "RunUBT projectfiles failed (exit $LASTEXITCODE)."
  }
}


function Build-Editor([string]$engineRoot, [string]$uprojectPath, [string]$projectName, [string]$platform, [string]$config) {
  $buildBat = Join-Path $engineRoot "Engine\Build\BatchFiles\Build.bat"
  if (-not (Test-Path $buildBat)) { throw "Build.bat not found: $buildBat" }

  $target = "${projectName}Editor"
  Warn "Building $target ($platform $config) using engine root: $engineRoot"
  & $buildBat $target $platform $config -Project="`"$uprojectPath`"" -WaitMutex | Out-Host
}

# ---- Main ----
$manual = $Force -or $CleanSaved -or $CleanCache -or $NoRegen -or $NoBuild

# If invoked from hook, only run on branch checkouts.
if (-not $manual -and $Flag -ne 1) { 
  exit 0 
}

# Rebase can execute hooks at many internal steps. Keep hook execution silent there.
$reflogAction = [string]$env:GIT_REFLOG_ACTION
if (-not $manual -and $reflogAction -match 'rebase') {
  exit 0
}

# Skip silently during active merge/rebase contexts when running from hooks.
if (-not $manual) {
  $gitDir = (git rev-parse --git-dir 2>$null | Select-Object -First 1).Trim()
  if ([string]::IsNullOrWhiteSpace($gitDir)) { $gitDir = ".git" }
  if (-not [System.IO.Path]::IsPathRooted($gitDir)) {
    $gitDir = Join-Path (Get-Location).Path $gitDir
  }

  if (
      (Test-Path (Join-Path $gitDir "rebase-apply")) -or
      (Test-Path (Join-Path $gitDir "rebase-merge")) -or
      (Test-Path (Join-Path $gitDir "MERGE_HEAD")) -or
      (Test-Path (Join-Path $gitDir "CHERRY_PICK_HEAD")) -or
      (Test-Path (Join-Path $gitDir "REVERT_HEAD"))
    ) {
    exit 0
  }
}

$triggerFiles = @()
if (-not $manual) {
  $changed = Get-ChangedFiles $OldRev $NewRev
  $triggerFiles = Get-RebuildTriggers $changed
  if (-not $Force -and $triggerFiles.Count -eq 0) {
    # No structural trigger => no-op, keep hook output clean.
    exit 0
  }
}

$uprojectPath = Get-UProjectPath
$projectName = Get-ProjectName $uprojectPath

Info "UProject Path: $uprojectPath"
if (-not $manual) {
  Info "Checking for structural C++ changes between $OldRev and $NewRev..."
  Show-RebuildTriggers $triggerFiles
}

if (-not $manual) {
  $isNonInteractive = $NonInteractive.IsPresent
  $canPrompt = Test-CanPrompt
  if (-not $isNonInteractive -and -not $canPrompt) { $isNonInteractive = $true }

  if (-not $isNonInteractive) {
    Info "Would you like to proceed with regenerating project files and building the editor? (y/n)"
    $response = Read-Host
    if ($response -ne 'y' -and $response -ne 'Y') {
      Warn "User chose not to proceed. Exiting."
      exit 0
    }
  }
  else {
    Warn "Non-interactive execution detected; proceeding without confirmation."
  }
}
else {
  $isNonInteractive = $NonInteractive.IsPresent
  if (-not $isNonInteractive) {
    $isNonInteractive = -not (Test-CanPrompt)
  }
}

Info "Cleaning generated folders..."
if ($DryRun) {
  Info "DryRun enabled. Skipping cleanup/regeneration/build."
  exit 0
}

[void](Remove-IfExists -Path "Binaries" -NonInteractive:$isNonInteractive)
[void](Remove-IfExists -Path "Intermediate" -NonInteractive:$isNonInteractive)
if ($CleanCache) { [void](Remove-IfExists -Path "DerivedDataCache" -NonInteractive:$isNonInteractive) }
if ($CleanSaved) { [void](Remove-IfExists -Path "Saved" -NonInteractive:$isNonInteractive) }


if (-not $NoRegen) {
  Invoke-Regenerate-ProjectFiles $uprojectPath
}
else {
  Warn "Skipping project file regeneration..."
}

if (-not $NoBuild) {
  $engineRoot = Resolve-EngineRootForBuild $WorkspacePath
  Info "Engine (build): $engineRoot"
  Build-Editor $engineRoot $uprojectPath $projectName $Platform $Config
}
else {
  Warn "Skipping build..."
}

Success "Done."
exit 0
