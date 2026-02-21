[CmdletBinding()]
param(
  [switch]$NoCleanup,
  [switch]$FailFast
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = (git rev-parse --show-toplevel 2>$null).Trim()
if (-not $repoRoot) { throw "Not inside a git repository." }
Set-Location $repoRoot

$stamp = (Get-Date).ToString("yyyyMMdd-HHmmss")
$resultsDir = Join-Path $repoRoot "Scripts\Tests\Test-UESyncShellAliasesResults"
New-Item -ItemType Directory -Force -Path $resultsDir | Out-Null
$logPath = Join-Path $resultsDir "UESyncShellAliasesTest-$stamp.log"
$scratchRoot = Join-Path $resultsDir "scratch-$stamp"
New-Item -ItemType Directory -Force -Path $scratchRoot | Out-Null

$script:PassCount = 0
$script:FailCount = 0
$script:WarnCount = 0
$script:SkipCount = 0
$script:CleanupRan = $false
$script:ExternalTempDirs = New-Object System.Collections.Generic.List[string]

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

function Assert-Condition {
  param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][bool]$Condition,
    [string]$PassDetail = "condition is true",
    [string]$FailDetail = "condition is false"
  )
  if ($Condition) { Pass $Name $PassDetail; return }
  Fail $Name $FailDetail
}

function Assert-TextContains {
  param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$Text,
    [Parameter(Mandatory)][string]$Needle
  )
  if ([string]::Concat($Text).Contains($Needle)) { Pass $Name "matched: $Needle"; return }
  Fail $Name "missing expected text: $Needle"
}

function Assert-TextNotContains {
  param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$Text,
    [Parameter(Mandatory)][string]$Needle
  )
  if (-not [string]::Concat($Text).Contains($Needle)) { Pass $Name "did not match: $Needle"; return }
  Fail $Name "unexpected text found: $Needle"
}

function Normalize-Newlines([string]$Text) {
  if ($null -eq $Text) { return "" }
  return ($Text -replace "`r`n", "`n" -replace "`r", "`n")
}

function Count-Matches {
  param(
    [Parameter(Mandatory)][string]$Text,
    [Parameter(Mandatory)][string]$Pattern
  )
  return [regex]::Matches($Text, $Pattern).Count
}

function Remove-ManagedBlock {
  param(
    [Parameter(Mandatory)][string]$Text,
    [Parameter(Mandatory)][string]$StartMarker,
    [Parameter(Mandatory)][string]$EndMarker
  )
  $pattern = "(?s)$([regex]::Escape($StartMarker)).*?$([regex]::Escape($EndMarker))"
  return [regex]::Replace($Text, $pattern, "")
}

function Write-TextFileLf {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Content
  )

  $normalized = Normalize-Newlines $Content
  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($Path, $normalized, $utf8NoBom)
}

function New-ScratchPath([string]$Name) {
  return (Join-Path $scratchRoot $Name)
}

function Reset-LoadedAliases {
  Remove-Item -LiteralPath Function:\Invoke-UETools -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath Function:\Invoke-UESync -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath Function:\Invoke-CozyUESync -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath Alias:\ue-tools -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath Alias:\uesync -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath Alias:\ue-sync -ErrorAction SilentlyContinue
}

