// AdQuery.cpp — ADSI (IDirectorySearch) ile Active Directory sorgusu.
// ÖNEMLİ: üyelik bilgisi memberOf/member ÖZELLİĞİ OKUNARAK değil, ARAMA ile bulunur.
#include "AdQuery.h"
#include "Common.h"

#include <activeds.h>   // IDirectorySearch, IADs, ADsOpenObject, ADS_* sabitleri
#include <functional>

#pragma comment(lib, "activeds.lib")
#pragma comment(lib, "adsiid.lib")

namespace ith {
namespace {

// --- ADSI grup tipi bitleri (winnt.h/iads.h ile uyumlu) ---
constexpr uint32_t GT_GLOBAL        = 0x00000002;
constexpr uint32_t GT_DOMAIN_LOCAL  = 0x00000004;
constexpr uint32_t GT_UNIVERSAL     = 0x00000008;
constexpr uint32_t GT_SECURITY      = 0x80000000;

// userAccountControl: hesap devre dışı biti.
constexpr uint32_t UAC_ACCOUNTDISABLE = 0x00000002;

// Boş opsiyoneli "—" ile gösterir.
std::wstring ValOr(const std::optional<std::wstring>& v) {
    if (v && !v->empty()) return *v;
    return L"—";
}

// HRESULT'u okunur Türkçe mesaja çevirir (AD bağlam ipuçlarıyla).
std::wstring HrMessage(HRESULT hr) {
    wchar_t code[32];
    swprintf(code, 32, L" (0x%08X)", static_cast<unsigned>(hr));

    LPWSTR sys = nullptr;
    DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&sys), 0, nullptr);
    std::wstring text;
    if (n && sys) {
        text.assign(sys, n);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                                 text.back() == L' '))
            text.pop_back();
    }
    if (sys) ::LocalFree(sys);
    if (text.empty()) text = L"Active Directory'ye erişilemedi";
    return text + code;
}

// --- IDirectorySearch sütun okuyucuları -----------------------------------

std::optional<std::wstring> ColStr(IDirectorySearch* ds, ADS_SEARCH_HANDLE h,
                                   LPCWSTR attr) {
    ADS_SEARCH_COLUMN col{};
    if (FAILED(ds->GetColumn(h, const_cast<LPWSTR>(attr), &col)))
        return std::nullopt;
    std::optional<std::wstring> r;
    if (col.dwNumValues > 0 && col.pADsValues) {
        const ADSVALUE& v = col.pADsValues[0];
        switch (v.dwType) {
            case ADSTYPE_DN_STRING:          r = v.DNString;          break;
            case ADSTYPE_CASE_IGNORE_STRING: r = v.CaseIgnoreString;  break;
            case ADSTYPE_CASE_EXACT_STRING:  r = v.CaseExactString;   break;
            case ADSTYPE_PRINTABLE_STRING:   r = v.PrintableString;   break;
            default: break;
        }
    }
    ds->FreeColumn(&col);
    return r;
}

std::optional<uint32_t> ColInt(IDirectorySearch* ds, ADS_SEARCH_HANDLE h,
                               LPCWSTR attr) {
    ADS_SEARCH_COLUMN col{};
    if (FAILED(ds->GetColumn(h, const_cast<LPWSTR>(attr), &col)))
        return std::nullopt;
    std::optional<uint32_t> r;
    if (col.dwNumValues > 0 && col.pADsValues &&
        col.pADsValues[0].dwType == ADSTYPE_INTEGER) {
        r = static_cast<uint32_t>(col.pADsValues[0].Integer);
    }
    ds->FreeColumn(&col);
    return r;
}

std::optional<int64_t> ColLarge(IDirectorySearch* ds, ADS_SEARCH_HANDLE h,
                                LPCWSTR attr) {
    ADS_SEARCH_COLUMN col{};
    if (FAILED(ds->GetColumn(h, const_cast<LPWSTR>(attr), &col)))
        return std::nullopt;
    std::optional<int64_t> r;
    if (col.dwNumValues > 0 && col.pADsValues &&
        col.pADsValues[0].dwType == ADSTYPE_LARGE_INTEGER) {
        const LARGE_INTEGER& li = col.pADsValues[0].LargeInteger;
        r = (static_cast<int64_t>(li.HighPart) << 32) |
            static_cast<uint32_t>(li.LowPart);
    }
    ds->FreeColumn(&col);
    return r;
}

