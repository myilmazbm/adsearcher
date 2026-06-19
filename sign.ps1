# sign.ps1 — ITHelper.exe için kod imzalama.
# - Kendinden imzalı kod imzalama sertifikası üretir (yoksa).
# - signtool ile SHA256 + RFC3161 zaman damgasıyla imzalar.
# - İsteğe bağlı olarak açık sertifikayı (.cer) dışa aktarır
#   (kurum içinde GPO ile "Güvenilen Yayıncılar" + "Güvenilen Kök"e eklenir).
[CmdletBinding()]
param(
    [string]$Exe        = "ITHelper.exe",
    [string]$Subject    = "CN=ITHelper Kod Imzalama",
    [string]$TimeStamp  = "http://timestamp.digicert.com",
    [string]$ExportCer  = "ITHelper-Public.cer"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $root

if (-not (Test-Path $Exe)) { throw "Imzalanacak dosya yok: $Exe (once build.ps1 calistirin)." }

# 1) Mevcut sertifikayi bul ya da uret (CurrentUser\My).
$cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
        Select-Object -First 1

if (-not $cert) {
    Write-Host "Kod imzalama sertifikasi uretiliyor: $Subject" -ForegroundColor Cyan
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $Subject `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature `
        -KeyAlgorithm RSA -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(5)
} else {
    Write-Host "Mevcut sertifika kullanilacak: $($cert.Thumbprint)" -ForegroundColor Green
}

# 2) signtool yolunu bul (Windows SDK).
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) {
    # Yedek: PowerShell Set-AuthenticodeSignature
    Write-Host "signtool bulunamadi; Set-AuthenticodeSignature kullaniliyor." -ForegroundColor Yellow
    Set-AuthenticodeSignature -FilePath $Exe -Certificate $cert `
        -HashAlgorithm SHA256 -TimestampServer $TimeStamp | Out-Null
} else {
    Write-Host "signtool: $($signtool.FullName)" -ForegroundColor Cyan
    & $signtool.FullName sign /fd SHA256 /sha1 $cert.Thumbprint `
        /tr $TimeStamp /td SHA256 $Exe
    if ($LASTEXITCODE -ne 0) { throw "signtool imzalama basarisiz." }
}

# 3) Acik sertifikayi disa aktar (dagitim icin).
if ($ExportCer) {
    Export-Certificate -Cert $cert -FilePath $ExportCer -Type CERT | Out-Null
    Write-Host "Acik sertifika disa aktarildi: $ExportCer" -ForegroundColor Green
    Write-Host "  Kurum ici: GPO ile 'Guvenilen Yayincilar' + 'Guvenilen Kok Sertifika Yetkilileri'ne ekleyin." -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "Imza dogrulamasi:" -ForegroundColor Cyan
Get-AuthenticodeSignature $Exe | Format-List Status, StatusMessage, SignerCertificate