function Restore-State {
  if ($script:CleanupRan) { return }
  $script:CleanupRan = $true

  Reset-LoadedAliases

  if ($NoCleanup) {
    Warn "Cleanup" "NoCleanup set; leaving scratch files in place."
    return
  }

  try {
    if (Test-Path -LiteralPath $scratchRoot) {
      Remove-Item -LiteralPath $scratchRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    foreach ($p in ($script:ExternalTempDirs | Sort-Object -Unique)) {
      if ($p -and (Test-Path -LiteralPath $p)) {
        Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
  }
  catch {
    Warn "Cleanup" "Could not fully delete scratch root: $scratchRoot"
  }
}

try {
  Step "UE Tools Alias Automated Tests ($stamp)"
  Write-Log "Repo: $repoRoot" Cyan
  Write-Log "Log : $logPath" Cyan

  $helperPath = Join-Path $repoRoot "Scripts\Unreal\UESyncShellAliases.ps1"
  if (-not (Test-Path -LiteralPath $helperPath)) {
    throw "Helper script not found: $helperPath"
  }
  . $helperPath

  Step "Case 1: Snippet contract uses ue-tools subcommands"
  $snippet = Get-UEToolsAliasSnippet
  Assert-TextContains "case1 function name" $snippet "function Invoke-UETools"
  Assert-TextContains "case1 alias ue-tools" $snippet "Set-Alias -Name ue-tools -Value Invoke-UETools"
  Assert-TextContains "case1 command usage line" $snippet "ue-tools <command> [options]"
  Assert-TextContains "case1 has build command" $snippet "build [sync options]"
  Assert-TextContains "case1 has help command" $snippet "help                 Show this help text."
  Assert-TextNotContains "case1 no legacy function" $snippet "function Invoke-UESync"
  Assert-TextNotContains "case1 no legacy alias uesync" $snippet "Set-Alias -Name uesync"
  Assert-TextNotContains "case1 no legacy alias ue-sync" $snippet "Set-Alias -Name ue-sync"
  Assert-TextNotContains "case1 no project marker" $snippet "cppCozyRPG"

  Step "Case 2: Install writes ue-tools block into a new profile"
  $profileNew = New-ScratchPath "profile-new.ps1"
  $installNew = Install-UEToolsShellAliases -ProfilePath $profileNew
  $markers = Get-UEToolsAliasMarkers
  $newContent = Get-Content -LiteralPath $profileNew -Raw

  Assert-Condition "case2 profile created" (Test-Path -LiteralPath $profileNew) "profile file exists"
  Assert-TextContains "case2 start marker present" $newContent $markers.StartMarker
  Assert-TextContains "case2 end marker present" $newContent $markers.EndMarker
  Assert-Condition "case2 one start marker" ((Count-Matches $newContent ([regex]::Escape($markers.StartMarker))) -eq 1) "start marker count=1"
  Assert-Condition "case2 one end marker" ((Count-Matches $newContent ([regex]::Escape($markers.EndMarker))) -eq 1) "end marker count=1"
  Assert-Condition "case2 one function definition" ((Count-Matches $newContent 'function\s+Invoke-UETools') -eq 1) "Invoke-UETools definition count=1"
  Assert-Condition "case2 return metadata function name" ($installNew.FunctionName -eq "Invoke-UETools") "FunctionName metadata is Invoke-UETools"
  Assert-Condition "case2 alias metadata includes ue-tools" ($installNew.Aliases -contains "ue-tools") "Aliases metadata includes ue-tools"
  Assert-TextNotContains "case2 no old marker remains" $newContent "# >>> ue-sync aliases >>>"

  Step "Case 3: Install is idempotent and does not duplicate function block"
  $beforeSecondInstall = Get-Content -LiteralPath $profileNew -Raw
  $null = Install-UEToolsShellAliases -ProfilePath $profileNew
  $afterSecondInstall = Get-Content -LiteralPath $profileNew -Raw
  Assert-Condition "case3 content unchanged on second install" ($beforeSecondInstall -ceq $afterSecondInstall) "profile content is unchanged"
  Assert-Condition "case3 one start marker" ((Count-Matches $afterSecondInstall ([regex]::Escape($markers.StartMarker))) -eq 1) "start marker count=1"
  Assert-Condition "case3 one end marker" ((Count-Matches $afterSecondInstall ([regex]::Escape($markers.EndMarker))) -eq 1) "end marker count=1"
  Assert-Condition "case3 one function" ((Count-Matches $afterSecondInstall 'function\s+Invoke-UETools') -eq 1) "Invoke-UETools definition count=1"

  Step "Case 4: Legacy block migration preserves non-managed profile content"
  $profileLegacy = New-ScratchPath "profile-legacy.ps1"
  $legacyContent = @(
    "KEEP_TOP = '1'"
    "function KeepTop { return 'top-ok' }"
    "# >>> ue-sync aliases >>>"
    "function Invoke-UESync { throw 'legacy-sync-block' }"
    "Set-Alias -Name uesync -Value Invoke-UESync"
    "# <<< ue-sync aliases <<<"
    "# >>> cppCozyRPG UnrealSync aliases >>>"
    "function Invoke-CozyUESync { throw 'legacy-cozy-block' }"
    "Set-Alias -Name ue-sync -Value Invoke-CozyUESync"
    "# <<< cppCozyRPG UnrealSync aliases <<<"
    "KEEP_BOTTOM = '1'"
    "function KeepBottom { return 'bottom-ok' }"
  ) -join "`r`n"
  Write-TextFileLf -Path $profileLegacy -Content $legacyContent

  $null = Install-UEToolsShellAliases -ProfilePath $profileLegacy
  $migratedContent = Get-Content -LiteralPath $profileLegacy -Raw
  $outsideAfter = Remove-ManagedBlock -Text $migratedContent -StartMarker $markers.StartMarker -EndMarker $markers.EndMarker

  Assert-TextNotContains "case4 removes legacy marker" $migratedContent "# >>> ue-sync aliases >>>"
  Assert-TextNotContains "case4 removes cozy marker" $migratedContent "# >>> cppCozyRPG UnrealSync aliases >>>"
  Assert-TextNotContains "case4 removes legacy sync body" $migratedContent "legacy-sync-block"
  Assert-TextNotContains "case4 removes legacy cozy body" $migratedContent "legacy-cozy-block"
  Assert-TextContains "case4 top line preserved" $outsideAfter "KEEP_TOP = '1'"
  Assert-TextContains "case4 bottom line preserved" $outsideAfter "KEEP_BOTTOM = '1'"
  Assert-Condition "case4 one ue-tools function" ((Count-Matches $migratedContent 'function\s+Invoke-UETools') -eq 1) "Invoke-UETools definition count=1"

  Step "Case 5: Help command works via function and alias"
  Reset-LoadedAliases
  . $profileNew
  $helpDirect = @(& { Invoke-UETools help } 2>&1 6>&1)
  $helpAlias = @(& { ue-tools help } 2>&1 6>&1)
  $helpDirectText = ($helpDirect | ForEach-Object { "$_" }) -join "`n"
  $helpAliasText = ($helpAlias | ForEach-Object { "$_" }) -join "`n"

  Assert-TextContains "case5 direct help output" $helpDirectText "ue-tools <command> [options]"
  Assert-TextContains "case5 alias help output" $helpAliasText "Commands:"
  Assert-Condition "case5 alias maps to function" (((Get-Alias -Name 'ue-tools').Definition) -eq "Invoke-UETools") "ue-tools alias definition is Invoke-UETools"

  Step "Case 6: Build command help works"
  $buildHelp = @(& { Invoke-UETools build --help } 2>&1 6>&1)
  $buildHelpText = ($buildHelp | ForEach-Object { "$_" }) -join "`n"
  Assert-TextContains "case6 build help usage line" $buildHelpText "Usage: ue-tools build [UnrealSync.ps1 options]"

  Step "Case 7: Unknown subcommand returns actionable error"
  $unknownThrew = $false
  $unknownMsg = ""
  try {
    Invoke-UETools banana | Out-Null
  }
  catch {
    $unknownThrew = $true
    $unknownMsg = $_.Exception.Message
  }
  Assert-Condition "case7 unknown command throws" $unknownThrew "unknown command threw as expected"
  Assert-TextContains "case7 unknown command message" $unknownMsg "Unknown ue-tools command 'banana'"
  Assert-TextContains "case7 unknown command guidance" $unknownMsg "ue-tools help"

  Step "Case 8: Build command errors clearly outside git repo"
  $nonRepoDir = Join-Path ([System.IO.Path]::GetTempPath()) ("uetools-nongit-{0}" -f $stamp)
  New-Item -ItemType Directory -Force -Path $nonRepoDir | Out-Null
  $script:ExternalTempDirs.Add($nonRepoDir) | Out-Null

  Push-Location $nonRepoDir
  try {
    Reset-LoadedAliases
    . $profileNew
    $threw = $false
    $msg = ""
    try {
      Invoke-UETools build -NoBuild | Out-Null
    }
    catch {
      $threw = $true
      $msg = $_.Exception.Message
    }
    Assert-Condition "case8 throws outside git repo" $threw "build threw as expected"
    Assert-TextContains "case8 error message" $msg "inside a git repository"
  }
  finally {
    Pop-Location
  }

  Step "Case 9: Build command errors clearly when UnrealSync script is missing"
  $missingSyncRepo = New-ScratchPath "missing-sync-repo"
  New-Item -ItemType Directory -Force -Path $missingSyncRepo | Out-Null
  & git -C $missingSyncRepo init | Out-Null

  Push-Location $missingSyncRepo
  try {
    Reset-LoadedAliases
    . $profileNew
    $threw = $false
    $msg = ""
    try {
      Invoke-UETools build -NoBuild | Out-Null
    }
    catch {
      $threw = $true
      $msg = $_.Exception.Message
    }

    Assert-Condition "case9 throws when script missing" $threw "build threw as expected"
    Assert-TextContains "case9 missing script message" $msg "UnrealSync script not found"
  }
  finally {
    Pop-Location
  }

  Step "Case 10: Build command forwards -Force and passthrough arguments"
  $forwardRepo = New-ScratchPath "forwarding-repo"
  $forwardUnrealDir = Join-Path $forwardRepo "Scripts\Unreal"
  New-Item -ItemType Directory -Force -Path $forwardUnrealDir | Out-Null
  & git -C $forwardRepo init | Out-Null

  $forwardScript = Join-Path $forwardUnrealDir "UnrealSync.ps1"
  $forwardResult = Join-Path $forwardUnrealDir "last-run.json"
  $forwardScriptBody = @'
[CmdletBinding()]
param(
  [switch]$Force,
  [switch]$NoBuild,
  [switch]$NoRegen,
  [switch]$DryRun,
  [string]$Config = "Development",
  [string]$Platform = "Win64"
)

$outPath = Join-Path (Split-Path -Parent $PSCommandPath) "last-run.json"
[pscustomobject]@{
  Force = [bool]$Force
  NoBuild = [bool]$NoBuild
  NoRegen = [bool]$NoRegen
  DryRun = [bool]$DryRun
  Config = $Config
  Platform = $Platform
} | ConvertTo-Json -Compress | Set-Content -LiteralPath $outPath -Encoding UTF8
'@
  Write-TextFileLf -Path $forwardScript -Content $forwardScriptBody

  Push-Location $forwardRepo
  try {
    Reset-LoadedAliases
    . $profileNew

    Invoke-UETools build -NoBuild -NoRegen -DryRun -Config Debug -Platform Win64 | Out-Null
    Assert-Condition "case10 explicit build wrote result" (Test-Path -LiteralPath $forwardResult) "last-run.json written"
    $payload = Get-Content -LiteralPath $forwardResult -Raw | ConvertFrom-Json
    Assert-Condition "case10 explicit build Force forwarded" ([bool]$payload.Force) "Force=true"
    Assert-Condition "case10 explicit build NoBuild forwarded" ([bool]$payload.NoBuild) "NoBuild=true"
    Assert-Condition "case10 explicit build NoRegen forwarded" ([bool]$payload.NoRegen) "NoRegen=true"
    Assert-Condition "case10 explicit build DryRun forwarded" ([bool]$payload.DryRun) "DryRun=true"
    Assert-Condition "case10 explicit build Config forwarded" ($payload.Config -eq "Debug") "Config=Debug"
    Assert-Condition "case10 explicit build Platform forwarded" ($payload.Platform -eq "Win64") "Platform=Win64"

    Remove-Item -LiteralPath $forwardResult -ErrorAction SilentlyContinue
    Invoke-UETools -NoBuild -DryRun -Config Debug -Platform Win64 | Out-Null
    Assert-Condition "case10 implicit build wrote result" (Test-Path -LiteralPath $forwardResult) "implicit build path wrote last-run.json"
  }
  finally {
    Pop-Location
  }

  Step "Summary"
  Write-Log ("PASS={0} FAIL={1} WARN={2} SKIP={3}" -f $script:PassCount, $script:FailCount, $script:WarnCount, $script:SkipCount) Cyan
  if ($script:FailCount -eq 0) {
    Write-Log "UE tools alias tests passed." Green
  }
  else {
    Write-Log "UE tools alias tests failed." Red
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
  Restore-State
  Write-Log ""
  Write-Log "Log saved: $logPath" Cyan
}

