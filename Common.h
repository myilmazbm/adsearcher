// Common.h — ortak yardımcılar: RAII sarmalayıcılar (BSTR/VARIANT/ComPtr),
// DPI ölçekleme, RFC 4515 LDAP kaçışı, SID yardımcıları, string araçları.
// Tüm yorumlar ve kullanıcıya dönük metinler Türkçe.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <oleauto.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <cstdint>

// WRL akıllı COM işaretçisi — projede tek ComPtr kaynağı.
using Microsoft::WRL::ComPtr;

namespace ith {

// ---------------------------------------------------------------------------
// BSTR için RAII sarmalayıcı. Kopyalanamaz, taşınabilir.
// ---------------------------------------------------------------------------
class BstrPtr {
public:
    BstrPtr() noexcept = default;
    explicit BstrPtr(const wchar_t* s) : bstr_(s ? ::SysAllocString(s) : nullptr) {}
    explicit BstrPtr(const std::wstring& s) : bstr_(::SysAllocString(s.c_str())) {}
    ~BstrPtr() { free(); }

    BstrPtr(const BstrPtr&) = delete;
    BstrPtr& operator=(const BstrPtr&) = delete;

    BstrPtr(BstrPtr&& o) noexcept : bstr_(o.bstr_) { o.bstr_ = nullptr; }
    BstrPtr& operator=(BstrPtr&& o) noexcept {
        if (this != &o) { free(); bstr_ = o.bstr_; o.bstr_ = nullptr; }
        return *this;
    }

    BSTR  get() const noexcept { return bstr_; }
    operator BSTR() const noexcept { return bstr_; }      // ADSI API'leri için kolaylık

private:
    void free() noexcept { if (bstr_) { ::SysFreeString(bstr_); bstr_ = nullptr; } }
    BSTR bstr_ = nullptr;
};

// ---------------------------------------------------------------------------
// VARIANT için RAII sarmalayıcı. İnşada Init, yıkımda Clear.
// ---------------------------------------------------------------------------
class VariantPtr {
public:
    VariantPtr() noexcept { ::VariantInit(&v_); }
    ~VariantPtr() { ::VariantClear(&v_); }

    VariantPtr(const VariantPtr&) = delete;
    VariantPtr& operator=(const VariantPtr&) = delete;

    VARIANT*       operator&()       noexcept { return &v_; }
    const VARIANT& get()       const noexcept { return v_; }
    VARTYPE        type()      const noexcept { return v_.vt; }

