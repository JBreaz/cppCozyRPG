$ErrorActionPreference = "Stop"

git config --local core.hooksPath .githooks
Write-Host "Set local core.hooksPath to .githooks"

# Optional: ensure the hook exists
if (-not (Test-Path ".githooks/post-checkout")) {
  Write-Warning "Missing .githooks/post-checkout. Did you pull the repo changes?"
}

Write-Host "Done. Git Hooks are now enabled."
