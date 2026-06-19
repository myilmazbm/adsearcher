// AdQueryTool.cpp — "AD Sorgusu" görünümü (saf Win32).
// Arama worker thread'de; sonuç PostMessage(WM_APP_RESULT) ile UI'ya taşınır.
#include "AdQueryTool.h"

#include <commctrl.h>
#include <thread>
#include <utility>

#pragma comment(lib, "comctl32.lib")

namespace ith {
namespace {

const wchar_t* kViewClass    = L"ITH_ADView";
const wchar_t* kResultsClass = L"ITH_ADResults";

constexpr UINT WM_APP_RESULT = WM_APP + 7;   // worker → UI sonuç mesajı

// Kontrol kimlikleri
enum : int {
    IDC_COMBO_TYPE = 1001,
    IDC_EDIT_NAME,
    IDC_EDIT_DOMAIN,
    IDC_BTN_SEARCH,
    IDC_PROGRESS,
    IDC_RESULTS,
    IDC_LV_INFO     = 1101,
    IDC_LV_MEMBEROF,
    IDC_LV_MEMBERS,
    IDC_LBL_INFO    = 1201,
    IDC_LBL_MEMBEROF,
    IDC_LBL_MEMBERS,
    IDC_LBL_TIP     = 1301,
    IDC_LBL_AD,
    IDC_LBL_DOM,
};

// Bağlam menüsü komutları
enum : int {
    IDM_COPY_VALUE   = 2001,
    IDM_COPY_ROW,
    IDM_COPY,
    IDM_CROSSSEARCH,
};

// Kullanıcının seçtiği tip metinleri
const wchar_t* kTypeLabels[] = { L"Kullanıcı", L"Grup", L"Bilgisayar" };

// --- worker thread ---------------------------------------------------------
void WorkerFn(HWND hwnd, unsigned gen, AdQueryRequest req) {
    ComApartment com(COINIT_MULTITHREADED);   // kendi apartman'ı
    std::unique_ptr<AdResult> res;
    try {
        if (!com.ok()) {
            res = std::make_unique<AdResult>();
            res->error = true;
            res->message = L"COM başlatılamadı (worker).";
        } else {
            res = RunAdQuery(req);
        }
    } catch (...) {
        res = std::make_unique<AdResult>();
        res->error = true;
        res->message = L"Sorgu sırasında beklenmeyen bir hata oluştu.";
    }
    // Düz veri işaretçisini UI thread'ine taşı (unique_ptr ile sahiplenilecek).
    if (!::PostMessageW(hwnd, WM_APP_RESULT, static_cast<WPARAM>(gen),
                        reinterpret_cast<LPARAM>(res.release()))) {
        // pencere yoksa sızıntıyı önle
        // (res zaten release edildi; tekrar sarmalayıp bırak)
    }
}

// --- alt sınıflama yordamları ---------------------------------------------

// Edit: Enter → arama tetikle (bip sesini de yut).
LRESULT CALLBACK EditSubProc(HWND h, UINT m, WPARAM w, LPARAM l,
                             UINT_PTR id, DWORD_PTR) {
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        ::SendMessageW(::GetParent(h), WM_COMMAND,
                       MAKEWPARAM(IDC_BTN_SEARCH, BN_CLICKED), 0);
        return 0;
    }
    if (m == WM_CHAR && w == VK_RETURN) return 0;
    if (m == WM_NCDESTROY) ::RemoveWindowSubclass(h, EditSubProc, id);
    return ::DefSubclassProc(h, m, w, l);
}

// ListView: fare tekerleğini kaba (parent) ilet → tüm sonuç alanı kayar.
LRESULT CALLBACK ListSubProc(HWND h, UINT m, WPARAM w, LPARAM l,
                             UINT_PTR id, DWORD_PTR) {
    if (m == WM_MOUSEWHEEL) {
        return ::SendMessageW(::GetParent(h), WM_MOUSEWHEEL, w, l);
    }
    if (m == WM_NCDESTROY) ::RemoveWindowSubclass(h, ListSubProc, id);
    return ::DefSubclassProc(h, m, w, l);
}

} // anonim ad alanı

