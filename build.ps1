# build.ps1 — ITHelper tek komutluk derleme (vswhere -> vcvars64 -> rc -> cl -> link).
# Kullanım:
#   .\build.ps1              # Release (varsayılan)
#   .\build.ps1 -DebugBuild  # Debug
[CmdletBinding()]
param(
    [switch]$DebugBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $root

# 1) vswhere ile Visual Studio'yu bul
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere bulunamadi: $vswhere" }

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) { throw "MSVC araclari (VC.Tools.x86.x64) yuklu degil." }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat bulunamadi: $vcvars" }

# 2) vcvars64 ortamini bu PowerShell oturumuna aktar
Write-Host "vcvars64 ortami yukleniyor..." -ForegroundColor Cyan
$envDump = & cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2]
    }
}

# 3) Derleyici bayraklari
$common = @('/nologo','/c','/W4','/EHsc','/std:c++20','/utf-8',
            '/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00')

if ($DebugBuild) {
    $cfg     = @('/Od','/Zi','/MTd','/D_DEBUG')
    $linkCfg = @('/DEBUG')
    Write-Host "Yapilandirma: DEBUG" -ForegroundColor Yellow
} else {
    $cfg     = @('/O1','/Os','/GL','/MT','/DNDEBUG')
    $linkCfg = @('/LTCG','/OPT:REF','/OPT:ICF')
    Write-Host "Yapilandirma: RELEASE" -ForegroundColor Green
}

$sources = @('main.cpp','Shell.cpp','AdQuery.cpp','AdQueryTool.cpp')

# 4) Kaynak kod imzasi (rc)
Write-Host "rc: ITHelper.rc -> ITHelper.res" -ForegroundColor Cyan
& rc /nologo /fo ITHelper.res ITHelper.rc
if ($LASTEXITCODE -ne 0) { throw "rc basarisiz." }

# 5) Derleme (cl)
Write-Host "cl: kaynaklar derleniyor..." -ForegroundColor Cyan
& cl @common @cfg @sources
if ($LASTEXITCODE -ne 0) { throw "cl basarisiz." }

# 6) Baglama (link) — manifesti GOM, .map uret
$objs = $sources | ForEach-Object { [System.IO.Path]::ChangeExtension($_, 'obj') }
$libs = @('activeds.lib','adsiid.lib','comctl32.lib','ole32.lib','oleaut32.lib',
          'user32.lib','gdi32.lib','advapi32.lib','shell32.lib')

$linkArgs = @('/nologo','/SUBSYSTEM:WINDOWS','/OUT:ITHelper.exe',
              '/MANIFEST:EMBED','/MANIFESTINPUT:app.manifest',
              '/MAP:ITHelper.map') + $linkCfg + $objs + @('ITHelper.res') + $libs

Write-Host "link: ITHelper.exe olusturuluyor..." -ForegroundColor Cyan
& link @linkArgs
if ($LASTEXITCODE -ne 0) { throw "link basarisiz." }

Write-Host ""
Write-Host "TAMAM -> $root\ITHelper.exe" -ForegroundColor Green
Get-Item ITHelper.exe | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
