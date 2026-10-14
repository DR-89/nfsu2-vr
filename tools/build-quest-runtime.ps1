param(
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $Ndk -or -not (Test-Path -LiteralPath (Join-Path $Ndk 'build/cmake/android.toolchain.cmake'))) {
    throw 'Pass -Ndk <Android NDK directory>, or set ANDROID_NDK_HOME.'
}
if ($Jobs -lt 1) { throw 'Jobs must be positive.' }
$ninja = Join-Path $repo '.venv/Scripts/ninja.exe'
if (-not (Test-Path -LiteralPath $ninja)) {
    throw 'Create .venv and install tools/quest-requirements.txt first.'
}
function Run-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}
$dependencies = Get-Content -LiteralPath (Join-Path $repo 'android/dependencies.json') -Raw | ConvertFrom-Json
foreach ($property in $dependencies.PSObject.Properties) {
    $dep = $property.Value
    $destination = Join-Path $repo "build/deps/$($property.Name)"
    if (-not (Test-Path -LiteralPath $destination)) {
        Run-Checked git @('clone', '--depth', '1', '--branch', $dep.tag, $dep.url, $destination)
    }
    $actual = & git -C $destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $dep.revision) { throw "Dependency revision mismatch: $destination" }
    $changes = & git -C $destination status --porcelain
    if ($LASTEXITCODE -ne 0 -or $changes) { throw "Dependency checkout has changes: $destination" }
}
$buildDir = Join-Path $repo 'build/quest-runtime'
Run-Checked cmake @('-S', (Join-Path $repo 'android/runtime'), '-B', $buildDir,
    '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$Ndk/build/cmake/android.toolchain.cmake",
    '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30',
    '-DANDROID_STL=c++_shared', '-DCMAKE_BUILD_TYPE=RelWithDebInfo')
Run-Checked cmake @('--build', $buildDir, '--target', 'quest_runtime', '--parallel', "$Jobs")
Write-Host 'Android ARM64 runtime compiled. This target does not produce a playable game or APK.'
