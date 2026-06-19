// AdQueryTool.h — "AD Sorgusu" aracı (kategori: Active Directory) ve görünümü.
#pragma once

#include "ITool.h"
#include "AdQuery.h"
#include "Common.h"

#include <memory>
#include <vector>

namespace ith {

// Araç tanımı: ad + kategori + görünüm üretimi.
class AdQueryTool : public ITool {
public:
    std::wstring Name()     const override { return L"AD Sorgusu"; }
    std::wstring Category() const override { return L"Active Directory"; }
    std::unique_ptr<ToolView> CreateView(HWND parent, HFONT font) override;
};

// "AD Sorgusu" görünümü — saf Win32. Arama ayrı worker thread'de çalışır;
// sonuç PostMessage ile UI thread'ine taşınır.
class AdQueryView : public ToolView {
public:
    AdQueryView(HWND parent, HFONT font);
    ~AdQueryView() override;

    // ToolView arayüzü
    void SetBounds(const RECT& rc) override;
    void Show(bool visible) override;
    HWND Handle() const override { return root_; }

    // --- pencere yordamlarından çağrılan iç işleyiciler ---
    LRESULT RootProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT ResultsProc(HWND, UINT, WPARAM, LPARAM);

    // Çapraz arama: adı kutuya yaz, tipi seç, aramayı tetikle.
    void StartCrossSearch(const std::wstring& name, ObjectType type);

private:
    // Kayıt ve oluşturma
    static void EnsureClasses(HINSTANCE);
    void CreateControls();
    void LayoutTop(int width);
    void LayoutResults();

    // Arama akışı
    void BeginSearch();
    void OnResult(std::unique_ptr<AdResult> res);
    void SetBusy(bool busy);

    // Liste doldurma
    void FillInfo(const std::vector<InfoPair>&);
    void FillNamedList(HWND lv, const std::vector<NamedEntry>&);

    // Bağlam menüsü / kopyalama
    void OnListContextMenu(HWND lv, int ctrlId, POINT screenPt);
    void CopySelected(HWND lv, int ctrlId);
    void CopyTextToClipboard(const std::wstring&);

    // Yardımcılar
    int  ListPreferredHeight(HWND lv, size_t rows, bool hasHeader) const;
    ObjectType ComboType() const;
    void SetComboType(ObjectType);

    HINSTANCE hinst_ = nullptr;
    HFONT     font_  = nullptr;   // sahiplenilmez (Shell üretir)
    UINT      dpi_   = 96;

    // pencereler
    HWND root_       = nullptr;   // kök panel (ToolView kökü)
    HWND comboType_  = nullptr;
    HWND editName_   = nullptr;
    HWND editDomain_ = nullptr;
    HWND btnSearch_  = nullptr;
    HWND progress_   = nullptr;
    HWND results_    = nullptr;   // kaydırmalı sonuç kabı (WS_VSCROLL)

    HWND lblInfo_     = nullptr, lvInfo_     = nullptr;
    HWND lblMemberOf_ = nullptr, lvMemberOf_ = nullptr;
    HWND lblMembers_  = nullptr, lvMembers_  = nullptr;

    // sonuç verisi (çapraz arama için tip saklanır)
    std::vector<NamedEntry> memberOfData_;
    std::vector<NamedEntry> membersData_;
    std::vector<InfoPair>   infoData_;
    bool showMemberOf_ = false;
    bool showMembers_  = false;

    int  scrollY_ = 0;            // kabın dikey kaydırma konumu
    bool busy_    = false;        // arama sürüyor mu
    unsigned generation_ = 0;     // eski sonuçları yok saymak için
};

} // namespace ith
