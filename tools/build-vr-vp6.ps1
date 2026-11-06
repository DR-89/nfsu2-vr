param([Parameter(Mandatory)][ValidateSet('pcvr','quest')][string]$Target,
      [string]$Ndk, [int]$Jobs=4)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$ffmpeg=(Get-Content -Raw -LiteralPath (Join-Path $repo 'android/dependencies.json') | ConvertFrom-Json).FFmpeg
$revision=$ffmpeg.revision
$src=Join-Path $repo 'build/deps/FFmpeg'
$work=New-Item -ItemType Directory -Force (Join-Path $repo "build/ffmpeg-$Target")
$toolsDir=New-Item -ItemType Directory -Force (Join-Path $repo 'build/ffmpeg-tools')
$gitRoot=(git --exec-path) -replace '[/\\]mingw64[/\\]libexec[/\\]git-core$',''
$usr=Join-Path $gitRoot 'usr/bin'
$bash=Join-Path $usr 'bash.exe'
if(!(Test-Path -LiteralPath $bash)){throw 'The VP6 build needs Git for Windows with Bash.'}
$options=@('--disable-everything','--enable-decoder=vp6,h264,aac,mp3float,flac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_u8,pcm_f32le',
    '--enable-demuxer=mp3,wav,flac,mov','--enable-parser=mpegaudio,flac,h264,aac','--enable-protocol=file','--disable-avdevice',
    '--disable-avfilter','--enable-swscale','--disable-programs','--disable-doc',
    '--disable-network','--disable-pthreads','--disable-debug','--disable-autodetect','--enable-static',
    '--disable-shared','--enable-pic')
if($Target -eq 'pcvr'){
    if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)){throw 'Use tools/build-pcvr.ps1 or an x64 VS developer shell.'}
    $toolId=$env:VCToolsVersion
    $options+=@('--toolchain=msvc','--arch=x86_64','--disable-x86asm','--extra-cflags=-MD')
}else{
    $toolBin=(Resolve-Path -LiteralPath (Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin')).Path
    $toolId=(Get-Content (Join-Path $Ndk 'source.properties') -Raw).Trim()
    $options+=@('--enable-cross-compile','--target-os=android','--arch=aarch64',
        '--cc=clang --target=aarch64-linux-android30','--cxx=clang++ --target=aarch64-linux-android30',
        '--host-cc=clang --target=x86_64-pc-windows-msvc','--ar=llvm-ar','--ranlib=llvm-ranlib',
        '--nm=llvm-nm','--strip=llvm-strip')
}
$stamp=Join-Path $work 'vp6-build.txt'
$recipe="2 | $revision | $toolId | $($options -join ' ')"

# Both platforms use the same read-only sources and Git Bash's MSYS runtime.
$lock=[Threading.Mutex]::new($false,'Local\NFSU2VR-VP6-Build')
$oldPath=$env:Path
$oldInclude=$env:INCLUDE
$oldLib=$env:LIB
$held=$false
try {
    try{$held=$lock.WaitOne()}catch [Threading.AbandonedMutexException]{$held=$true}
    if((Test-Path $stamp) -and (Get-Content $stamp -Raw).Trim() -eq $recipe -and
       (Test-Path "$work/libavcodec/libavcodec.a") -and (Test-Path "$work/libavutil/libavutil.a") -and
       (Test-Path "$work/libavformat/libavformat.a") -and (Test-Path "$work/libswresample/libswresample.a") -and
       (Test-Path "$work/libswscale/libswscale.a") -and
       (Test-Path "$work/src/libavcodec/avcodec.h")){return}
    if(Test-Path -LiteralPath $stamp){Remove-Item -LiteralPath $stamp}
    if(!(Test-Path $src)){
        & git clone --quiet --config core.autocrlf=false --depth 1 --branch $ffmpeg.tag $ffmpeg.url $src
        if($LASTEXITCODE){throw 'FFmpeg checkout failed.'}
    }
    if((git -C $src rev-parse HEAD) -ne $revision){throw "Unexpected FFmpeg revision in $src"}
    $make=Join-Path $toolsDir 'usr/bin/make.exe'
    if(!(Test-Path $make)){
        $archive=Join-Path $toolsDir 'make-4.4.1-3-x86_64.pkg.tar.zst'
        Invoke-WebRequest 'https://repo.msys2.org/msys/x86_64/make-4.4.1-3-x86_64.pkg.tar.zst' -OutFile $archive
        if((Get-FileHash $archive).Hash -ne 'AF0BDBA17F06FE037F0194069ADAA31A8FE45F1A11381501896AEA1FAE37BD5D'){
            throw 'GNU Make archive checksum mismatch.'
        }
        & tar -xf $archive -C $toolsDir usr/bin/make.exe
        if($LASTEXITCODE){throw 'GNU Make extraction failed.'}
    }
    # FFmpeg supports a relative "src" out-of-tree build inside a path with spaces.
    if(!(Test-Path "$work/src")){New-Item -ItemType Junction -Path "$work/src" -Target $src | Out-Null}
    if($Target -eq 'quest' -and !$env:INCLUDE){
        # Configure also builds Windows host probes. The NDK Clang needs the
        # host's UCRT headers and libraries when it targets Windows.
        $vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if(!$vs){throw 'Install Visual Studio C++ x64 Build Tools and the Windows SDK for the VP6 host compiler.'}
        $vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
        & $env:ComSpec /d /c "`"$vcvars`" >nul && set" | ForEach-Object {
            if($_ -match '^(INCLUDE|LIB|PATH)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}
        }
    }
    $env:Path="$(Split-Path $make);$usr;$toolBin;$env:Path"
    Push-Location $work
    try {
        & $bash src/configure @options *> configure.log
        if($LASTEXITCODE){Get-Content configure.log -Tail 12;throw "VP6 configure failed. See $work/ffbuild/config.log"}
        # MSVC's generated .d files can be empty on a localized toolchain.
        # Rebuild the codec/format registries whenever the recipe changes.
        & $make clean *> clean.log
        if($LASTEXITCODE){throw "FFmpeg clean failed. See $work/clean.log"}
        & $make "-j$Jobs" *> make.log
        if($LASTEXITCODE){Get-Content make.log -Tail 15;throw "VP6 build failed. See $work/make.log"}
        Set-Content -LiteralPath $stamp -Value $recipe
    }finally{Pop-Location}
}finally{
    $env:Path=$oldPath
    $env:INCLUDE=$oldInclude
    $env:LIB=$oldLib
    if($held){$lock.ReleaseMutex()}
    $lock.Dispose()
}