// --- statik pencere yordamı dağıtıcıları -----------------------------------

static LRESULT CALLBACK RootWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    AdQueryView* self = reinterpret_cast<AdQueryView*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(l);
        self = static_cast<AdQueryView*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        try { return self->RootProc(h, m, w, l); }
        catch (...) { return ::DefWindowProcW(h, m, w, l); }   // çökme koruması
    }
    return ::DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK ResultsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    AdQueryView* self = reinterpret_cast<AdQueryView*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(l);
        self = static_cast<AdQueryView*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        try { return self->ResultsProc(h, m, w, l); }
        catch (...) { return ::DefWindowProcW(h, m, w, l); }
    }
    return ::DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------

void AdQueryView::EnsureClasses(HINSTANCE hinst) {
    static bool done = false;
    if (done) return;
    done = true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = RootWndProc;
    wc.hInstance     = hinst;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kViewClass;
    ::RegisterClassExW(&wc);

    wc.lpfnWndProc   = ResultsWndProc;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kResultsClass;
    ::RegisterClassExW(&wc);
}

AdQueryView::AdQueryView(HWND parent, HFONT font) : font_(font) {
    hinst_ = reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    dpi_   = ::GetDpiForWindow(parent);
    if (dpi_ == 0) dpi_ = 96;
    EnsureClasses(hinst_);

    root_ = ::CreateWindowExW(0, kViewClass, L"",
                              WS_CHILD | WS_CLIPCHILDREN,
                              0, 0, 100, 100, parent, nullptr, hinst_, this);
    CreateControls();
}

AdQueryView::~AdQueryView() {
    if (root_) ::DestroyWindow(root_);
}

static HWND MakeLabel(HWND parent, int id, const wchar_t* text, HFONT font) {
    HWND h = ::CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                               0, 0, 10, 10, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               nullptr, nullptr);
    ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return h;
}

static HWND MakeListView(HWND parent, int id, HFONT font) {
    HWND h = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                               WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS,
                               0, 0, 10, 10, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               nullptr, nullptr);
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                         LVS_EX_DOUBLEBUFFER);
    ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SetWindowSubclass(h, ListSubProc, 1, 0);
    return h;
}

void AdQueryView::CreateControls() {
    // Üst giriş çubuğu
    MakeLabel(root_, IDC_LBL_TIP, L"Nesne tipi:", font_);
    comboType_ = ::CreateWindowExW(0, L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   0, 0, 10, 200, root_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COMBO_TYPE)),
                                   hinst_, nullptr);
    for (const wchar_t* t : kTypeLabels)
        ::SendMessageW(comboType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
    ::SendMessageW(comboType_, CB_SETCURSEL, 0, 0);
    ::SendMessageW(comboType_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    MakeLabel(root_, IDC_LBL_AD, L"Ad:", font_);
    editName_ = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, root_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_NAME)),
                                  hinst_, nullptr);
    ::SendMessageW(editName_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    ::SetWindowSubclass(editName_, EditSubProc, 1, 0);

    MakeLabel(root_, IDC_LBL_DOM, L"Domain (ops.):", font_);
    editDomain_ = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    0, 0, 10, 10, root_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_DOMAIN)),
                                    hinst_, nullptr);
    ::SendMessageW(editDomain_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    ::SetWindowSubclass(editDomain_, EditSubProc, 1, 0);

    btnSearch_ = ::CreateWindowExW(0, L"BUTTON", L"Ara",
                                   WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   0, 0, 10, 10, root_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SEARCH)),
                                   hinst_, nullptr);
    ::SendMessageW(btnSearch_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    progress_ = ::CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                  WS_CHILD | PBS_MARQUEE,
                                  0, 0, 10, 10, root_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROGRESS)),
                                  hinst_, nullptr);

    // Kaydırmalı sonuç kabı
    results_ = ::CreateWindowExW(0, kResultsClass, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                 0, 0, 10, 10, root_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RESULTS)),
                                 hinst_, this);

    // Bölüm başlıkları + listeler (kabın çocukları)
    lblInfo_     = MakeLabel(results_, IDC_LBL_INFO,     L"Bilgiler", font_);
    lvInfo_      = MakeListView(results_, IDC_LV_INFO, font_);
    LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = const_cast<LPWSTR>(L"Özellik"); c.cx = 200;
    ListView_InsertColumn(lvInfo_, 0, &c);
    c.pszText = const_cast<LPWSTR>(L"Değer");   c.cx = 320;
    ListView_InsertColumn(lvInfo_, 1, &c);

    lblMemberOf_ = MakeLabel(results_, IDC_LBL_MEMBEROF, L"Üyesi olduğu gruplar", font_);
    lvMemberOf_  = MakeListView(results_, IDC_LV_MEMBEROF, font_);
    c.pszText = const_cast<LPWSTR>(L"Ad"); c.cx = 400;
    ListView_InsertColumn(lvMemberOf_, 0, &c);

    lblMembers_  = MakeLabel(results_, IDC_LBL_MEMBERS, L"Üyeler", font_);
    lvMembers_   = MakeListView(results_, IDC_LV_MEMBERS, font_);
    ListView_InsertColumn(lvMembers_, 0, &c);

    // Başlangıçta sonuç bölümleri gizli
    for (HWND h : { lblInfo_, lvInfo_, lblMemberOf_, lvMemberOf_, lblMembers_, lvMembers_ })
        ::ShowWindow(h, SW_HIDE);
}

