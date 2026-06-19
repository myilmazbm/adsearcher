# ITHelper

Active Directory'de **kullanıcı, grup ve bilgisayar** sorgulayan, tek dosyalık bağımsız
Windows masaüstü aracı. **Saf Win32 + Modern C++20**, hiçbir GUI çerçevesi (WinUI/Qt/wx) ve
harici çalışma zamanı/redist olmadan derlenir.

![tek exe](https://img.shields.io/badge/EXE-tek%20dosya-blue) ![win32](https://img.shields.io/badge/UI-saf%20Win32-green) ![c++20](https://img.shields.io/badge/C%2B%2B-20-orange)

## Özellikler

- **Genişletilebilir araç modeli**: sol TreeView'da kategoriye göre araçlar; seçilen aracın
  görünümü önbelleğe alınır. Yeni araç eklemek = `ToolRegistry`'de tek `push_back`.
- **AD Sorgusu** aracı:
  - Nesne tipi (Kullanıcı / Grup / Bilgisayar), ad ve opsiyonel domain ile arama.
  - Önce tam `sAMAccountName`, bulunamazsa joker (`*ad*` / `cn` / `displayName`).
  - **Üyelikler aramayla bulunur** (özellik okunmaz): birincil grup (ör. *Domain Users*) ve
    1500+ üyeli gruplar dâhil doğru sonuç.
  - Kaydırılabilir sonuç paneli, sağ tık menüsü (kopyala), `Ctrl+C`, **çapraz arama**.
  - Arama ayrı worker thread'de; arayüz donmaz, marquee ilerleme çubuğu.
- **Sağlamlık**: pencere yordamları ve worker `try/catch` ile sarılı; üst düzey SEH filtresi
  hata kodu + modül + RVA gösterir (`.map` ile eşlenebilir).
- **Dağıtım dostu**: gömülü manifest (Common Controls v6 + Per-Monitor DPI v2 + asInvoker),
  sürüm bilgisi, ikon, statik CRT.

## Derleme

Gereksinim: **Visual Studio 2022/2026 + "Masaüstü C++" iş yükü** (MSVC, Windows SDK).

```powershell
# Release (varsayılan)
.\build.ps1

# Debug
.\build.ps1 -DebugBuild
```

`build.ps1` zinciri: `vswhere` → `vcvars64` → `rc` → `cl` → `link`. Çıktı: `ITHelper.exe`
(+ çökme çözümü için `ITHelper.map`).

Alternatif (CMake):

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Kod imzalama (opsiyonel)

```powershell
.\sign.ps1
```

Kendinden imzalı kod imzalama sertifikası üretir (yoksa), `signtool` ile SHA256 + RFC3161
zaman damgasıyla imzalar ve açık sertifikayı (`ITHelper-Public.cer`) dışa aktarır. Kurum içi
dağıtımda bu sertifika GPO ile **Güvenilen Yayıncılar** + **Güvenilen Kök**'e eklenir.

## Kullanım

1. Sol ağaçtan **Active Directory → AD Sorgusu**'nu seçin.
2. Nesne tipini seçin, adı yazın (domain alanı boşsa mevcut etki alanı kullanılır).
3. **Ara**'ya basın veya `Enter`.
4. Sonuçta **Bilgiler**, **Üyesi olduğu gruplar** ve (grup için) **Üyeler** görünür.
   - Listede sağ tık → **Kopyala** / **Değeri kopyala** / **Satırı kopyala**.
   - Grup/üye listesinde sağ tık → **Ara** ile çapraz arama (tip otomatik seçilir).

Ayrıntılı mimari ve teknik notlar için **[PROJE.md](PROJE.md)**.

## Dosya düzeni

| Dosya | İçerik |
|-------|--------|
| `Common.h` | RAII (`BstrPtr`/`VariantPtr`/`ComApartment`), DPI, RFC 4515 kaçış, SID yardımcıları |
| `ITool.h` | `ITool` / `ToolView` arayüzleri |
| `AdQuery.h/.cpp` | ADSI `IDirectorySearch` ile AD sorgu motoru (worker) |
| `AdQueryTool.h/.cpp` | "AD Sorgusu" arayüzü (kaydırma, menü, çapraz arama) |
| `Shell.h/.cpp` | Ana pencere: TreeView + önbellekli içerik alanı |
| `ToolRegistry.h` | Araç listesi |
| `main.cpp` | `wWinMain`, SEH filtresi, Common Controls |
| `app.manifest`, `ITHelper.rc`, `resource.h` | Manifest, sürüm bilgisi, ikon |
| `build.ps1`, `sign.ps1`, `CMakeLists.txt` | Derleme ve imzalama |
