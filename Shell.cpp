// Shell.cpp — ana pencere (kabuk).
#include "Shell.h"
#include "ToolRegistry.h"

#include <commctrl.h>
#include <map>

#pragma comment(lib, "comctl32.lib")

namespace ith {
namespace {
const wchar_t* kShellClass = L"ITH_Shell";
ITool* currentTool = nullptr;   // DPI değişiminde yeniden seçmek için (örnek başına tek pencere)
} // anonim

Shell::Shell(HINSTANCE hinst) : hinst_(hinst) {
    tools_ = ToolRegistry::CreateTools();
}

Shell::~Shell() {
    if (font_) ::DeleteObject(font_);
}

void Shell::RecreateFont() {
    if (font_) { ::DeleteObject(font_); font_ = nullptr; }
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_)) {
        font_ = ::CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (!font_) {
        // yedek: Segoe UI, DPI'ya göre ölçekli
        LOGFONTW lf{};
        lf.lfHeight = -Scale(9, dpi_) * 96 / 72;  // ~9pt
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font_ = ::CreateFontIndirectW(&lf);
    }
}

bool Shell::Create() {
    dpi_ = 96;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &Shell::WndProc;
    wc.hInstance     = hinst_;
    wc.hIcon         = ::LoadIconW(hinst_, MAKEINTRESOURCEW(101));
    wc.hIconSm       = wc.hIcon;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kShellClass;
    if (!::RegisterClassExW(&wc)) return false;

    hwnd_ = ::CreateWindowExW(
        0, kShellClass, L"ITHelper — IT Yardımcı Araçları",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 600,
        nullptr, nullptr, hinst_, this);
    return hwnd_ != nullptr;
}

void Shell::Show(int nCmdShow) {
    ::ShowWindow(hwnd_, nCmdShow);
    ::UpdateWindow(hwnd_);
}

void Shell::BuildTree() {
    std::map<std::wstring, HTREEITEM> categories;
    HTREEITEM firstTool = nullptr;
    ITool* firstToolPtr = nullptr;

    for (auto& t : tools_) {
        std::wstring cat = t->Category();
        HTREEITEM hCat;
        auto it = categories.find(cat);
        if (it == categories.end()) {
            TVINSERTSTRUCTW tis{};
            tis.hParent = TVI_ROOT;
            tis.hInsertAfter = TVI_LAST;
            tis.item.mask = TVIF_TEXT | TVIF_PARAM;
            tis.item.pszText = const_cast<LPWSTR>(cat.c_str());
            tis.item.lParam = 0;   // kategori düğümü
            hCat = TreeView_InsertItem(tree_, &tis);
            categories[cat] = hCat;
        } else {
            hCat = it->second;
        }

        TVINSERTSTRUCTW tis{};
        tis.hParent = hCat;
        tis.hInsertAfter = TVI_LAST;
        tis.item.mask = TVIF_TEXT | TVIF_PARAM;
        std::wstring name = t->Name();
        tis.item.pszText = const_cast<LPWSTR>(name.c_str());
        tis.item.lParam = reinterpret_cast<LPARAM>(t.get());
        HTREEITEM hTool = TreeView_InsertItem(tree_, &tis);
        if (!firstTool) { firstTool = hTool; firstToolPtr = t.get(); }
    }

    for (auto& kv : categories) TreeView_Expand(tree_, kv.second, TVE_EXPAND);

    if (firstTool) {
        TreeView_SelectItem(tree_, firstTool);
        SelectTool(firstToolPtr);
    }
}

void Shell::SelectTool(ITool* tool) {
    if (!tool) return;
    currentTool = tool;

    auto it = views_.find(tool);
    if (it == views_.end()) {
        auto view = tool->CreateView(hwnd_, font_);
        it = views_.emplace(tool, std::move(view)).first;
    }
    ToolView* view = it->second.get();
    if (current_ && current_ != view) current_->Show(false);
    current_ = view;
    Layout();
    view->Show(true);
}

void Shell::Layout() {
    if (!hwnd_) return;
    RECT rc; ::GetClientRect(hwnd_, &rc);
    int treeW = Scale(240, dpi_);
    int pad   = Scale(6, dpi_);

    if (tree_)
        ::MoveWindow(tree_, pad, pad, treeW, rc.bottom - 2 * pad, TRUE);

    RECT content{ treeW + 2 * pad, pad, rc.right - pad, rc.bottom - pad };
    if (current_) current_->SetBounds(content);
}

LRESULT CALLBACK Shell::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Shell* self = reinterpret_cast<Shell*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(l);
        self = static_cast<Shell*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        try { return self->Handle(h, m, w, l); }
        catch (...) { return ::DefWindowProcW(h, m, w, l); }   // çökme koruması
    }
    return ::DefWindowProcW(h, m, w, l);
}

LRESULT Shell::Handle(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            hwnd_ = h;   // CreateWindowEx henüz dönmedi; üyeyi şimdi ayarla (çocuklar buna bağlanır)
            dpi_ = ::GetDpiForWindow(h);
            if (dpi_ == 0) dpi_ = 96;
            RecreateFont();

            tree_ = ::CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS |
                TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                0, 0, 10, 10, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)),
                hinst_, nullptr);
            ::SendMessageW(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            BuildTree();
            Layout();
            return 0;
        }

        case WM_SIZE:
            Layout();
            return 0;

        case WM_GETMINMAXINFO: {
            auto mmi = reinterpret_cast<MINMAXINFO*>(l);
            mmi->ptMinTrackSize.x = Scale(700, dpi_);
            mmi->ptMinTrackSize.y = Scale(430, dpi_);
            return 0;
        }

        case WM_NOTIFY: {
            auto nh = reinterpret_cast<LPNMHDR>(l);
            if (nh->idFrom == 100 && nh->code == TVN_SELCHANGEDW) {
                auto nmtv = reinterpret_cast<LPNMTREEVIEWW>(l);
                ITool* tool = reinterpret_cast<ITool*>(nmtv->itemNew.lParam);
                if (tool) SelectTool(tool);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            dpi_ = HIWORD(w);
            RecreateFont();
            if (tree_) ::SendMessageW(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            // Önbelleğe alınmış görünümleri yeni DPI ile yeniden oluştur.
            ITool* keep = currentTool;
            current_ = nullptr;
            views_.clear();

            auto prc = reinterpret_cast<RECT*>(l);
            ::SetWindowPos(h, nullptr, prc->left, prc->top,
                           prc->right - prc->left, prc->bottom - prc->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            if (keep) SelectTool(keep);
            Layout();
            return 0;
        }

        case WM_DESTROY:
            views_.clear();
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(h, m, w, l);
}

} // namespace ith