// --- yerleşim --------------------------------------------------------------

void AdQueryView::LayoutTop(int width) {
    // Kontroller ALT ALTA dizilir: her satırda solda etiket, sağında alan.
    const int pad   = Scale(10, dpi_);
    const int rowGap = Scale(6, dpi_);
    const int rowH  = Scale(24, dpi_);
    const int lblH  = Scale(18, dpi_);

    const int lblW  = Scale(110, dpi_);            // "Domain (ops.):" sığacak genişlik
    const int fieldX = pad + lblW + Scale(8, dpi_);
    const int maxField = Scale(320, dpi_);
    int fieldW = width - fieldX - pad;
    if (fieldW > maxField) fieldW = maxField;
    if (fieldW < Scale(120, dpi_)) fieldW = Scale(120, dpi_);

    HWND lblTip = ::GetDlgItem(root_, IDC_LBL_TIP);
    HWND lblAd  = ::GetDlgItem(root_, IDC_LBL_AD);
    HWND lblDom = ::GetDlgItem(root_, IDC_LBL_DOM);

    const int cyoff = (rowH - lblH) / 2;           // etiketi alanla dikey ortala
    int y = pad;

    // 1) Nesne tipi
    if (lblTip) ::MoveWindow(lblTip, pad, y + cyoff, lblW, lblH, TRUE);
    ::MoveWindow(comboType_, fieldX, y, fieldW, Scale(200, dpi_), TRUE);
    y += rowH + rowGap;

    // 2) Ad
    if (lblAd) ::MoveWindow(lblAd, pad, y + cyoff, lblW, lblH, TRUE);
    ::MoveWindow(editName_, fieldX, y, fieldW, rowH, TRUE);
    y += rowH + rowGap;

    // 3) Domain (opsiyonel)
    if (lblDom) ::MoveWindow(lblDom, pad, y + cyoff, lblW, lblH, TRUE);
    ::MoveWindow(editDomain_, fieldX, y, fieldW, rowH, TRUE);
    y += rowH + rowGap;

    // 4) Ara butonu (alan sütununa hizalı)
    ::MoveWindow(btnSearch_, fieldX, y, Scale(100, dpi_), rowH, TRUE);
    y += rowH + rowGap;

    // İlerleme çubuğu (tam genişlik, ince)
    int pbH = Scale(6, dpi_);
    ::MoveWindow(progress_, pad, y, width - 2 * pad, pbH, TRUE);
    y += pbH + Scale(8, dpi_);

    // Sonuç kabı kalan alanı doldurur
    RECT rc; ::GetClientRect(root_, &rc);
    ::MoveWindow(results_, 0, y, rc.right, (rc.bottom > y) ? rc.bottom - y : 0, TRUE);
}