std::optional<std::vector<BYTE>> ColOctet(IDirectorySearch* ds, ADS_SEARCH_HANDLE h,
                                          LPCWSTR attr) {
    ADS_SEARCH_COLUMN col{};
    if (FAILED(ds->GetColumn(h, const_cast<LPWSTR>(attr), &col)))
        return std::nullopt;
    std::optional<std::vector<BYTE>> r;
    if (col.dwNumValues > 0 && col.pADsValues &&
        col.pADsValues[0].dwType == ADSTYPE_OCTET_STRING) {
        const ADS_OCTET_STRING& os = col.pADsValues[0].OctetString;
        r = std::vector<BYTE>(os.lpValue, os.lpValue + os.dwLength);
    }
    ds->FreeColumn(&col);
    return r;
}

std::vector<std::wstring> ColMultiStr(IDirectorySearch* ds, ADS_SEARCH_HANDLE h,
                                      LPCWSTR attr) {
    std::vector<std::wstring> out;
    ADS_SEARCH_COLUMN col{};
    if (FAILED(ds->GetColumn(h, const_cast<LPWSTR>(attr), &col)))
        return out;
    if (col.pADsValues) {
        for (DWORD i = 0; i < col.dwNumValues; ++i) {
            const ADSVALUE& v = col.pADsValues[i];
            if (v.dwType == ADSTYPE_CASE_IGNORE_STRING)      out.emplace_back(v.CaseIgnoreString);
            else if (v.dwType == ADSTYPE_CASE_EXACT_STRING)  out.emplace_back(v.CaseExactString);
            else if (v.dwType == ADSTYPE_DN_STRING)          out.emplace_back(v.DNString);
        }
    }
    ds->FreeColumn(&col);
    return out;
}

// --- Bağlama yardımcıları --------------------------------------------------

// "LDAP://" veya "LDAP://domain/" önekini üretir.
std::wstring ServerPrefix(const std::wstring& domain) {
    if (domain.empty()) return L"LDAP://";
    return L"LDAP://" + domain + L"/";
}

// rootDSE'den defaultNamingContext'i alır.
HRESULT GetDefaultNc(const std::wstring& prefix, std::wstring& ncOut) {
    ComPtr<IADs> rootDse;
    std::wstring path = prefix + L"rootDSE";
    HRESULT hr = ::ADsOpenObject(path.c_str(), nullptr, nullptr,
                                 ADS_SECURE_AUTHENTICATION,
                                 IID_IADs, reinterpret_cast<void**>(rootDse.GetAddressOf()));
    if (FAILED(hr)) return hr;

    VariantPtr v;
    BstrPtr prop(L"defaultNamingContext");
    hr = rootDse->Get(prop, &v);
    if (FAILED(hr)) return hr;
    if (v.type() != VT_BSTR || !v.get().bstrVal) return E_FAIL;
    ncOut = v.get().bstrVal;
    return S_OK;
}

// Verilen DN üzerinde IDirectorySearch bağlar ve alt ağaç + sayfalama tercihlerini kurar.
HRESULT BindSearch(const std::wstring& prefix, const std::wstring& nc,
                   ComPtr<IDirectorySearch>& dsOut) {
    std::wstring path = prefix + nc;
    HRESULT hr = ::ADsOpenObject(path.c_str(), nullptr, nullptr,
                                 ADS_SECURE_AUTHENTICATION,
                                 IID_IDirectorySearch,
                                 reinterpret_cast<void**>(dsOut.GetAddressOf()));
    if (FAILED(hr)) return hr;

    ADS_SEARCHPREF_INFO prefs[2]{};
    prefs[0].dwSearchPref      = ADS_SEARCHPREF_SEARCH_SCOPE;
    prefs[0].vValue.dwType     = ADSTYPE_INTEGER;
    prefs[0].vValue.Integer    = ADS_SCOPE_SUBTREE;
    prefs[1].dwSearchPref      = ADS_SEARCHPREF_PAGESIZE;   // 1500+ üye için sayfalama
    prefs[1].vValue.dwType     = ADSTYPE_INTEGER;
    prefs[1].vValue.Integer    = 1000;
    dsOut->SetSearchPreference(prefs, 2);
    return S_OK;
}

