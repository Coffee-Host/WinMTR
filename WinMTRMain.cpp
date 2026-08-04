#include "WinMTRGlobal.h"
#include "WinMTRMain.h"
#include "WinMTRDialog.h"
#include "WinMTRHelp.h"

#include <shellapi.h>
#include <vector>

WinMTRMain WinMTR;

BEGIN_MESSAGE_MAP(WinMTRMain, CWinApp)
    ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

WinMTRMain::WinMTRMain()
{
}

namespace {

CString WideToLocal(const wchar_t* value)
{
#ifdef _UNICODE
    return CString(value);
#else
    const int size = WideCharToMultiByte(CP_ACP, 0, value, -1, NULL, 0, NULL, NULL);
    std::vector<char> local(size > 0 ? size : 1);
    if (size > 0)
        WideCharToMultiByte(CP_ACP, 0, value, -1, &local[0], size, NULL, NULL);
    else
        local[0] = '\0';
    return CString(&local[0]);
#endif
}

bool EqualsOption(const wchar_t* value, const wchar_t* shortName,
    const wchar_t* longName)
{
    return _wcsicmp(value, shortName) == 0 || _wcsicmp(value, longName) == 0;
}

} // namespace

BOOL WinMTRMain::InitInstance()
{
    SetProcessDPIAware();
    if (!AfxSocketInit()) {
        AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
        return FALSE;
    }
    AfxEnableControlContainer();

    WinMTRDialog dialog;
    m_pMainWnd = &dialog;

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    bool showHelp = false;
    for (int i = 1; arguments && i < argumentCount; ++i) {
        if (EqualsOption(arguments[i], L"-h", L"--help")) {
            showHelp = true;
        } else if (EqualsOption(arguments[i], L"-n", L"--numeric")) {
            dialog.SetUseDNS(FALSE);
        } else if (EqualsOption(arguments[i], L"-i", L"--interval") && i + 1 < argumentCount) {
            dialog.SetInterval(static_cast<float>(_wtof(arguments[++i])));
        } else if (EqualsOption(arguments[i], L"-s", L"--size") && i + 1 < argumentCount) {
            dialog.SetPingSize(_wtoi(arguments[++i]));
        } else if (EqualsOption(arguments[i], L"-m", L"--maxLRU") && i + 1 < argumentCount) {
            dialog.SetMaxLRU(_wtoi(arguments[++i]));
        } else if (arguments[i][0] != L'-') {
            const CString host = WideToLocal(arguments[i]);
            dialog.SetHostName(host);
        }
    }
    if (arguments)
        LocalFree(arguments);

    if (showHelp) {
        WinMTRHelp help;
        m_pMainWnd = &help;
        help.DoModal();
        return FALSE;
    }

    dialog.DoModal();
    return FALSE;
}
