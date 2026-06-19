# ITHelper — Proje Notları (Mimari ve Teknik Tasarım)

Bu belge ITHelper'ın iç tasarımını, kritik AD tuzaklarına verilen yanıtları ve derleme/
sağlamlık kararlarını ayrıntılandırır.

---

## 1. Hedef ve sabit kısıtlar

- **Tek dosya, bağımsız EXE** — harici çalışma zamanı/redist yok. Statik CRT (`/MT`).
- **Hiçbir GUI çerçevesi yok** — saf Win32 (User32/ComCtl32) + GDI.
- **C++20, MSVC**; derleyici `vswhere` + `vcvars64` ile bulunur.
- **Tüm arayüz metinleri ve kod yorumları Türkçe**; kaynaklar UTF-8 (`/utf-8`).

---

## 2. Genişletilebilir araç modeli

```
ITool                         ToolView
 ├─ Name()                     ├─ SetBounds(rect)
 ├─ Category()                 ├─ Show(bool)
 └─ CreateView(parent,font)    └─ Handle()
```

- **Shell** (ana pencere) `ToolRegistry::CreateTools()` ile araçları alır; kategoriye göre
  sol **TreeView**'a basar. Her ağaç düğümünün `lParam`'ı ilgili `ITool*`'tır (kategori
  düğümlerinde `0`).
- Bir araç seçildiğinde görünüm **önbelleğe alınır** (`unordered_map<ITool*, ToolView>`); aynı
  araca tekrar geçişte yeniden oluşturulmaz, yalnız `Show(true)` çağrılır.
- `WM_SIZE`'da `Layout()` sol ağacı ve seçili görünümü yeniden yerleştirir.
- **Yeni araç eklemek** = `ITool` türet + `ToolRegistry`'de tek `push_back`. Shell'de değişiklik
  gerekmez.

---

## 3. AD Sorgu motoru (`AdQuery.cpp`)

ADSI **`IDirectorySearch`** (activeds) kullanılır. WRL `ComPtr` + RAII `BstrPtr`/`VariantPtr`.

### 3.1 Bağlanma
- Domain boşsa `LDAP://rootDSE`'den `defaultNamingContext` okunur; doluysa
  `LDAP://<domain>/rootDSE`.
- Arama bağlamı `ADsOpenObject(..., ADS_SECURE_AUTHENTICATION, IID_IDirectorySearch)` ile açılır.
- Arama tercihleri: **alt ağaç kapsamı** + **sayfa boyutu 1000** (büyük üye listeleri için).

### 3.2 Bulma stratejisi
1. **Önce tam `sAMAccountName`** eşleşmesi (bilgisayarda `ad` ve `ad$` ile `cn` de denenir).
2. Bulunamazsa **joker**: `(|(sAMAccountName=*ad*)(cn=*ad*)(displayName=*ad*))`.
- Tüm kullanıcı girdileri **RFC 4515**'e göre kaçışlanır (`*`,`(`,`)`,`\`,NUL → `\XX`) →
  LDAP enjeksiyonu engellenir (`LdapEscape`, `Common.h`).

### 3.3 Getirilen bilgiler
| Tip | Alanlar |
|-----|---------|
| Kullanıcı | görünen ad, `sAMAccountName`, UPN, e-posta, açıklama, **etkin** (`userAccountControl & 0x2`), **son oturum** (`lastLogonTimestamp`) |
| Grup | `cn`, `sAMAccountName`, açıklama, **kapsam** + **güvenlik/dağıtım** (`groupType` bitleri) |
| Bilgisayar | ad, `sAMAccountName`, açıklama, etkin, son oturum |

- `lastLogonTimestamp` 64-bit FILETIME'dır → yerel tarihe çevrilir (`FileTime64ToString`).
- `groupType`: `0x2` Global, `0x4` Domain Local, `0x8` Universal; `0x80000000` Güvenlik.

