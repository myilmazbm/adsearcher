// AdQuery.h — AD sorgu verileri ve worker giriş noktası.
// Worker yalnız DÜZ VERİ (struct) döndürür; COM nesnesi iş parçacıkları arası GEÇMEZ.
#pragma once

#include <string>
#include <vector>
#include <memory>

namespace ith {

// Aranan nesne tipi. Sıralama TreeView/ComboBox ile aynı.
enum class ObjectType { User = 0, Group = 1, Computer = 2 };

// Bilgiler listesinde bir satır (özellik/değer).
struct InfoPair {
    std::wstring key;
    std::wstring value;
};

// Üye / grup listesinde bir öğe — ad + tespit edilen tip (çapraz arama için).
struct NamedEntry {
    std::wstring name;
    ObjectType   type;
};

// Worker'a verilen istek.
struct AdQueryRequest {
    ObjectType   type;
    std::wstring name;     // aranan ad (sAMAccountName / cn / displayName)
    std::wstring domain;   // opsiyonel; boşsa mevcut domain (rootDSE)
};

// Worker'dan dönen sonuç. Tamamen düz veri — UI thread'ine PostMessage ile taşınır.
struct AdResult {
    bool found = false;            // nesne bulundu mu
    bool error = false;            // hata oluştu mu
    std::wstring message;          // hata / "bulunamadı" mesajı

    ObjectType   type = ObjectType::User;  // tespit edilen gerçek tip (çapraz arama için)
    std::wstring title;            // sonuç başlığı (görünen ad / cn)

    std::vector<InfoPair>    info;        // "Bilgiler"
    std::vector<NamedEntry>  memberOf;    // "Üyesi olduğu gruplar"
    std::vector<NamedEntry>  members;     // "Üyeler"

    bool showMemberOf = false;     // tipe göre görünürlük
    bool showMembers  = false;
};

// AD sorgusunu çalıştırır (BLOKLAYICI — worker thread'de çağrılmalı).
// Çağıran iş parçacığı kendi COM apartman'ını (COINIT_MULTITHREADED) açmış olmalıdır.
std::unique_ptr<AdResult> RunAdQuery(const AdQueryRequest& req);

} // namespace ith