// Bir aramayı çalıştırır; her satır için geri çağırımı çağırır.
void ForEachRow(IDirectorySearch* ds, const std::wstring& filter,
                const std::vector<LPCWSTR>& attrs,
                const std::function<void(ADS_SEARCH_HANDLE)>& fn) {
    std::vector<LPWSTR> attrPtrs;
    attrPtrs.reserve(attrs.size());
    for (LPCWSTR a : attrs) attrPtrs.push_back(const_cast<LPWSTR>(a));

    ADS_SEARCH_HANDLE h = nullptr;
    HRESULT hr = ds->ExecuteSearch(const_cast<LPWSTR>(filter.c_str()),
                                   attrPtrs.data(),
                                   static_cast<DWORD>(attrPtrs.size()), &h);
    if (FAILED(hr) || !h) return;

    hr = ds->GetFirstRow(h);
    while (hr == S_OK) {
        fn(h);
        hr = ds->GetNextRow(h);
    }
    ds->CloseSearchHandle(h);
}

// Bir satırdaki en iyi görünen adı seçer: displayName > cn > name > sAMAccountName.
std::wstring BestName(IDirectorySearch* ds, ADS_SEARCH_HANDLE h) {
    if (auto v = ColStr(ds, h, L"displayName"))    if (!v->empty()) return *v;
    if (auto v = ColStr(ds, h, L"cn"))             if (!v->empty()) return *v;
    if (auto v = ColStr(ds, h, L"name"))           if (!v->empty()) return *v;
    if (auto v = ColStr(ds, h, L"sAMAccountName")) if (!v->empty()) return *v;
    return L"(adsız)";
}

// objectClass çoklu değerinden gerçek tipi tespit eder (computer ÖNCE — user'dan türer).
ObjectType DetectType(const std::vector<std::wstring>& classes) {
    auto has = [&](const wchar_t* c) {
        for (const auto& s : classes)
            if (::CompareStringOrdinal(s.c_str(), -1, c, -1, TRUE) == CSTR_EQUAL)
                return true;
        return false;
    };
    if (has(L"computer")) return ObjectType::Computer;
    if (has(L"group"))    return ObjectType::Group;
    return ObjectType::User;
}

// Bir aramadan tüm satırların ad + tipini toplar (sıralanmış değil).
// Çapraz arama için her öğenin gerçek tipi (objectClass) saklanır.
std::vector<NamedEntry> CollectNames(IDirectorySearch* ds, const std::wstring& filter) {
    std::vector<NamedEntry> out;
    const std::vector<LPCWSTR> attrs = {
        L"displayName", L"cn", L"name", L"sAMAccountName", L"objectClass"
    };
    ForEachRow(ds, filter, attrs, [&](ADS_SEARCH_HANDLE h) {
        NamedEntry ne;
        ne.name = BestName(ds, h);
        ne.type = DetectType(ColMultiStr(ds, h, L"objectClass"));
        out.push_back(std::move(ne));
    });
    return out;
}

// Tipe göre temel objectCategory/objectClass filtresi.
std::wstring TypeFilter(ObjectType t) {
    switch (t) {
        case ObjectType::User:     return L"(&(objectCategory=person)(objectClass=user))";
        case ObjectType::Group:    return L"(objectCategory=group)";
        case ObjectType::Computer: return L"(objectCategory=computer)";
    }
    return L"(objectClass=*)";
}

// Grup kapsamını groupType bitlerinden Türkçe açıklar.
std::wstring GroupScope(uint32_t gt) {
    if (gt & GT_GLOBAL)       return L"Global";
    if (gt & GT_DOMAIN_LOCAL) return L"Yerel etki alanı (Domain Local)";
    if (gt & GT_UNIVERSAL)    return L"Evrensel (Universal)";
    return L"Bilinmiyor";
}

} // anonim ad alanı