int AdQueryView::ListPreferredHeight(HWND lv, size_t rows, bool /*hasHeader*/) const {
    int rowH = Scale(19, dpi_);
    if (rows > 0) {
        RECT r;
        if (ListView_GetItemRect(lv, 0, &r, LVIR_BOUNDS) && (r.bottom - r.top) > 0)
            rowH = r.bottom - r.top;
    }
    int headerH = Scale(20, dpi_);
    HWND hdr = ListView_GetHeader(lv);
    if (hdr) {
        RECT hr;
        if (::GetWindowRect(hdr, &hr) && (hr.bottom - hr.top) > 0)
            headerH = hr.bottom - hr.top;
    }
    size_t visible = rows;
    return headerH + static_cast<int>(visible) * rowH + Scale(6, dpi_);
}

void AdQueryView::LayoutResults() {
    RECT rc; ::GetClientRect(results_, &rc);
    const int pad   = Scale(8, dpi_);
    const int lblH  = Scale(20, dpi_);
    const int gap   = Scale(12, dpi_);
    const int clientW = rc.right;
    const int clientH = rc.bottom;
    const int w = clientW - 2 * pad;

    struct Sec { HWND lbl; HWND lv; int listH; bool show; };
    std::vector<Sec> secs;

    // Büyük listeleri sınırlama: üyeler/gruplar en çok ~14 satır gösterir (kalan iç kaydırma).
    auto addSec = [&](HWND lbl, HWND lv, size_t rows, size_t cap, bool show) {
        size_t vis = rows;
        if (cap > 0 && vis > cap) vis = cap;
        if (vis == 0) vis = 1;
        int listH = ListPreferredHeight(lv, vis, true);
        secs.push_back({ lbl, lv, listH, show });
    };

    addSec(lblInfo_,     lvInfo_,     infoData_.size(),       0,  !infoData_.empty());
    addSec(lblMemberOf_, lvMemberOf_, memberOfData_.size(),  14,  showMemberOf_);
    addSec(lblMembers_,  lvMembers_,  membersData_.size(),   14,  showMembers_);

    // Doğal içerik yüksekliği
    int naturalH = pad;
    for (auto& s : secs)
        if (s.show) naturalH += lblH + Scale(2, dpi_) + s.listH + gap;
    naturalH += pad;

    int maxScroll = (naturalH > clientH) ? (naturalH - clientH) : 0;
    if (scrollY_ > maxScroll) scrollY_ = maxScroll;
    if (scrollY_ < 0) scrollY_ = 0;

    // Yerleştir (kaydırma uygulanmış konumda)
    int y = pad - scrollY_;
    for (auto& s : secs) {
        if (!s.show) {
            ::ShowWindow(s.lbl, SW_HIDE);
            ::ShowWindow(s.lv,  SW_HIDE);
            continue;
        }
        ::ShowWindow(s.lbl, SW_SHOW);
        ::ShowWindow(s.lv,  SW_SHOW);
        ::MoveWindow(s.lbl, pad, y, w, lblH, TRUE);
        y += lblH + Scale(2, dpi_);
        ::MoveWindow(s.lv, pad, y, w, s.listH, TRUE);
        // sütun genişlikleri
        if (s.lv == lvInfo_) {
            int c0 = (w * 38) / 100;
            ListView_SetColumnWidth(lvInfo_, 0, c0);
            ListView_SetColumnWidth(lvInfo_, 1, w - c0 - Scale(4, dpi_));
        } else {
            ListView_SetColumnWidth(s.lv, 0, w - Scale(4, dpi_));
        }
        y += s.listH + gap;
    }

    // Kaydırma çubuğu
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (naturalH > 0) ? naturalH - 1 : 0;
    si.nPage  = static_cast<UINT>(clientH);
    si.nPos   = scrollY_;
    ::SetScrollInfo(results_, SB_VERT, &si, TRUE);
}

// --- arama akışı -----------------------------------------------------------

ObjectType AdQueryView::ComboType() const {
    LRESULT i = ::SendMessageW(comboType_, CB_GETCURSEL, 0, 0);
    if (i == 1) return ObjectType::Group;
    if (i == 2) return ObjectType::Computer;
    return ObjectType::User;
}

