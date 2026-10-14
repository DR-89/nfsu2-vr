param(
    [string]$VulkanHeaders = '',
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$pcvrRoot = Split-Path -Parent $PSScriptRoot
$pcvrBuild = Join-Path $pcvrRoot 'build/pcvr'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ x64 tools are required.' }
$vcvars = Join-Path $vsPath 'VC/Auxiliary/Build/vcvars64.bat'
& $env:ComSpec /d /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$ninjaDir = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:PATH = "$ninjaDir;$env:PATH"
if (-not $VulkanHeaders -and $env:VULKAN_SDK) { $VulkanHeaders = Join-Path $env:VULKAN_SDK 'Include' }
if (-not (Test-Path (Join-Path $VulkanHeaders 'vulkan/vulkan.h'))) { throw 'Pass -VulkanHeaders with the Vulkan SDK or NDK header directory.' }
$vulkanPc = Join-Path $pcvrRoot 'build/deps/vulkan-pc'
New-Item -ItemType Directory -Force (Join-Path $vulkanPc 'include') | Out-Null
foreach ($directory in @('vulkan','vk_video')) {
    Copy-Item -LiteralPath (Join-Path $VulkanHeaders $directory) -Destination (Join-Path $vulkanPc 'include') -Recurse -Force
}
# Use the installed graphics driver's loader, without redistributing its DLL.
$exports = & dumpbin.exe /nologo /exports "$env:WINDIR/System32/vulkan-1.dll"
if ($LASTEXITCODE) { throw 'The Windows Vulkan loader is unavailable.' }
$names = @($exports | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(vk\w+)') { $Matches[1] } })
if ($names.Count -lt 100) { throw 'Unable to read Vulkan loader exports.' }
@('LIBRARY vulkan-1.dll','EXPORTS') + $names | Set-Content -Encoding ascii (Join-Path $vulkanPc 'vulkan-1.def')
& lib.exe /nologo /machine:x64 "/def:$vulkanPc/vulkan-1.def" "/out:$vulkanPc/vulkan-1.lib"
if ($LASTEXITCODE) { throw 'Vulkan import library generation failed.' }
& cmake.exe -S "$pcvrRoot/pcvr" -B $pcvrBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DVulkan_INCLUDE_DIR=$vulkanPc/include" "-DVulkan_LIBRARY=$vulkanPc/vulkan-1.lib"
if ($LASTEXITCODE) { throw 'PCVR configuration failed.' }
& cmake.exe --build $pcvrBuild --target pcvr_bundle --parallel $Jobs
if ($LASTEXITCODE) { throw 'PCVR build failed.' }
Write-Output "PCVR executable: $pcvrBuild/game/nfsu2-pcvr.exe"