// ===========================================================================
// Ana sorgu — worker thread'de çağrılır.
// ===========================================================================
std::unique_ptr<AdResult> RunAdQuery(const AdQueryRequest& req) {
    auto res = std::make_unique<AdResult>();
    res->type = req.type;

    const std::wstring prefix = ServerPrefix(req.domain);

    // 1) defaultNamingContext.
    std::wstring nc;
    HRESULT hr = GetDefaultNc(prefix, nc);
    if (FAILED(hr)) {
        res->error = true;
        res->message = L"Etki alanına bağlanılamadı. " + HrMessage(hr) +
                       L"\nMakine bir etki alanına katılı mı? Domain alanını boş bırakıp deneyin.";
        return res;
    }

    // 2) Arama bağlamı.
    ComPtr<IDirectorySearch> ds;
    hr = BindSearch(prefix, nc, ds);
    if (FAILED(hr)) {
        res->error = true;
        res->message = L"Dizin araması açılamadı. " + HrMessage(hr);
        return res;
    }

    const std::wstring tf = TypeFilter(req.type);
    const std::wstring e  = LdapEscape(req.name);

    // 3) Önce TAM sAMAccountName eşleşmesi.
    std::wstring exact;
    if (req.type == ObjectType::Computer)
        exact = L"(&" + tf + L"(|(sAMAccountName=" + e + L")(sAMAccountName=" + e +
                L"$)(cn=" + e + L")))";
    else
        exact = L"(&" + tf + L"(sAMAccountName=" + e + L"))";

    // Bulunamazsa wildcard (*ad* / cn / displayName).
    std::wstring wild =
        L"(&" + tf + L"(|(sAMAccountName=*" + e + L"*)(cn=*" + e +
        L"*)(displayName=*" + e + L"*)))";

    const std::vector<LPCWSTR> objAttrs = {
        L"distinguishedName", L"objectClass", L"objectSid", L"primaryGroupID",
        L"sAMAccountName", L"displayName", L"cn", L"name",
        L"userPrincipalName", L"mail", L"description",
        L"userAccountControl", L"lastLogonTimestamp", L"groupType"
    };

    // Bulunan nesnenin ham verileri.
    bool found = false;
    std::wstring dn, samName, descr, displayName, cn, upn, mail;
    std::vector<std::wstring> classes;
    std::vector<BYTE> sid;
    std::optional<uint32_t> uac, primaryGroupId, groupType;
    std::optional<int64_t> lastLogon;

    auto readObject = [&](ADS_SEARCH_HANDLE h) {
        if (found) return;  // ilk satırı al
        found = true;
        if (auto v = ColStr(ds.Get(), h, L"distinguishedName")) dn = *v;
        classes = ColMultiStr(ds.Get(), h, L"objectClass");
        if (auto v = ColOctet(ds.Get(), h, L"objectSid")) sid = *v;
        primaryGroupId = ColInt(ds.Get(), h, L"primaryGroupID");
        if (auto v = ColStr(ds.Get(), h, L"sAMAccountName")) samName = *v;
        if (auto v = ColStr(ds.Get(), h, L"displayName"))    displayName = *v;
        if (auto v = ColStr(ds.Get(), h, L"cn"))             cn = *v;
        if (auto v = ColStr(ds.Get(), h, L"userPrincipalName")) upn = *v;
        if (auto v = ColStr(ds.Get(), h, L"mail"))           mail = *v;
        if (auto v = ColStr(ds.Get(), h, L"description"))    descr = *v;
        uac       = ColInt(ds.Get(), h, L"userAccountControl");
        lastLogon = ColLarge(ds.Get(), h, L"lastLogonTimestamp");
        groupType = ColInt(ds.Get(), h, L"groupType");
    };

    ForEachRow(ds.Get(), exact, objAttrs, readObject);
    if (!found)
        ForEachRow(ds.Get(), wild, objAttrs, readObject);

    if (!found) {
        res->found = false;
        res->message = L"'" + req.name + L"' için eşleşen nesne bulunamadı.";
        return res;
    }

    // 4) Gerçek tip tespiti (çapraz arama için).
    res->type = DetectType(classes);
    res->found = true;
    res->title = !displayName.empty() ? displayName : (!cn.empty() ? cn : samName);

    // 5) Bilgiler — tipe göre.
    auto& info = res->info;
    if (res->type == ObjectType::User) {
        info.push_back({ L"Görünen ad", ValOr(displayName.empty() ? std::optional<std::wstring>{} : displayName) });
        info.push_back({ L"Oturum açma adı (sAMAccountName)", ValOr(samName) });
        info.push_back({ L"Kullanıcı asıl adı (UPN)", ValOr(upn) });
        info.push_back({ L"E-posta", ValOr(mail) });
        info.push_back({ L"Açıklama", ValOr(descr) });
        info.push_back({ L"Etkin", uac ? ((*uac & UAC_ACCOUNTDISABLE) ? L"Hayır" : L"Evet") : L"—" });
        info.push_back({ L"Son oturum (replike)", lastLogon ? FileTime64ToString(*lastLogon) : L"Hiç" });
        res->showMemberOf = true;
        res->showMembers  = false;
    } else if (res->type == ObjectType::Group) {
        info.push_back({ L"Ad (cn)", ValOr(cn) });
        info.push_back({ L"Oturum açma adı (sAMAccountName)", ValOr(samName) });
        info.push_back({ L"Açıklama", ValOr(descr) });
        info.push_back({ L"Kapsam", groupType ? GroupScope(*groupType) : L"—" });
        info.push_back({ L"Tür", groupType ? ((*groupType & GT_SECURITY) ? L"Güvenlik" : L"Dağıtım") : L"—" });
        res->showMemberOf = true;   // gruplar başka gruplara üye olabilir
        res->showMembers  = true;
    } else { // Computer
        info.push_back({ L"Ad", ValOr(!cn.empty() ? std::optional<std::wstring>(cn)
                                                   : (displayName.empty() ? std::optional<std::wstring>{} : displayName)) });
        info.push_back({ L"Oturum açma adı (sAMAccountName)", ValOr(samName) });
        info.push_back({ L"Açıklama", ValOr(descr) });
        info.push_back({ L"Etkin", uac ? ((*uac & UAC_ACCOUNTDISABLE) ? L"Hayır" : L"Evet") : L"—" });
        info.push_back({ L"Son oturum (replike)", lastLogon ? FileTime64ToString(*lastLogon) : L"Hiç" });
        res->showMemberOf = true;
        res->showMembers  = false;
    }

    // 6) Üyesi olduğu gruplar — ARAMA ile (memberOf özelliği OKUNMAZ).
    if (res->showMemberOf && !dn.empty()) {
        std::wstring dnEsc = LdapEscape(dn);
        std::wstring f = L"(&(objectCategory=group)(member=" + dnEsc + L"))";
        res->memberOf = CollectNames(ds.Get(), f);

        // ARTI birincil grup (ör. "Domain Users") — memberOf'ta HİÇ görünmez.
        if (!sid.empty() && primaryGroupId) {
            if (auto pgSid = SidReplaceRid(sid, *primaryGroupId)) {
                std::wstring sidFilter = BytesToLdapFilter(*pgSid);
                std::wstring pgf = L"(&(objectCategory=group)(objectSid=" + sidFilter + L"))";
                auto pg = CollectNames(ds.Get(), pgf);
                for (auto& g : pg) res->memberOf.push_back(std::move(g));
            }
        }
        std::sort(res->memberOf.begin(), res->memberOf.end(),
                  [](const NamedEntry& a, const NamedEntry& b) { return LessNoCase(a.name, b.name); });
    }

    // 7) Üyeler (yalnız grup) — ARAMA ile.
    //    (|(memberOf=<grupDN>)(primaryGroupID=<RID>)) — member özelliği 1500+ üyede
    //    aralıklı döner; birincil-grup üyeleri member'da YER ALMAZ.
    if (res->showMembers && !dn.empty()) {
        std::wstring dnEsc = LdapEscape(dn);
        std::wstring f = L"(memberOf=" + dnEsc + L")";
        if (auto rid = SidRid(sid)) {
            wchar_t ridStr[16];
            swprintf(ridStr, 16, L"%u", *rid);
            f = L"(|(memberOf=" + dnEsc + L")(primaryGroupID=" + ridStr + L"))";
        }
        res->members = CollectNames(ds.Get(), f);
        std::sort(res->members.begin(), res->members.end(),
                  [](const NamedEntry& a, const NamedEntry& b) { return LessNoCase(a.name, b.name); });
    }

    return res;
}

} // namespace ith
