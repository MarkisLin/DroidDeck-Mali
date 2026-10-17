param(
  [Parameter(Mandatory = $true)][string]$Apk
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dsh = Split-Path -Parent $root
Set-Location $root

$expected = "9df9e89a0b34e5f96ae8050640f86f2e15e3876030b484343dd2039574aea5bf"
$hash = (Get-FileHash $Apk -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $expected) { throw "APK SHA256 mismatch: $hash (expect $expected)" }
Write-Host "APK 校验通过: $hash"

$extract = Join-Path $root "_apk-extract"
if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
# APK 资源表里存在仅大小写不同的条目；Windows 文件系统不区分大小写，逐条覆盖式解包
$zip = [System.IO.Compression.ZipFile]::OpenRead($Apk)
try {
  foreach ($entry in $zip.Entries) {
    $dest = Join-Path $extract ($entry.FullName -replace '/', [IO.Path]::DirectorySeparatorChar)
    if ($entry.FullName.EndsWith('/')) { New-Item -ItemType Directory -Force -Path $dest | Out-Null; continue }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
  }
} finally { $zip.Dispose() }

$assets = Join-Path $root "app\src\main\assets"
$prepared = Join-Path $root "app\build\prepared-assets"
$jni = Join-Path $root "app\src\main\jniLibs\arm64-v8a"
New-Item -ItemType Directory -Force -Path $assets, $prepared, $jni | Out-Null

foreach ($tree in "linuxfs", "directaudio", "droiddeck-esync") {
  $src = Join-Path $extract "assets\$tree"
  if (-not (Test-Path $src)) { throw "APK missing assets/$tree" }
  $dst = Join-Path $assets $tree
  if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
  Copy-Item $src $dst -Recurse -Force
  Write-Host "staged assets/$tree"
}

Copy-Item (Join-Path $extract "assets\pulseaudio.tzst") (Join-Path $prepared "pulseaudio.tzst") -Force
Write-Host "staged prepared-assets (pulseaudio)"

Copy-Item (Join-Path $extract "lib\arm64-v8a\*.so") $jni -Force
Write-Host "staged jniLibs from APK"

$gsTmp = Join-Path $extract "_gs"
New-Item -ItemType Directory -Force -Path $gsTmp | Out-Null
tar -xf (Join-Path $dsh "vendor\gamescope.tzst") -C $gsTmp
$gsBin = Join-Path $gsTmp "usr\local\bin\gamescope"
if (-not (Test-Path $gsBin)) { throw "patched gamescope not found in vendor/gamescope.tzst" }
Copy-Item $gsBin (Join-Path $assets "linuxfs\usr\local\bin\gamescope") -Force
Write-Host "gamescope: 换入带 nested-without-DRM 补丁的 3.16.29 构建"

& (Join-Path $root "tools\shims\python3.cmd") (Join-Path $root "tools\release\runtime_inputs.py") record
if ($LASTEXITCODE -ne 0) { throw "runtime_inputs record failed" }
Write-Host "runtime-inputs.json 已生成"
