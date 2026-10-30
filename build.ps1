<#
  HikayeTurkRP derle + oyuna kur.

  Kullanım (proje kökünden):
    .\build.ps1                          # SDK'yı Downloads'ta arar, derler, oyuna kurar
    .\build.ps1 -SdkZip C:\yol\sdk.zip   # SDK zip'ini belirt
    .\build.ps1 -SdkDir C:\yol\sdk       # açılmış SDK klasörünü belirt (inc\ ve lib\ içinde)
    .\build.ps1 -NoInstall               # sadece derle
    .\build.ps1 -InstallRuntime          # Downloads'taki ScriptHookV çalışma zamanını da oyuna kur
    .\build.ps1 -NewNatives              # yeni natives.h (MISC/HUD namespace) için NEW_NATIVES tanımla
#>
param(
    [string]$SdkZip,
    [string]$SdkDir,
    [string]$CefRoot,
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto V",
    [switch]$NoInstall,
    [switch]$InstallRuntime,
    [switch]$NewNatives
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$sdkTarget = Join-Path $root "mod\third_party\ScriptHookV_SDK"
$cefSdkTarget = Join-Path $root "mod\third_party\cef\sdk\cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48_windows64"

function Find-SdkRoot([string]$dir) {
    # inc\main.h içeren klasörü bul (zip içinde bir üst klasör olabilir)
    $main = Get-ChildItem $dir -Recurse -Filter "main.h" -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq "inc" } | Select-Object -First 1
    if ($main) { return $main.Directory.Parent.FullName }
    return $null
}

# 1) SDK'yı yerine koy
if (-not (Test-Path (Join-Path $sdkTarget "inc\main.h"))) {
    if (-not $SdkDir) {
        if (-not $SdkZip) {
            $found = Get-ChildItem "$env:USERPROFILE\Downloads" -Filter "ScriptHookV_SDK*.zip" -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if ($found) { $SdkZip = $found.FullName }
        }
        if ($SdkZip) {
            $tmp = Join-Path $env:TEMP "hikaye_sdk_extract"
            if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
            Expand-Archive -Path $SdkZip -DestinationPath $tmp -Force
            $SdkDir = $tmp
        }
    }
    if (-not $SdkDir) { throw "SDK bulunamadı. -SdkZip veya -SdkDir ver (dev-c.com/gtav/scripthookv, 'SDK' zip'i)." }
    $sdkRoot = Find-SdkRoot $SdkDir
    if (-not $sdkRoot) { throw "'$SdkDir' içinde inc\main.h bulunamadı. Bu SDK değil, çalışma zamanı zip'i olabilir." }
    New-Item -ItemType Directory -Force $sdkTarget | Out-Null
    Copy-Item (Join-Path $sdkRoot "inc") $sdkTarget -Recurse -Force
    Copy-Item (Join-Path $sdkRoot "lib") $sdkTarget -Recurse -Force
    Write-Host "SDK kopyalandı: $sdkTarget"
}
$lib = Get-ChildItem (Join-Path $sdkTarget "lib") -Filter "ScriptHookV.lib" -Recurse | Select-Object -First 1
if (-not $lib) { throw "ScriptHookV.lib bulunamadı: $sdkTarget\lib" }
if ($lib.DirectoryName -ne (Join-Path $sdkTarget "lib")) {
    Copy-Item $lib.FullName (Join-Path $sdkTarget "lib") -Force  # alt klasördeyse (ör. x64\) lib\ köküne al
}

# 2) CEF kaynak/runtime paketi
if (-not $CefRoot) { $CefRoot = $cefSdkTarget }
if (-not (Test-Path (Join-Path $CefRoot "include\cef_app.h")) -or
    -not (Test-Path (Join-Path $CefRoot "libcef_dll\CMakeLists.txt")) -or
    -not (Test-Path (Join-Path $CefRoot "Release\libcef.lib"))) {
    throw "CEF SDK bulunamadı: '$CefRoot'. Önce .\tools\setup_cef.ps1 çalıştır veya -CefRoot ver."
}

# 2) MSBuild'i bul ve derle
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild bulunamadı (Visual Studio 'C++ ile masaüstü geliştirme' iş yükü gerekli)." }
$props = "/p:Configuration=Release;Platform=x64;CEFRoot=`"$CefRoot`""
if ($NewNatives) { $props += ";ExtraDefines=NEW_NATIVES" }
$wrapperProject = Join-Path $root "mod\HikayeTurkRPWrapper.vcxproj"
& $msbuild $wrapperProject $props /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "CEF wrapper derlemesi başarısız." }
& $msbuild (Join-Path $root "mod\HikayeTurkRP.vcxproj") $props /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Derleme başarısız (yukarıdaki hatalara bak)." }
$subprocessProject = Join-Path $root "mod\HikayeTurkRPSubprocess.vcxproj"
& $msbuild $subprocessProject $props /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "CEF subprocess derlemesi başarısız." }
$cefOut = Join-Path $root "mod\build\cef"
New-Item -ItemType Directory -Force $cefOut | Out-Null
Copy-Item (Join-Path $CefRoot "Release\*") $cefOut -Recurse -Force
Copy-Item (Join-Path $CefRoot "Resources") (Join-Path $cefOut "Resources") -Recurse -Force
Copy-Item (Join-Path $CefRoot "LICENSE.txt") (Join-Path $cefOut "LICENSE.txt") -Force
Copy-Item (Join-Path $root "mod\build\HikayeTurkRPSubprocess.exe") (Join-Path $cefOut "HikayeTurkRPSubprocess.exe") -Force
$asi = Join-Path $root "mod\build\HikayeTurkRP.asi"
Write-Host "Derlendi: $asi"

# 3) Kurulum YAPILMAZ: orijinal GTA V klasörüne asla yazılmaz (FiveM mantığı).
#    Oyuna almak için:  .\package.ps1  ->  dist\HikayeTurkRP\Baslat.bat (başlatıcı mod için ayrı oyun kopyası hazırlar)
#    Hızlı geliştirme döngüsü: başlatıcıyı geliştirme modunda çalıştır:  cd launcher; npm start  (moduild\HikayeTurkRP.asi ve data\ kullanılır)
Write-Host "CEF runtime: $cefOut"
Write-Host "Kurulum yapılmadı (orijinal oyuna yazılmaz). Başlatıcı ile kur: launcher> npm start"
