param(
  [string]$Version = "0.1.0-beta"
)

$ErrorActionPreference = "Stop"
Write-Host "Building Windows beta package for Imposr Pro $Version"
Write-Host "Step 1: compile renderer and main process"
Write-Host "Step 2: assemble plugin payload"
Write-Host "Step 3: sign artifact"
Write-Host "Done"
