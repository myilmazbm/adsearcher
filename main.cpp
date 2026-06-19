// main.cpp — ITHelper giriş noktası. DPI v2 manifest ile; SEH üst düzey filtre;
// Common Controls v6; UI thread STA.
#include "Shell.h"

#include <commctrl.h>
#include <cstdio>

// Bağımlılıkları kaynakta da sabitle (CMake / elle derleme için).
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

// SEH üst düzey filtre: yakalanmayan hatada kapanmadan önce kod + modül + RVA göster.
static LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(addr), &mod);

    wchar_t modPath[MAX_PATH] = L"(bilinmiyor)";
    ::GetModuleFileNameW(mod, modPath, MAX_PATH);

    uintptr_t base = reinterpret_cast<uintptr_t>(mod);
    uintptr_t rva  = reinterpret_cast<uintptr_t>(addr) - base;

    wchar_t msg[1200];
    swprintf(msg, 1200,
             L"Beklenmeyen bir hata oluştu; uygulama kapanacak.\n\n"
             L"Hata kodu: 0x%08X\n"
             L"Modül: %s\n"
             L"Temel adres: 0x%p\n"
             L"RVA: 0x%IX\n\n"
             L"(.map dosyası ile satıra eşlenebilir.)",
             ep->ExceptionRecord->ExceptionCode, modPath,
             reinterpret_cast<void*>(base), rva);
    ::MessageBoxW(nullptr, msg, L"ITHelper — Kritik Hata", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    ::SetUnhandledExceptionFilter(TopLevelFilter);

    // UI thread: tek iş parçacıklı apartman (STA). ADSI worker'ı kendi MTA'sını açar.
    HRESULT hrCom = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES |
                 ICC_PROGRESS_CLASS  | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&icc);

    int rc = 0;
    try {
        ith::Shell shell(hInst);
        if (!shell.Create()) {
            ::MessageBoxW(nullptr, L"Ana pencere oluşturulamadı.", L"ITHelper",
                          MB_ICONERROR | MB_OK);
            rc = 1;
        } else {
            shell.Show(nCmdShow);
            MSG msg;
            while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            rc = static_cast<int>(msg.wParam);
        }
    } catch (...) {
        ::MessageBoxW(nullptr, L"Uygulama başlatılırken beklenmeyen bir hata oluştu.",
                      L"ITHelper", MB_ICONERROR | MB_OK);
        rc = 1;
    }

    if (SUCCEEDED(hrCom)) ::CoUninitialize();
    return rc;
}
