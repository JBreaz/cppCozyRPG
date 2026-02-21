function Write-Utf8NoBomFile {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][AllowEmptyString()][string]$Content
  )

  $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($Path, $Content, $utf8NoBom)
}

function Remove-ProfileSnippet {
  param(
    [Parameter(Mandatory)][string]$ProfilePath,
    [Parameter(Mandatory)][string]$StartMarker,
    [Parameter(Mandatory)][string]$EndMarker
  )

  if (-not (Test-Path -LiteralPath $ProfilePath)) {
    return
  }

  $existing = Get-Content -LiteralPath $ProfilePath -Raw
  $pattern = "(?s)$([regex]::Escape($StartMarker)).*?$([regex]::Escape($EndMarker))"
  $updated = [regex]::Replace($existing, $pattern, "")

  if ($updated -cne $existing) {
    Write-Utf8NoBomFile -Path $ProfilePath -Content $updated
  }
}

function Set-ProfileSnippet {
  param(
    [Parameter(Mandatory)][string]$ProfilePath,
    [Parameter(Mandatory)][string]$StartMarker,
    [Parameter(Mandatory)][string]$EndMarker,
    [Parameter(Mandatory)][string]$SnippetBody
  )

  $profileDir = Split-Path -Parent $ProfilePath
  if ($profileDir -and -not (Test-Path -LiteralPath $profileDir)) {
    New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
  }

  $existing = ""
  if (Test-Path -LiteralPath $ProfilePath) {
    $existing = Get-Content -LiteralPath $ProfilePath -Raw
  }

  $snippet = @(
    $StartMarker
    $SnippetBody.TrimEnd()
    $EndMarker
  ) -join "`r`n"

  $updated = $existing
  $pattern = "(?s)$([regex]::Escape($StartMarker)).*?$([regex]::Escape($EndMarker))"
  $regex = [regex]::new($pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)

  if ($regex.IsMatch($existing)) {
    $updated = $regex.Replace(
      $existing,
      [System.Text.RegularExpressions.MatchEvaluator] { param($m) $snippet },
      1
    )
  }
  else {
    if ($updated -and -not $updated.EndsWith("`n")) {
      $updated += "`r`n"
    }
    if ($updated) {
      $updated += "`r`n"
    }
    $updated += $snippet + "`r`n"
  }

  Write-Utf8NoBomFile -Path $ProfilePath -Content $updated
}

function Get-UEToolsAliasMarkers {
  [pscustomobject]@{
    StartMarker = "# >>> ue-tools aliases >>>"
    EndMarker   = "# <<< ue-tools aliases <<<"
  }
}

function Get-UELegacyAliasMarkers {
  @(
    [pscustomobject]@{
      StartMarker = "# >>> ue-sync aliases >>>"
      EndMarker   = "# <<< ue-sync aliases <<<"
    },
    [pscustomobject]@{
      StartMarker = "# >>> cppCozyRPG UnrealSync aliases >>>"
      EndMarker   = "# <<< cppCozyRPG UnrealSync aliases <<<"
    }
  )
}

function Get-UEToolsAliasSnippet {
@'
function Invoke-UETools {
  $helpTokens = @("help", "--help", "-help", "-h", "/?", "-?")
  $argsList = @($args)

  function Test-HelpToken([object]$Value) {
    if ($null -eq $Value) { return $false }
    $token = ([string]$Value).ToLowerInvariant()
    return ($helpTokens -contains $token)
  }

  function Show-UEToolsHelp {
    @(
      "UE tools wrapper for repository Unreal helpers."
      "Usage:"
      "  ue-tools <command> [options]"
      "Commands:"
      "  help                 Show this help text."
      "  build [sync options] Run Scripts\Unreal\UnrealSync.ps1 with -Force."
      "Examples:"
      "  ue-tools help"
      "  ue-tools build -DryRun"
      "  ue-tools build -NoBuild -Config Debug"
      "Notes:"
      "  - If the first argument starts with '-' or '/', 'build' is assumed."
      "  - Additional commands can be added under this command group later."
    ) | Write-Output
  }

  function Show-UEToolsBuildHelp {
    @(
      "Usage: ue-tools build [UnrealSync.ps1 options]"
      "Examples:"
      "  ue-tools build -DryRun"
      "  ue-tools build -NoBuild -NoRegen"
      "  ue-tools build -Config Debug -Platform Win64"
      "Notes:"
      "  - Wrapper always passes -Force to UnrealSync.ps1."
    ) | Write-Output
  }

  $command = "help"
  $commandArgs = @()

  if ($argsList.Count -gt 0) {
    $first = [string]$argsList[0]
    if (Test-HelpToken $first) {
      $command = "help"
      if ($argsList.Count -gt 1) {
        $commandArgs = @($argsList[1..($argsList.Count - 1)])
      }
    }
    elseif ($first.StartsWith("-") -or $first.StartsWith("/")) {
      $command = "build"
      $commandArgs = $argsList
    }
    else {
      $command = $first.ToLowerInvariant()
      if ($argsList.Count -gt 1) {
        $commandArgs = @($argsList[1..($argsList.Count - 1)])
      }
    }
  }

  switch ($command) {
    "help" {
      Show-UEToolsHelp
      return
    }
    "build" {
      foreach ($arg in $commandArgs) {
        if (Test-HelpToken $arg) {
          Show-UEToolsBuildHelp
          return
        }
      }

      $repoRoot = ((git rev-parse --show-toplevel 2>$null) | Select-Object -First 1)
      if ([string]::IsNullOrWhiteSpace($repoRoot)) {
        throw "Invoke-UETools must be run from inside a git repository."
      }

      $syncScript = Join-Path $repoRoot.Trim() "Scripts\Unreal\UnrealSync.ps1"
      if (-not (Test-Path -LiteralPath $syncScript)) {
        throw "UnrealSync script not found: $syncScript"
      }

      & $syncScript -Force @commandArgs
      return
    }
    default {
      throw "Unknown ue-tools command '$command'. Run 'ue-tools help'."
    }
  }
}
Set-Alias -Name ue-tools -Value Invoke-UETools -Scope Global
'@
}

function Install-UEToolsShellAliases {
  param(
    [string]$ProfilePath
  )

  if (-not $ProfilePath) {
    $ProfilePath = $PROFILE.CurrentUserAllHosts
  }
  if (-not $ProfilePath) {
    $ProfilePath = [string]$PROFILE
  }
  if (-not $ProfilePath) {
    throw "Could not resolve a PowerShell profile path for alias installation."
  }

  $markers = Get-UEToolsAliasMarkers
  $snippetBody = Get-UEToolsAliasSnippet

  foreach ($legacy in (Get-UELegacyAliasMarkers)) {
    Remove-ProfileSnippet `
      -ProfilePath $ProfilePath `
      -StartMarker $legacy.StartMarker `
      -EndMarker $legacy.EndMarker
  }

  Set-ProfileSnippet `
    -ProfilePath $ProfilePath `
    -StartMarker $markers.StartMarker `
    -EndMarker $markers.EndMarker `
    -SnippetBody $snippetBody

  [pscustomobject]@{
    ProfilePath = $ProfilePath
    FunctionName = "Invoke-UETools"
    Aliases = @("ue-tools")
    StartMarker = $markers.StartMarker
    EndMarker = $markers.EndMarker
  }
}
