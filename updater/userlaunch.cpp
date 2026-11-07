#include "userlaunch.h"
#include "common.h"

#include <QFileInfo>

#include <windows.h>
#include <ole2.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace up {

bool launchAsUser(const QString& exe)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    do {
        ComPtr<IShellWindows> windows;
        if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) {
            log(QStringLiteral("launch: no ShellWindows"));
            break;
        }
        VARIANT empty;
        VariantInit(&empty);
        long hwnd = 0;
        ComPtr<IDispatch> desktop;
        if (windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &desktop) != S_OK || !desktop) {
            log(QStringLiteral("launch: no desktop window"));
            break;
        }
        ComPtr<IServiceProvider> provider;
        ComPtr<IShellBrowser> browser;
        ComPtr<IShellView> view;
        ComPtr<IDispatch> background;
        ComPtr<IShellFolderViewDual> folderView;
        ComPtr<IDispatch> application;
        ComPtr<IShellDispatch2> shell;
        if (FAILED(desktop.As(&provider))
                || FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))
                || FAILED(browser->QueryActiveShellView(&view))
                || FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))
                || FAILED(background.As(&folderView))
                || FAILED(folderView->get_Application(&application))
                || FAILED(application.As(&shell))) {
            log(QStringLiteral("launch: shell view unreachable"));
            break;
        }

        const QString native = nativePath(exe);
        BSTR file = SysAllocString(reinterpret_cast<const OLECHAR*>(native.utf16()));
        VARIANT args, dir, operation, show;
        VariantInit(&args);
        VariantInit(&dir);
        VariantInit(&operation);
        VariantInit(&show);
        args.vt = VT_BSTR;
        args.bstrVal = SysAllocString(L"");
        const QString folder = nativePath(QFileInfo(exe).absolutePath());
        dir.vt = VT_BSTR;
        dir.bstrVal = SysAllocString(reinterpret_cast<const OLECHAR*>(folder.utf16()));
        operation.vt = VT_BSTR;
        operation.bstrVal = SysAllocString(L"open");
        show.vt = VT_I4;
        show.lVal = SW_SHOWNORMAL;
        ok = SUCCEEDED(shell->ShellExecute(file, args, dir, operation, show));
        SysFreeString(file);
        VariantClear(&args);
        VariantClear(&dir);
        VariantClear(&operation);
        log(QStringLiteral("launch: %1 through Explorer: %2").arg(native, ok ? QStringLiteral("ok") : QStringLiteral("failed")));
    } while (false);
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

} // namespace up
