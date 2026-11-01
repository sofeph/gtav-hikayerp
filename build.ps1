<#
  HikayeTurkRP derle + paketle + oyuna kur.

  Kullanım (proje kökünden):
    .\build.ps1                          # SDK'yı Downloads'ta arar, derler, paketler ve oyuna kurar
    .\build.ps1 -SdkZip C:\yol\sdk.zip   # SDK zip'ini belirt
    .\build.ps1 -SdkDir C:\yol\sdk       # açılmış SDK klasörünü belirt (inc\ ve lib\ içinde)
    .\build.ps1 -NoInstall               # sadece derle ve paketle
    .\build.ps1 -InstallRuntime          # Downloads'taki ScriptHookV çalışma zamanını da oyuna kur
    .\build.ps1 -NewNatives              # yeni natives.h (MISC/HUD namespace) için NEW_NATIVES tanımla
#>
param(
    [string]$SdkZip,
    [string]$SdkDir,
    [string]$CefRoot,
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto V",
    [switch]$Install,
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
$pythonHost = Join-Path $root "companion\python_browser_host.py"
if (Test-Path $pythonHost) {
    Write-Host "Python CEF host bulundu; CEF SDK kontrolü atlanıyor."
} else {
    if (-not (Test-Path (Join-Path $CefRoot "include\cef_app.h")) -or
        -not (Test-Path (Join-Path $CefRoot "libcef_dll\CMakeLists.txt")) -or
        -not (Test-Path (Join-Path $CefRoot "Release\libcef.lib"))) {
        throw "CEF SDK bulunamadı: '$CefRoot'. Önce .\tools\setup_cef.ps1 çalıştır veya -CefRoot ver."
    }
}

# 3) MSBuild'i bul ve derle
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild bulunamadı (Visual Studio 'C++ ile masaüstü geliştirme' iş yükü gerekli)." }

$props = "/p:Configuration=Release;Platform=x64;CEFRoot=`"$CefRoot`";AdditionalOptions=`"/FS %(AdditionalOptions)`"" 
if ($NewNatives) { $props += ";ExtraDefines=NEW_NATIVES" }

& $msbuild (Join-Path $root "mod\HikayeTurkRP.vcxproj") $props /t:Rebuild /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Derleme başarısız (yukarıdaki hatalara bak)." }
$asi = Join-Path $root "mod\build\HikayeTurkRP.asi"
Write-Host "Derlendi: $asi"

# 4) Paketleme (derleme sonrası otomatik ZIP üret)
$packageRoot = Join-Path $root "mod\build\package"
$packageName = "HikayeTurkRP_x64_release"
$packageDir = Join-Path $packageRoot $packageName
if (Test-Path $packageDir) { Remove-Item $packageDir -Recurse -Force }
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

Copy-Item $asi (Join-Path $packageDir "HikayeTurkRP.asi") -Force

$srcData = Join-Path $root "data"
if (Test-Path $srcData) {
    Copy-Item $srcData (Join-Path $packageDir "data") -Recurse -Force
}

$srcAddons = Join-Path $root "addons"
if (Test-Path $srcAddons) {
    Copy-Item $srcAddons (Join-Path $packageDir "addons") -Recurse -Force
}

$packageZip = Join-Path $packageRoot ("$packageName.zip")
if (Test-Path $packageZip) { Remove-Item $packageZip -Force }
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $packageZip -Force
Write-Host "Paket hazır: $packageZip"

# 5) Oyuna kur (Sadece -Install verilirse)
if ($Install) {
    $gameDir = $GameDir
    if (-not (Test-Path $gameDir)) { throw "Oyun dizini bulunamadı: $gameDir. -GameDir ile belirtin." }
    
    # ASI'yi kopyala
    $destAsi = Join-Path $gameDir "HikayeTurkRP.asi"
    Copy-Item $asi $destAsi -Force
    Write-Host "ASI kuruldu: $destAsi"
    
    # HikayeTurkRP klasörü ve veri dosyalarını kur
    $destBase = Join-Path $gameDir "HikayeTurkRP"
    if (-not (Test-Path $destBase)) {
        New-Item -ItemType Directory -Path $destBase -Force | Out-Null
    }
    
    # data klasörünü kopyala
    $srcData = Join-Path $root "data"
    $destData = Join-Path $destBase "data"
    if (Test-Path $srcData) {
        Copy-Item $srcData $destData -Recurse -Force
        Write-Host "Data klasörü kuruldu: $destData"
    }

    # addons klasörünü kopyala
    $srcAddons = Join-Path $root "addons"
    $destAddons = Join-Path $destBase "addons"
    if (Test-Path $srcAddons) {
        Copy-Item $srcAddons $destAddons -Recurse -Force
        Write-Host "Addons klasörü kuruldu: $destAddons"
    }
    
    # ScriptHookV runtime kur (InstallRuntime switch'i varsa)
    if ($InstallRuntime) {
            # SDK zip'lerini hariç tut (SDK zip'inde "SDK" geçer)
            $shvRuntime = Get-ChildItem "$env:USERPROFILE\Downloads" -Filter "ScriptHookV_*.zip" -ErrorAction SilentlyContinue | 
                Where-Object { $_.Name -notlike "*SDK*" } | 
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if ($shvRuntime) {
                $tmp = Join-Path $env:TEMP "hikaye_shv_runtime"
                if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
                Expand-Archive -Path $shvRuntime.FullName -DestinationPath $tmp -Force
                $shvFiles = Get-ChildItem $tmp -Recurse -Include "*.asi", "*.dll" -ErrorAction SilentlyContinue
                foreach ($f in $shvFiles) {
                    $dest = Join-Path $gameDir $f.Name
                    Copy-Item $f.FullName $dest -Force
                    Write-Host "ScriptHookV runtime kuruldu: $dest"
                }
            } else {
                Write-Host "UYARI: ScriptHookV runtime zip'i Downloads'ta bulunamadı, atlanıyor."
            }
        }
    
    Write-Host "Kurulum tamamlandı!"
} else {
    Write-Host "Orijinal dizine kopyalama yapilmiyor. Sadece derleme + paketleme yapildi."
}