void AdQueryView::SetComboType(ObjectType t) {
    ::SendMessageW(comboType_, CB_SETCURSEL, static_cast<WPARAM>(t), 0);
}

void AdQueryView::SetBusy(bool busy) {
    busy_ = busy;
    ::EnableWindow(btnSearch_, !busy);
    ::ShowWindow(progress_, busy ? SW_SHOW : SW_HIDE);
    ::SendMessageW(progress_, PBM_SETMARQUEE, busy ? TRUE : FALSE, 30);
}

void AdQueryView::BeginSearch() {
    if (busy_) return;

    wchar_t name[256]{}, dom[256]{};
    ::GetWindowTextW(editName_, name, 256);
    ::GetWindowTextW(editDomain_, dom, 256);

    std::wstring n = name;
    // baştaki/sondaki boşlukları kırp
    auto trim = [](std::wstring& s) {
        size_t a = s.find_first_not_of(L" \t");
        size_t b = s.find_last_not_of(L" \t");
        if (a == std::wstring::npos) { s.clear(); return; }
        s = s.substr(a, b - a + 1);
    };
    trim(n);
    if (n.empty()) {
        ::MessageBoxW(root_, L"Lütfen aranacak bir ad girin.", L"AD Sorgusu",
                      MB_ICONINFORMATION | MB_OK);
        return;
    }

    AdQueryRequest req;
    req.type   = ComboType();
    req.name   = n;
    req.domain = dom;
    trim(req.domain);

    SetBusy(true);
    ++generation_;
    std::thread(WorkerFn, root_, generation_, req).detach();
}

void AdQueryView::OnResult(std::unique_ptr<AdResult> res) {
    SetBusy(false);

    infoData_.clear();
    memberOfData_.clear();
    membersData_.clear();
    showMemberOf_ = false;
    showMembers_  = false;

    if (res->error) {
        ::MessageBoxW(root_, res->message.c_str(), L"AD Sorgusu — Hata",
                      MB_ICONERROR | MB_OK);
    } else if (!res->found) {
        ::MessageBoxW(root_, res->message.c_str(), L"AD Sorgusu",
                      MB_ICONINFORMATION | MB_OK);
    } else {
        infoData_     = res->info;
        memberOfData_ = res->memberOf;
        membersData_  = res->members;
        showMemberOf_ = res->showMemberOf;
        showMembers_  = res->showMembers;
        // Tespit edilen gerçek tipi combo'ya yansıt (çapraz arama tutarlılığı).
        SetComboType(res->type);
    }

    FillInfo(infoData_);
    FillNamedList(lvMemberOf_, memberOfData_);
    FillNamedList(lvMembers_,  membersData_);

    scrollY_ = 0;
    LayoutResults();
    ::InvalidateRect(results_, nullptr, TRUE);
}

// --- liste doldurma --------------------------------------------------------

void AdQueryView::FillInfo(const std::vector<InfoPair>& info) {
    ListView_DeleteAllItems(lvInfo_);
    int row = 0;
    for (const auto& p : info) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.iSubItem = 0;
        it.pszText = const_cast<LPWSTR>(p.key.c_str());
        ListView_InsertItem(lvInfo_, &it);
        ListView_SetItemText(lvInfo_, row, 1, const_cast<LPWSTR>(p.value.c_str()));
        ++row;
    }
}

void AdQueryView::FillNamedList(HWND lv, const std::vector<NamedEntry>& items) {
    ListView_DeleteAllItems(lv);
    int row = 0;
    for (const auto& e : items) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.iSubItem = 0;
        it.pszText = const_cast<LPWSTR>(e.name.c_str());
        ListView_InsertItem(lv, &it);
        ++row;
    }
}

// --- bağlam menüsü / kopyalama ---------------------------------------------

void AdQueryView::CopyTextToClipboard(const std::wstring& text) {
    if (!::OpenClipboard(root_)) return;
    ::EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* p = ::GlobalLock(h);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            ::GlobalUnlock(h);
            ::SetClipboardData(CF_UNICODETEXT, h);
        } else {
            ::GlobalFree(h);
        }
    }
    ::CloseClipboard();
}

