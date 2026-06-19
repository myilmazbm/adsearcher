// Shell.h — ana pencere (kabuk). Sol TreeView (kategori→araç), sağ içerik alanı.
// Seçilen aracın görünümü CACHE'lenir; WM_SIZE'da yeniden yerleşim.
#pragma once

#include "ITool.h"
#include "Common.h"

#include <memory>
#include <vector>
#include <unordered_map>

namespace ith {

class Shell {
public:
    explicit Shell(HINSTANCE hinst);
    ~Shell();

    bool Create();           // ana pencereyi oluşturur
    void Show(int nCmdShow);
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    void BuildTree();
    void Layout();
    void SelectTool(ITool* tool);
    void UpdateFontForDpi(UINT dpi);
    void RecreateFont();

    HINSTANCE hinst_ = nullptr;
    HWND  hwnd_    = nullptr;
    HWND  tree_    = nullptr;     // sol TreeView
    HWND  content_ = nullptr;     // sağ içerik konteyneri
    HFONT font_    = nullptr;     // DPI'ya göre üretilen arayüz fontu
    UINT  dpi_     = 96;

    std::vector<std::unique_ptr<ITool>> tools_;
    // Araç → cache'lenmiş görünüm
    std::unordered_map<ITool*, std::unique_ptr<ToolView>> views_;
    ToolView* current_ = nullptr;
};

} // namespace ith