    void reset() noexcept { ::VariantClear(&v_); ::VariantInit(&v_); }

private:
    VARIANT v_;
};

// ---------------------------------------------------------------------------
// CoInitializeEx / CoUninitialize için RAII (her iş parçacığı kendi apartman'ı).
// ---------------------------------------------------------------------------
class ComApartment {
public:
    explicit ComApartment(DWORD coInit) noexcept {
        hr_ = ::CoInitializeEx(nullptr, coInit);
        ok_ = SUCCEEDED(hr_);
    }
    ~ComApartment() { if (ok_) ::CoUninitialize(); }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    bool    ok() const noexcept { return ok_; }
    HRESULT hr() const noexcept { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
    bool    ok_ = false;
};

// ---------------------------------------------------------------------------
// RFC 4515 — LDAP arama filtresi kaçışı. *, (, ), \, NUL → \XX.
// LDAP enjeksiyonunu engeller.
// ---------------------------------------------------------------------------
inline std::wstring LdapEscape(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size() + 8);
    for (wchar_t c : in) {
        switch (c) {
            case L'*':  out += L"\\2a"; break;
            case L'(':  out += L"\\28"; break;
            case L')':  out += L"\\29"; break;
            case L'\\': out += L"\\5c"; break;
            case L'\0': out += L"\\00"; break;
            default:    out += c;       break;
        }
    }
    return out;
}

// Bir bayt dizisini (ör. SID/GUID) LDAP filtresi için \XX\XX... biçimine çevirir.
inline std::wstring BytesToLdapFilter(const std::vector<BYTE>& bytes) {
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring out;
    out.reserve(bytes.size() * 3);
    for (BYTE b : bytes) {
        out += L'\\';
        out += hex[(b >> 4) & 0xF];
        out += hex[b & 0xF];
    }
    return out;
}

// ---------------------------------------------------------------------------
// SID yardımcıları (saf bayt düzeyinde — bağımlılık yok).
// SID düzeni: [0]=revizyon, [1]=alt yetki sayısı, [2..7]=kimlik yetkisi,
// ardından her biri 4 bayt (little-endian) alt yetkiler. Son alt yetki = RID.
// ---------------------------------------------------------------------------

// SID'in son alt yetkisini (RID) döndürür.
inline std::optional<uint32_t> SidRid(const std::vector<BYTE>& sid) {
    if (sid.size() < 8) return std::nullopt;
    BYTE count = sid[1];
    if (count == 0) return std::nullopt;
    size_t off = 8 + static_cast<size_t>(count - 1) * 4;
    if (off + 4 > sid.size()) return std::nullopt;
    uint32_t rid = static_cast<uint32_t>(sid[off]) |
                   (static_cast<uint32_t>(sid[off + 1]) << 8) |
                   (static_cast<uint32_t>(sid[off + 2]) << 16) |
                   (static_cast<uint32_t>(sid[off + 3]) << 24);
    return rid;
}

// Nesnenin SID'ini temel alarak son RID'i verilen değerle değiştirir
// (birincil grup SID'ini kurmak için: alan SID'i aynı, yalnız RID değişir).
inline std::optional<std::vector<BYTE>> SidReplaceRid(const std::vector<BYTE>& sid,
                                                      uint32_t newRid) {
    if (sid.size() < 8) return std::nullopt;
    BYTE count = sid[1];
    if (count == 0) return std::nullopt;
    size_t off = 8 + static_cast<size_t>(count - 1) * 4;
    if (off + 4 > sid.size()) return std::nullopt;
    std::vector<BYTE> out = sid;
    out[off]     = static_cast<BYTE>(newRid & 0xFF);
    out[off + 1] = static_cast<BYTE>((newRid >> 8) & 0xFF);
    out[off + 2] = static_cast<BYTE>((newRid >> 16) & 0xFF);
    out[off + 3] = static_cast<BYTE>((newRid >> 24) & 0xFF);
    return out;
}

// ---------------------------------------------------------------------------
// Büyük/küçük harf duyarsız string karşılaştırma (sıralama için).
// ---------------------------------------------------------------------------
inline bool LessNoCase(const std::wstring& a, const std::wstring& b) {
    return ::CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
}

// Bir wstring vektörünü alfabetik (harf duyarsız) sıralar.
inline void SortNoCase(std::vector<std::wstring>& v) {
    std::sort(v.begin(), v.end(), LessNoCase);
}

// ---------------------------------------------------------------------------
// FILETIME tabanlı 64-bit zaman damgasını (lastLogonTimestamp) okunur metne çevirir.
// 0 / geçersiz → "Hiç".
// ---------------------------------------------------------------------------
inline std::wstring FileTime64ToString(int64_t raw) {
    if (raw <= 0) return L"Hiç";
    FILETIME ft;
    ft.dwLowDateTime  = static_cast<DWORD>(raw & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>((raw >> 32) & 0xFFFFFFFF);
    FILETIME local{};
    SYSTEMTIME st{};
    if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
        return L"Hiç";
    wchar_t buf[64];
    swprintf(buf, 64, L"%02d.%02d.%04d %02d:%02d:%02d",
             st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------------------
// DPI'ya göre ölçekleme (96 = %100). MulDiv ile.
// ---------------------------------------------------------------------------
inline int Scale(int value, UINT dpi) {
    return ::MulDiv(value, static_cast<int>(dpi), 96);
}

} // namespace ith
