param(
    [string]$Sdk = $env:ANDROID_HOME,
    [string]$Ndk = $env:ANDROID_NDK_HOME,
    [string]$Gradle = 'gradle',
    [string]$JavaHome = $env:JAVA_HOME,
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath "$Sdk/platforms/android-35/android.jar")) {
    throw 'Pass -Sdk with Android platform 35 and build-tools 35.0.0 installed.'
}
if (-not (Test-Path -LiteralPath "$JavaHome/bin/java.exe")) { throw 'Pass -JavaHome with JDK 17 or newer.' }
if (-not (Test-Path -LiteralPath "$repo/gen/recomp_funcs.h")) { throw 'Run tools/regen.py game first.' }
$env:JAVA_HOME = $JavaHome
$env:ANDROID_HOME = $Sdk
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe exited with $LASTEXITCODE" }
}
& "$PSScriptRoot/build-quest-runtime.ps1" -Ndk $Ndk -Jobs $Jobs
Invoke-Checked cmake @('-S', "$repo/android/runtime", '-B', "$repo/build/quest-runtime", '-DNFSU2_ANDROID_GAME=ON')
Invoke-Checked cmake @('--build', "$repo/build/quest-runtime", '--target', 'nfsu2_quest', '--parallel', "$Jobs")
$jni = "$repo/build/quest-jni/arm64-v8a"
New-Item -ItemType Directory -Force $jni | Out-Null
Copy-Item -LiteralPath "$repo/build/quest-runtime/libnfsu2_quest.so" -Destination $jni
Copy-Item -LiteralPath "$repo/build/quest-runtime/SDL/libSDL2.so" -Destination $jni
Copy-Item -LiteralPath "$repo/build/quest-runtime/openxr/src/loader/libopenxr_loader.so" -Destination $jni
Copy-Item -LiteralPath "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" -Destination $jni
Invoke-Checked "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" @('--strip-unneeded', "$jni/libnfsu2_quest.so")
Invoke-Checked "$repo/.venv/Scripts/python.exe" @("$repo/tools/collect-runtime-notices.py", '--ndk', $Ndk)
Invoke-Checked $Gradle @('-p', "$repo/android", 'assembleDebug', '--console=plain')
Write-Host "APK: $repo/android/app/build/outputs/apk/debug/app-debug.apk"
Write-Host 'Development build. Native game boot and VR functionality must be validated on hardware.'
