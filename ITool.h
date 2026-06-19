// ITool.h — genişletilebilir araç modeli arayüzleri.
// Yeni bir araç eklemek = ITool türetmek + ToolRegistry'de tek push_back.
#pragma once

#include <windows.h>
#include <memory>
#include <string>

namespace ith {

// Bir aracın görünümü. Kabuk (Shell) yalnız bu arayüz üzerinden yerleştirir/gösterir.
class ToolView {
public:
    virtual ~ToolView() = default;

    // Görünümün kapladığı dikdörtgeni ayarlar (içerik alanına göre).
    virtual void SetBounds(const RECT& rc) = 0;

    // Görünümü gösterir/gizler (sekme değişiminde).
    virtual void Show(bool visible) = 0;

    // Görünümün kök pencere tutamacı (gerektiğinde).
    virtual HWND Handle() const = 0;
};

// Bir araç: ad + kategori + görünüm üretimi.
class ITool {
public:
    virtual ~ITool() = default;

    // TreeView'da görünecek araç adı.
    virtual std::wstring Name() const = 0;

    // Araç kategorisi (TreeView'da üst düğüm).
    virtual std::wstring Category() const = 0;

    // Görünümü oluşturur. parent = içerik alanı; font = DPI'ya uygun arayüz fontu.
    // Görünüm CACHE'lenir (her seçimde yeniden oluşturulmaz).
    virtual std::unique_ptr<ToolView> CreateView(HWND parent, HFONT font) = 0;
};

} // namespace ith
