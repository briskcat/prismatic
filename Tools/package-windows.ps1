# Builds Prism FX's VST3 for 64-bit Windows and zips it for sharing.
#   pwsh Tools/package-windows.ps1     -> dist/Prism-FX-Windows.zip
# Run from a Visual Studio developer shell, or anywhere CMake can find Visual Studio.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(PrismFX VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$build = "build-release"

cmake -B $build -G "Visual Studio 17 2022" -A x64 -DPRISM_LTO=ON -DPRISM_INSTALL_PLUGINS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build --config Release --target PrismFX_VST3 --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$stage = "dist/Prism FX $version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path "$stage/Licenses" | Out-Null
Copy-Item -Recurse "$build/PrismFX_artefacts/Release/VST3/Prism FX.vst3" "$stage/"

# the install notes point at this repo's GitHub page, if it has one
$repo = (git remote get-url origin 2>$null) -replace '\.git$', '' -replace '^git@github.com:', 'https://github.com/'
$notes = Get-Content Tools/INSTALL-Windows.txt
if ($repo) { $notes = $notes -replace 'REPO_URL', $repo } else { $notes = $notes | Where-Object { $_ -notmatch 'REPO_URL' } }
$notes | Set-Content "$stage/INSTALL.txt"

Copy-Item LICENSE "$stage/Licenses/LICENSE (AGPLv3).txt"
Copy-Item NOTICE.md, TRADEMARKS.md "$stage/Licenses/"
Copy-Item Assets/fonts/OFL-*.txt "$stage/Licenses/"

$zip = "dist/Prism-FX-Windows.zip"  # the same name every release, so "latest" download links keep working
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Output $zip
