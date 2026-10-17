param(
  [string]$Project = "src-dd9400",
  [string]$Task = "assembleDebug",
  [string]$Log = "build-dd9400.log"
)
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
Set-Location $root

$env:JAVA_HOME = (Resolve-Path "toolchain\jdk17\jdk-17.0.13+11").Path
$env:ANDROID_HOME = (Resolve-Path "toolchain\android-sdk").Path
New-Item -ItemType Directory -Force -Path "toolchain\gh", "tools\tmp" | Out-Null
$tmp = (Resolve-Path "tools\tmp").Path
$env:TMP = $tmp
$env:TEMP = $tmp
$env:GRADLE_USER_HOME = (Resolve-Path "toolchain\gh").Path
$env:GRADLE_OPTS = "-Djava.io.tmpdir=$tmp"
$projProps = Join-Path $Project "gradle.properties"
if (Test-Path $projProps) {
  $cur = Get-Content $projProps -Raw
  if ($cur -notmatch 'java\.io\.tmpdir') {
    $cur = $cur -replace '(org\.gradle\.jvmargs=[^\r\n]+)', "`$1 -Djava.io.tmpdir=$tmp"
    Set-Content -Path $projProps -Value $cur -Encoding UTF8 -NoNewline
    Write-Host "patched $projProps with java.io.tmpdir"
  }
}
$env:PATH = "$root\$Project\tools\shims;$env:JAVA_HOME\bin;$env:PATH"

$gradle = (Resolve-Path "toolchain\gradle\gradle-8.10.2\bin\gradle.bat").Path
$ndkVersion = "30.0.16248370"

Write-Host "JAVA_HOME=$env:JAVA_HOME"
Write-Host "ANDROID_HOME=$env:ANDROID_HOME"
Write-Host "gradle=$gradle"

& $gradle "-p" $Project $Task "--no-daemon" "-PndkVersion=$ndkVersion" "-PskipRust=true" "--stacktrace" 2>&1 |
  ForEach-Object { "$_" } | Tee-Object -FilePath $Log

Write-Host "GRADLE_EXIT=$LASTEXITCODE"
exit $LASTEXITCODE
