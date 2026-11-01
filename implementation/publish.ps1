# Run from this repository after signing into GitHub with Git Credential Manager.
$ErrorActionPreference = 'Stop'
git push -u origin main
if ($LASTEXITCODE -ne 0) { throw 'GitHub sign-in or write access is required.' }
git push origin v1.0 v1.1
if ($LASTEXITCODE -ne 0) { throw 'Release tags could not be pushed.' }
Write-Output 'Published. Open the repository Actions tab and inspect the CI run.'