### 3.4 Üyelik — KRİTİK TUZAKLAR (özellik OKUNMAZ, ARAMA yapılır)

> Bu projedeki en önemli tasarım kararı. `memberOf`/`member` **özelliklerini okumak yanlıştır**.

**Üyesi olduğu gruplar:**
```
(&(objectCategory=group)(member=<nesneDN>))
   +  birincil grup:  objectSid + primaryGroupID → grup SID'i kurup objectSid ile çözümle
```
- **Birincil grup** (ör. *Domain Users*, bilgisayarlarda *Domain Controllers*/*Domain Computers*)
  `memberOf`'ta **hiç görünmez**. `objectSid`'in son alt yetkisi (RID) `primaryGroupID` ile
  değiştirilerek grup SID'i kurulur (`SidReplaceRid`), sonra `(objectSid=<binary>)` ile çözülür.

**Grup üyeleri:**
```
(|(memberOf=<grupDN>)(primaryGroupID=<grupRID>))
```
- `member` özelliği 1500+ üyede **aralıklı/`;range=`** döner ve tek seferde boş kalır; ayrıca
  **birincil-grup üyeleri `member`'da yer almaz**. Bu yüzden ters yönlü arama kullanılır:
  `memberOf` eşleşmesi **artı** grubun RID'ine eşit `primaryGroupID`.

**İsimler büyük/küçük harf duyarsız alfabetik sıralanır** (`CompareStringOrdinal`).

### 3.5 Çapraz arama için tip
Sonuç `struct`'ında her üye/grup öğesi **ad + tespit edilen tip** taşır (`NamedEntry`). Tip
`objectClass`'tan tespit edilir; **`computer` user'dan türediği için ÖNCE** kontrol edilir →
`group` → `user` (`DetectType`).

### 3.6 İş parçacığı modeli
- Arama **bloklayıcı** olduğundan ayrı bir **worker thread**'de çalışır; thread **kendi COM
  apartman'ını** (`COINIT_MULTITHREADED`) açar.
- **COM nesnesi iş parçacıkları arası geçmez** — worker yalnız düz `AdResult` (struct) döndürür.
- Sonuç `PostMessage(WM_APP_RESULT, generation, AdResult*)` ile UI thread'ine taşınır ve orada
  `unique_ptr` ile sahiplenilir. `generation` sayacı eski sonuçları yok sayar.

### Gerçek domaine karşı doğrulama (yilmaz.local)
| Senaryo | Beklenen | Sonuç |
|---------|----------|-------|
| `Administrator` üyelikleri | *Domain Users* (birincil) dâhil | ✔ 7 grup, *Domain Users* listede |
| `Domain Users` üyeleri | birincil-grup üyeleri görünür | ✔ 24 üye (member özelliği boşken) |
| `AD25` (bilgisayar) | birincil grup *Domain Controllers* | ✔ |
| `dev` / `DEV` | sAMAccountName ≠ cn | ✔ doğru çözüldü |
| `yokboyle12345` | bulunamadı | ✔ |

---

## 4. Arayüz (`AdQueryTool.cpp`)

### 4.1 Yerleşim
- Üst alan: kontroller **alt alta** dizilir (her satır: solda etiket, sağda alan) — tip
  ComboBox, ad Edit, domain Edit, **Ara** butonu, ardından marquee ilerleme çubuğu. Böylece dar
  pencerede de tüm alanlar görünür kalır.
- Alt: **kaydırmalı sonuç kabı** (`WS_VSCROLL`) → üç bölüm (Bilgiler / Üyesi olduğu gruplar /
  Üyeler), her biri başlık + **ListView** (rapor görünümü). Bölümler tipe göre görünür/gizli.

### 4.2 Kaydırma
- Dikey kaydırma **`ScrollWindowEx` + `SW_SCROLLCHILDREN`** ile yapılır (çocuklar tek tek
  taşınmaz → artefakt bırakmaz).
- **Fare tekerleği** (ListView'lardan alt sınıflama ile kaba iletilir), **kaydırma çubuğu** ve
  **PageUp/Down/Home/End** çalışır. Büyük listeler ayrıca **kendi içlerinde** de kayar (ListView
  iç kaydırma çubuğu; bölüm yüksekliği ~14 satırla sınırlandırılır).

### 4.3 Etkileşim
- Sağ tık menüsü: Bilgiler → *Değeri kopyala* / *Satırı kopyala*; grup/üye → *Kopyala* + *Ara*.
- `Ctrl+C` seçili satır(lar)ı panoya kopyalar (`LVN_KEYDOWN`).
- **Çapraz arama**: grup listesi → daima **Grup**; üye listesi → öğenin **tespit edilmiş tipi**.
  Ad kutusuna yazılır, tip combo'ya yansır, arama tetiklenir.

### 4.4 Modern C++
- WRL `ComPtr`; `BstrPtr`/`VariantPtr` RAII; `std::optional`, `std::wstring`, `std::unique_ptr`.
  Ham `new/delete` yalnız PostMessage ile taşınan sonuçta (UI tarafında hemen `unique_ptr`'a
  sarılır).

---

## 5. Derleme / paketleme

- **`/utf-8` şart** — Türkçe geniş string literalleri (`L"Görünen ad"`) bozulmasın.
- **Manifest gömülür** (`/MANIFEST:EMBED` + `/MANIFESTINPUT:app.manifest`): Common Controls v6,
  Per-Monitor **DPI v2**, `asInvoker`. Yan `.manifest` yeterli değildir.
- **DPI**: `GetDpiForWindow` + `MulDiv` ile ölçekleme; font `SystemParametersInfoForDpi`
  (`SPI_GETNONCLIENTMETRICS`) ile DPI'ya göre üretilir; `WM_DPICHANGED`'de yeniden oluşturulur.
- **Sürüm bilgisi + ikon** (`ITHelper.rc` / `VERSIONINFO`) — boş meta veri AV sezgisellerini
  tetikler.
- **Release**: `/O1 /Os /GL` + `/LTCG /OPT:REF /OPT:ICF`; her zaman **`/MAP`** üretilir (çökme
  RVA eşlemesi için).

---

## 6. Sağlamlık / çökme koruması

- Tüm pencere yordamları ve worker **`try/catch`** ile sarılı. Pencere yordamından kaçan istisna
  `0xC000041D` (FATAL_USER_CALLBACK_EXCEPTION) çökmesine yol açar; yakalanıp `DefWindowProc`'a
  düşülür (uygulama ayakta kalır).
- **`SetUnhandledExceptionFilter`**: yakalanmayan hatada kapanmadan önce **hata kodu + modül +
  temel adres + RVA** gösteren bir pencere çıkar; `.map` ile satıra eşlenebilir.

---

## 7. Kod imzalama (`sign.ps1`)

Kendinden imzalı **kod imzalama** sertifikası üretir (yoksa), `signtool` ile **SHA256 +
RFC3161** zaman damgasıyla imzalar, açık sertifikayı `.cer` olarak dışa aktarır. Kurum içi
dağıtımda GPO ile **Güvenilen Yayıncılar** + **Güvenilen Kök**'e eklenir.

---

## 8. Bilinen sınırlar / sonraki adımlar

- Yalnız okuma (sorgu) aracıdır; nesne değiştirme yoktur.
- Kimlik doğrulama mevcut oturum bağlamıyla yapılır (`ADS_SECURE_AUTHENTICATION`); alternatif
  kimlik bilgisi girişi eklenebilir.
- İç içe (transitive) grup üyeliği gösterilmez; yalnız doğrudan üyelik + birincil grup. İstenirse
  `LDAP_MATCHING_RULE_IN_CHAIN` (`1.2.840.113556.1.4.1941`) ile genişletilebilir.