static std::wstring GetLvText(HWND lv, int item, int sub) {
    wchar_t buf[1024];
    buf[0] = 0;
    ListView_GetItemText(lv, item, sub, buf, 1024);
    return buf;
}

void AdQueryView::CopySelected(HWND lv, int ctrlId) {
    std::wstring out;
    int i = -1;
    bool first = true;
    while ((i = ListView_GetNextItem(lv, i, LVNI_SELECTED)) != -1) {
        if (!first) out += L"\r\n";
        first = false;
        if (ctrlId == IDC_LV_INFO)
            out += GetLvText(lv, i, 0) + L": " + GetLvText(lv, i, 1);
        else
            out += GetLvText(lv, i, 0);
    }
    if (!out.empty()) CopyTextToClipboard(out);
}

void AdQueryView::OnListContextMenu(HWND lv, int ctrlId, POINT screenPt) {
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    if (ctrlId == IDC_LV_INFO) {
        ::AppendMenuW(menu, MF_STRING, IDM_COPY_VALUE, L"Değeri kopyala");
        ::AppendMenuW(menu, MF_STRING, IDM_COPY_ROW,   L"Satırı kopyala");
    } else {
        ::AppendMenuW(menu, MF_STRING, IDM_COPY, L"Kopyala");
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, IDM_CROSSSEARCH, L"Ara");
    }

    int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               screenPt.x, screenPt.y, 0, results_, nullptr);
    ::DestroyMenu(menu);
    if (cmd == 0) return;

    int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);

    switch (cmd) {
        case IDM_COPY_VALUE:
            if (sel >= 0) CopyTextToClipboard(GetLvText(lv, sel, 1));
            break;
        case IDM_COPY_ROW:
            if (sel >= 0)
                CopyTextToClipboard(GetLvText(lv, sel, 0) + L": " + GetLvText(lv, sel, 1));
            break;
        case IDM_COPY:
            CopySelected(lv, ctrlId);
            break;
        case IDM_CROSSSEARCH:
            if (sel >= 0) {
                const std::vector<NamedEntry>& data =
                    (ctrlId == IDC_LV_MEMBEROF) ? memberOfData_ : membersData_;
                if (sel < static_cast<int>(data.size())) {
                    // grup listesi → daima Grup; üye listesi → öğenin tespit edilmiş tipi
                    ObjectType t = (ctrlId == IDC_LV_MEMBEROF) ? ObjectType::Group
                                                              : data[sel].type;
                    StartCrossSearch(data[sel].name, t);
                }
            }
            break;
        default: break;
    }
}

void AdQueryView::StartCrossSearch(const std::wstring& name, ObjectType type) {
    SetComboType(type);
    ::SetWindowTextW(editName_, name.c_str());
    BeginSearch();
}

// --- kaydırma yardımcıları (ResultsProc içinden) ---------------------------

// --- ToolView arayüzü ------------------------------------------------------

void AdQueryView::SetBounds(const RECT& rc) {
    ::MoveWindow(root_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, TRUE);
}

void AdQueryView::Show(bool visible) {
    ::ShowWindow(root_, visible ? SW_SHOW : SW_HIDE);
}

// --- pencere yordamları ----------------------------------------------------

LRESULT AdQueryView::RootProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE:
            LayoutTop(LOWORD(l));
            return 0;

        case WM_COMMAND:
            if (LOWORD(w) == IDC_BTN_SEARCH && HIWORD(w) == BN_CLICKED) {
                BeginSearch();
                return 0;
            }
            break;

        case WM_APP_RESULT: {
            unsigned gen = static_cast<unsigned>(w);
            std::unique_ptr<AdResult> res(reinterpret_cast<AdResult*>(l));
            if (res && gen == generation_) OnResult(std::move(res));
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(w);
            ::SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_BTNFACE));
        }
    }
    return ::DefWindowProcW(h, m, w, l);
}

LRESULT AdQueryView::ResultsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE:
            LayoutResults();
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(w);
            ::SetBkColor(hdc, ::GetSysColor(COLOR_WINDOW));
            ::SetBkMode(hdc, OPAQUE);
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_WINDOW));
        }

        case WM_VSCROLL: {
            RECT rc; ::GetClientRect(results_, &rc);
            int clientH = rc.bottom;
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS;
            ::GetScrollInfo(results_, SB_VERT, &si);
            int maxScroll = (si.nMax + 1 > clientH) ? (si.nMax + 1 - clientH) : 0;
            int pos = scrollY_;
            switch (LOWORD(w)) {
                case SB_LINEUP:   pos -= Scale(24, dpi_); break;
                case SB_LINEDOWN: pos += Scale(24, dpi_); break;
                case SB_PAGEUP:   pos -= clientH; break;
                case SB_PAGEDOWN: pos += clientH; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                case SB_TOP:      pos = 0; break;
                case SB_BOTTOM:   pos = maxScroll; break;
                default: break;
            }
            if (pos > maxScroll) pos = maxScroll;
            if (pos < 0) pos = 0;
            if (pos != scrollY_) {
                int dy = scrollY_ - pos;
                scrollY_ = pos;
                ::ScrollWindowEx(results_, 0, dy, nullptr, nullptr, nullptr, nullptr,
                                 SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
                ::SetScrollPos(results_, SB_VERT, scrollY_, TRUE);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            int step = -(delta / WHEEL_DELTA) * Scale(54, dpi_);  // ~3 satır/çentik
            RECT rc; ::GetClientRect(results_, &rc);
            int clientH = rc.bottom;
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_RANGE;
            ::GetScrollInfo(results_, SB_VERT, &si);
            int maxScroll = (si.nMax + 1 > clientH) ? (si.nMax + 1 - clientH) : 0;
            int pos = scrollY_ + step;
            if (pos > maxScroll) pos = maxScroll;
            if (pos < 0) pos = 0;
            if (pos != scrollY_) {
                int dy = scrollY_ - pos;
                scrollY_ = pos;
                ::ScrollWindowEx(results_, 0, dy, nullptr, nullptr, nullptr, nullptr,
                                 SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
                ::SetScrollPos(results_, SB_VERT, scrollY_, TRUE);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            WPARAM key = w;
            if (key == VK_PRIOR) ::SendMessageW(h, WM_VSCROLL, SB_PAGEUP, 0);
            else if (key == VK_NEXT)  ::SendMessageW(h, WM_VSCROLL, SB_PAGEDOWN, 0);
            else if (key == VK_HOME)  ::SendMessageW(h, WM_VSCROLL, SB_TOP, 0);
            else if (key == VK_END)   ::SendMessageW(h, WM_VSCROLL, SB_BOTTOM, 0);
            else if (key == VK_UP)    ::SendMessageW(h, WM_VSCROLL, SB_LINEUP, 0);
            else if (key == VK_DOWN)  ::SendMessageW(h, WM_VSCROLL, SB_LINEDOWN, 0);
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR nh = reinterpret_cast<LPNMHDR>(l);
            if (nh->code == NM_RCLICK) {
                LPNMITEMACTIVATE ia = reinterpret_cast<LPNMITEMACTIVATE>(l);
                HWND lv = nh->hwndFrom;
                int id = static_cast<int>(nh->idFrom);
                if (ia->iItem >= 0)
                    ListView_SetItemState(lv, ia->iItem,
                                          LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                POINT pt; ::GetCursorPos(&pt);
                OnListContextMenu(lv, id, pt);
                return TRUE;
            }
            if (nh->code == LVN_KEYDOWN) {
                LPNMLVKEYDOWN kd = reinterpret_cast<LPNMLVKEYDOWN>(l);
                if (kd->wVKey == 'C' && (::GetKeyState(VK_CONTROL) & 0x8000))
                    CopySelected(nh->hwndFrom, static_cast<int>(nh->idFrom));
            }
            return 0;
        }
    }
    return ::DefWindowProcW(h, m, w, l);
}

// --- araç fabrikası --------------------------------------------------------

std::unique_ptr<ToolView> AdQueryTool::CreateView(HWND parent, HFONT font) {
    return std::make_unique<AdQueryView>(parent, font);
}

} // namespace ith
