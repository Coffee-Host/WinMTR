#include "WinMTRGlobal.h"
#include "WinMTRDialog.h"
#include "WinMTROptions.h"
#include "WinMTRProperties.h"
#include "WinMTRNetworkInfoDialog.h"

#include <algorithm>
#include <iomanip>
#include <process.h>
#include <sstream>
#include <vector>

namespace {

bool BitmapHasVisualContent(HDC dc, HBITMAP bitmap, int width, int height)
{
    if (!dc || !bitmap || width <= 0 || height <= 0)
        return false;
    BITMAPINFO information = {};
    information.bmiHeader.biSize = sizeof(information.bmiHeader);
    information.bmiHeader.biWidth = width;
    information.bmiHeader.biHeight = -height;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(static_cast<size_t>(width) * height);
    if (!GetDIBits(dc, bitmap, 0, height, &pixels[0], &information,
        DIB_RGB_COLORS)) {
        return false;
    }
    const DWORD first = pixels[0] & 0x00ffffff;
    const size_t stride = std::max<size_t>(1, pixels.size() / 2048);
    for (size_t i = stride; i < pixels.size(); i += stride) {
        if ((pixels[i] & 0x00ffffff) != first)
            return true;
    }
    return false;
}

struct DialogTraceContext {
    WinMTRNet* network;
    sockaddr_storage target;
    TraceConfig config;
};

struct PublicInfoContext {
    HWND window;
};

unsigned __stdcall PingThread(void* parameter)
{
    DialogTraceContext* context = static_cast<DialogTraceContext*>(parameter);
    context->network->DoTrace(context->target, context->config);
    delete context;
    return 0;
}

unsigned __stdcall PublicInfoThread(void* parameter)
{
    PublicInfoContext* context = static_cast<PublicInfoContext*>(parameter);
    PublicNetworkInfo* info = new PublicNetworkInfo(QueryPublicNetworkInfo());
    if (!PostMessage(context->window, WM_PUBLIC_NETWORK_INFO, 0,
        reinterpret_cast<LPARAM>(info))) {
        delete info;
    }
    delete context;
    return 0;
}

bool ReadDword(HKEY key, const char* name, DWORD& value)
{
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegQueryValueExA(key, name, 0, &type,
        reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(value);
}

void WriteDword(HKEY key, const char* name, DWORD value)
{
    RegSetValueExA(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

std::string CStringToUtf8(const CString& value)
{
#ifdef _UNICODE
    const wchar_t* wide = value;
#else
    const int wideSize = MultiByteToWideChar(CP_ACP, 0, value, -1, NULL, 0);
    std::vector<wchar_t> converted(wideSize > 0 ? wideSize : 1);
    if (wideSize > 0)
        MultiByteToWideChar(CP_ACP, 0, value, -1, &converted[0], wideSize);
    else
        converted[0] = L'\0';
    const wchar_t* wide = &converted[0];
#endif
    const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    std::vector<char> utf8(utf8Size > 0 ? utf8Size : 1);
    if (utf8Size > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, &utf8[0], utf8Size, NULL, NULL);
    else
        utf8[0] = '\0';
    return std::string(&utf8[0]);
}

std::string HtmlEscape(const std::string& value)
{
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        switch (value[i]) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result.push_back(value[i]); break;
        }
    }
    return result;
}

std::string CsvEscape(const std::string& value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos)
        return value;
    std::string result = "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '"')
            result += "\"\"";
        else
            result.push_back(value[i]);
    }
    result += '"';
    return result;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream result;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        switch (ch) {
        case '"': result << "\\\""; break;
        case '\\': result << "\\\\"; break;
        case '\b': result << "\\b"; break;
        case '\f': result << "\\f"; break;
        case '\n': result << "\\n"; break;
        case '\r': result << "\\r"; break;
        case '\t': result << "\\t"; break;
        default:
            if (ch < 0x20)
                result << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(ch) << std::dec;
            else
                result << value[i];
            break;
        }
    }
    return result.str();
}

} // namespace

BEGIN_MESSAGE_MAP(WinMTRDialog, CDialog)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_SIZING()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_CLOSE()
    ON_BN_CLICKED(ID_RESTART, OnRestart)
    ON_BN_CLICKED(ID_OPTIONS, OnOptions)
    ON_COMMAND(ID_CTTC, OnCTTC)
    ON_COMMAND(ID_CHTC, OnCHTC)
    ON_COMMAND(ID_EXPT, OnEXPT)
    ON_COMMAND(ID_EXPH, OnEXPH)
    ON_COMMAND(ID_EXPC, OnEXPC)
    ON_COMMAND(ID_EXPJ, OnEXPJ)
    ON_BN_CLICKED(ID_CAPTURE_SCREENSHOT, OnCaptureScreenshot)
    ON_BN_CLICKED(ID_REPORT_MENU, OnReportMenu)
    ON_BN_CLICKED(ID_RESET_STATS, OnResetStats)
    ON_BN_CLICKED(ID_NETWORK_DETAILS, OnNetworkInfo)
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_MTR, OnDblclkList)
    ON_CBN_SELCHANGE(IDC_COMBO_HOST, OnCbnSelchangeComboHost)
    ON_CBN_SELENDOK(IDC_COMBO_HOST, OnCbnSelendokComboHost)
    ON_CBN_CLOSEUP(IDC_COMBO_HOST, OnCbnCloseupComboHost)
    ON_MESSAGE(WM_PUBLIC_NETWORK_INFO, OnPublicNetworkInfoReady)
END_MESSAGE_MAP()

WinMTRDialog::WinMTRDialog(CWnd* parent)
    : CDialog(WinMTRDialog::IDD, parent), state(IDLE), transition(IDLE_TO_IDLE),
      traceThread(NULL), publicInfoThread(NULL), interval(DEFAULT_INTERVAL),
      pingSize(DEFAULT_PING_SIZE),
      maxLRU(DEFAULT_MAX_LRU), nrLRU(0), maxHops(DEFAULT_MAX_HOPS),
      timeoutMs(DEFAULT_TIMEOUT_MS), cycles(DEFAULT_CYCLES), tos(DEFAULT_TOS),
      bitPattern(DEFAULT_BIT_PATTERN), useDNS(DEFAULT_DNS),
      dontFragment(DEFAULT_DONT_FRAGMENT), lookupAsn(DEFAULT_ASN_LOOKUP),
      lookupPublicInfo(WINMTR_ENABLE_PUBLIC_IP_LOOKUP_DEFAULT ? TRUE : FALSE),
      useIPv4(DEFAULT_IPV4), useIPv6(DEFAULT_IPV6),
      hasIntervalFromCmdLine(false), hasPingSizeFromCmdLine(false),
      hasMaxLRUFromCmdLine(false), hasUseDNSFromCmdLine(false),
      publicInfoQueryStarted(false), adjustingWindow(false),
      minimumWindowSize(0, 0), m_autostart(0),
      network(new WinMTRNet()),
      publicNetworkInfo(NULL)
{
    ZeroMemory(&traceTarget, sizeof(traceTarget));
    ZeroMemory(defaultHostName, sizeof(defaultHostName));
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

WinMTRDialog::~WinMTRDialog()
{
    network->StopTrace();
    if (traceThread) {
        WaitForSingleObject(traceThread, INFINITE);
        CloseHandle(traceThread);
    }
    if (publicInfoThread) {
        WaitForSingleObject(publicInfoThread, INFINITE);
        CloseHandle(publicInfoThread);
    }
    MSG message = {};
    while (PeekMessage(&message, NULL, WM_PUBLIC_NETWORK_INFO,
        WM_PUBLIC_NETWORK_INFO, PM_REMOVE)) {
        delete reinterpret_cast<PublicNetworkInfo*>(message.lParam);
    }
    delete network;
    delete publicNetworkInfo;
}

void WinMTRDialog::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, ID_OPTIONS, m_buttonOptions);
    DDX_Control(pDX, ID_RESTART, m_buttonStart);
    DDX_Control(pDX, ID_RESET_STATS, m_buttonReset);
    DDX_Control(pDX, ID_CAPTURE_SCREENSHOT, m_buttonCapture);
    DDX_Control(pDX, ID_REPORT_MENU, m_buttonReportMenu);
    DDX_Control(pDX, ID_NETWORK_DETAILS, m_buttonNetworkDetails);
    DDX_Control(pDX, IDC_PUBLIC_IP_TEXT, m_publicIpSummary);
    DDX_Control(pDX, IDC_PUBLIC_HOSTNAME_TEXT, m_publicHostnameSummary);
    DDX_Control(pDX, IDC_PUBLIC_COUNTRY_TEXT, m_publicCountrySummary);
    DDX_Control(pDX, IDC_PUBLIC_CITY_TEXT, m_publicCitySummary);
    DDX_Control(pDX, IDC_PUBLIC_ASN_TEXT, m_publicAsnSummary);
    DDX_Control(pDX, IDC_PUBLIC_ISP_TEXT, m_publicIspSummary);
    DDX_Control(pDX, IDC_COMBO_HOST, m_comboHost);
    DDX_Control(pDX, IDC_LIST_MTR, m_listMTR);
    DDX_Control(pDX, IDC_STATICS, m_staticS);
    DDX_Control(pDX, IDC_STATIC_ACTIONS, m_staticActions);
    DDX_Control(pDX, IDC_STATICJ, m_staticJ);
}

CString WinMTRDialog::LoadText(UINT id) const
{
    CString text;
    text.LoadString(id);
    return text;
}

CString WinMTRDialog::Utf8ToLocal(const std::string& value) const
{
    if (value.empty())
        return CString();
    const int wideSize = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, NULL, 0);
    if (wideSize <= 0)
        return CString(value.c_str());
    std::vector<wchar_t> wide(wideSize);
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &wide[0], wideSize);
#ifdef _UNICODE
    return CString(&wide[0]);
#else
    const int localSize = WideCharToMultiByte(CP_ACP, 0, &wide[0], -1, NULL, 0, NULL, NULL);
    std::vector<char> local(localSize > 0 ? localSize : 1);
    if (localSize > 0)
        WideCharToMultiByte(CP_ACP, 0, &wide[0], -1, &local[0], localSize, NULL, NULL);
    else
        local[0] = '\0';
    return CString(&local[0]);
#endif
}

void WinMTRDialog::SetTraceStatus(const CString& text)
{
    if (::IsWindow(footerStatus.GetSafeHwnd()))
        footerStatus.SetPaneText(0, text);
}

void WinMTRDialog::SetPublicInfoPlaceholders(UINT ipTextId)
{
    const CString unavailable = LoadText(IDS_NETWORK_INFO_NOT_AVAILABLE);
    CString value;
    m_publicIpSummary.SetWindowText(LoadText(ipTextId));
    value.Format(LoadText(IDS_NETWORK_SUMMARY_HOSTNAME_FORMAT),
        static_cast<LPCTSTR>(unavailable));
    m_publicHostnameSummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_COUNTRY_FORMAT),
        static_cast<LPCTSTR>(unavailable));
    m_publicCountrySummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_CITY_FORMAT),
        static_cast<LPCTSTR>(unavailable));
    m_publicCitySummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_ASN_FORMAT),
        static_cast<LPCTSTR>(unavailable));
    m_publicAsnSummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_ISP_FORMAT),
        static_cast<LPCTSTR>(unavailable));
    m_publicIspSummary.SetWindowText(value);
    m_buttonNetworkDetails.EnableWindow(FALSE);
}

void WinMTRDialog::UpdatePublicInfoSummary(const IpNetworkDetails& details)
{
    const CString unavailable = LoadText(IDS_NETWORK_INFO_NOT_AVAILABLE);
    CString value;
    value.Format(LoadText(IDS_STATUS_PUBLIC_IP_FORMAT),
        static_cast<LPCTSTR>(Utf8ToLocal(details.address)));
    m_publicIpSummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_HOSTNAME_FORMAT),
        static_cast<LPCTSTR>(details.hostname.empty()
            ? unavailable : Utf8ToLocal(details.hostname)));
    m_publicHostnameSummary.SetWindowText(value);
    const CString country = details.country.empty()
        ? (details.countryCode.empty()
            ? unavailable : Utf8ToLocal(details.countryCode))
        : Utf8ToLocal(details.country);
    const CString city = details.city.empty()
        ? unavailable : Utf8ToLocal(details.city);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_COUNTRY_FORMAT),
        static_cast<LPCTSTR>(country));
    m_publicCountrySummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_CITY_FORMAT),
        static_cast<LPCTSTR>(city));
    m_publicCitySummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_ASN_FORMAT),
        static_cast<LPCTSTR>(details.asn.empty()
            ? unavailable : Utf8ToLocal(details.asn)));
    m_publicAsnSummary.SetWindowText(value);
    value.Format(LoadText(IDS_NETWORK_SUMMARY_ISP_FORMAT),
        static_cast<LPCTSTR>(details.isp.empty()
            ? unavailable : Utf8ToLocal(details.isp)));
    m_publicIspSummary.SetWindowText(value);
    m_buttonNetworkDetails.EnableWindow(TRUE);
}

BOOL WinMTRDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

    const CString caption = LoadText(IDS_WINDOW_TITLE);
    SetWindowText(caption);
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetTimer(1, WINMTR_DIALOG_TIMER, NULL);

    m_codeFont.CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, WINMTR_CODE_FONT_NAME);
    m_tableFont.CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, WINMTR_CODE_FONT_NAME);
    m_listMTR.SetFont(&m_tableFont);
    m_comboHost.SetFont(&m_codeFont);
    CRect hostDisplayRect;
    m_comboHost.GetWindowRect(hostDisplayRect);
    ScreenToClient(hostDisplayRect);
    if (m_traceHostDisplay.CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""),
        WS_CHILD | ES_AUTOHSCROLL | ES_READONLY, hostDisplayRect, this,
        IDC_TRACE_HOST_DISPLAY)) {
        m_traceHostDisplay.SetFont(&m_codeFont);
    }
    m_publicIpSummary.SetFont(&m_codeFont);
    m_publicHostnameSummary.SetFont(&m_codeFont);
    m_publicCountrySummary.SetFont(GetFont());
    m_publicCitySummary.SetFont(GetFont());
    m_publicAsnSummary.SetFont(&m_codeFont);
    m_publicIspSummary.SetFont(&m_codeFont);
    m_buttonNetworkDetails.SetWindowText(LoadText(IDS_BUTTON_NETWORK_DETAILS));
    SetPublicInfoPlaceholders(IDS_STATUS_PUBLIC_IP_QUERYING);
    SetTraceStatus(LoadText(IDS_STATUS_READY));

    if (!footerStatus.Create(this))
        return FALSE;
    footerStatus.SetBarStyle((footerStatus.GetBarStyle() & ~CBRS_ALIGN_ANY) |
        CBRS_BOTTOM);
    footerStatus.GetStatusBarCtrl().SetMinHeight(24);
    const UINT footerIndicators[2] = { IDS_STATUS_READY, IDS_COMPANY_LINK };
    footerStatus.SetIndicators(footerIndicators, 2);
    footerStatus.SetPaneInfo(0, footerStatus.GetItemID(0), SBPS_STRETCH, 0);
    footerStatus.SetFont(GetFont());
    const CString companyName = LoadText(IDS_COMPANY_LINK);
    CClientDC footerDc(&footerStatus);
    CFont* previousFooterFont = footerDc.SelectObject(&m_codeFont);
    const int companyPaneWidth = footerDc.GetTextExtent(companyName).cx + 20;
    footerDc.SelectObject(previousFooterFont);
    footerStatus.SetPaneInfo(1, footerStatus.GetItemID(1), SBPS_NORMAL,
        companyPaneWidth);
    if (companyLink.Create(companyName,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(0, 0, 0, 0),
        &footerStatus, IDC_COMPANY_LINK)) {
        companyLink.SetFont(&m_codeFont);
        companyLink.SetURL(WINMTR_COMPANY_URL);
        footerStatus.AddPaneControl(&companyLink, IDS_COMPANY_LINK, FALSE);
        footerStatus.SetPaneText(1, _T(""));
    }
    m_listMTR.SetExtendedStyle(m_listMTR.GetExtendedStyle() |
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < MTR_NR_COLS; ++i)
        m_listMTR.InsertColumn(i, LoadText(MTR_COL_RESOURCE_IDS[i]), LVCFMT_LEFT,
            MTR_COL_LENGTH[i], -1);

    CRect window;
    GetWindowRect(window);
    CRect footer;
    footerStatus.GetWindowRect(footer);
    window.bottom += footer.Height();
    MoveWindow(window, FALSE);
    RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0);
    GetWindowRect(window);
    CRect listWindow;
    m_listMTR.GetWindowRect(listWindow);
    minimumWindowSize = CSize(window.Width(),
        window.Height() - listWindow.Height() + 6);
    m_listMTR.ShowWindow(SW_HIDE);

    AdjustColumnWidths();
    AdjustWindowToContent();
    CRect client;
    GetClientRect(client);
    PostMessage(WM_SIZE, SIZE_RESTORED,
        MAKELPARAM(client.Width(), client.Height()));

    InitRegistry();
    if (lookupPublicInfo)
        StartPublicInfoLookup();
    else
        SetPublicInfoPlaceholders(IDS_NETWORK_INFO_UNAVAILABLE);

    if (m_autostart) {
        m_comboHost.SetWindowText(defaultHostName);
        OnRestart();
    } else {
        m_comboHost.SetFocus();
    }
    return FALSE;
}

BOOL WinMTRDialog::InitRegistry()
{
    HKEY root = NULL;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinMTR", 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &root, NULL) != ERROR_SUCCESS)
        return FALSE;
    RegSetValueExA(root, "Version", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(WINMTR_VERSION),
        static_cast<DWORD>(strlen(WINMTR_VERSION) + 1));
    RegSetValueExA(root, "HomePage", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(WINMTR_HOMEPAGE),
        static_cast<DWORD>(strlen(WINMTR_HOMEPAGE) + 1));

    HKEY config = NULL;
    if (RegCreateKeyExA(root, "Config", 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE, NULL, &config, NULL) == ERROR_SUCCESS) {
        DWORD value = 0;
        if (ReadDword(config, "PingSize", value)) {
            if (!hasPingSizeFromCmdLine) pingSize = static_cast<int>(value);
        } else WriteDword(config, "PingSize", pingSize);
        if (ReadDword(config, "MaxLRU", value)) {
            if (!hasMaxLRUFromCmdLine) maxLRU = static_cast<int>(value);
        } else WriteDword(config, "MaxLRU", maxLRU);
        if (ReadDword(config, "UseDNS", value)) {
            if (!hasUseDNSFromCmdLine) useDNS = value ? TRUE : FALSE;
        } else WriteDword(config, "UseDNS", useDNS ? 1 : 0);
        if (ReadDword(config, "Interval", value)) {
            if (!hasIntervalFromCmdLine) interval = value / 1000.0;
        } else WriteDword(config, "Interval", static_cast<DWORD>(interval * 1000));
        if (ReadDword(config, "MaxHops", value)) maxHops = static_cast<int>(value);
        if (ReadDword(config, "TimeoutMs", value)) timeoutMs = static_cast<int>(value);
        if (ReadDword(config, "Cycles", value)) cycles = static_cast<int>(value);
        if (ReadDword(config, "TOS", value)) tos = static_cast<int>(value);
        if (ReadDword(config, "BitPattern", value)) bitPattern = static_cast<int>(value);
        if (ReadDword(config, "DontFragment", value)) dontFragment = value ? TRUE : FALSE;
        if (ReadDword(config, "LookupAsn", value)) lookupAsn = value ? TRUE : FALSE;
        if (ReadDword(config, "LookupPublicInfo", value)) lookupPublicInfo = value ? TRUE : FALSE;
        if (ReadDword(config, "UseIPv4", value)) useIPv4 = value ? TRUE : FALSE;
        else WriteDword(config, "UseIPv4", useIPv4 ? 1 : 0);
        if (ReadDword(config, "UseIPv6", value)) useIPv6 = value ? TRUE : FALSE;
        else WriteDword(config, "UseIPv6", useIPv6 ? 1 : 0);
        if (!useIPv4 && !useIPv6) {
            useIPv4 = DEFAULT_IPV4;
            useIPv6 = DEFAULT_IPV6;
            WriteDword(config, "UseIPv4", useIPv4 ? 1 : 0);
            WriteDword(config, "UseIPv6", useIPv6 ? 1 : 0);
        }
        RegCloseKey(config);
    }

    maxLRU = std::max(1, std::min(maxLRU, MaxHost));
    pingSize = std::max(0, std::min(pingSize, MAXPACKET));
    interval = std::max(0.1, std::min(interval, 60.0));
    maxHops = std::max(1, std::min(maxHops, 64));
    timeoutMs = std::max(100, std::min(timeoutMs, 10000));
    cycles = std::max(0, std::min(cycles, 100000));
    tos = std::max(0, std::min(tos, 255));
    bitPattern = std::max(-1, std::min(bitPattern, 255));
    HKEY history = NULL;
    if (RegCreateKeyExA(root, "LRU", 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE, NULL, &history, NULL) == ERROR_SUCCESS) {
        DWORD count = 0;
        if (ReadDword(history, "NrLRU", count))
            nrLRU = std::min(static_cast<int>(count), maxLRU);
        for (int i = 1; i <= nrLRU; ++i) {
            char name[32] = {};
            sprintf_s(name, "Host%d", i);
            char value[1000] = {};
            DWORD size = sizeof(value);
            DWORD type = 0;
            if (RegQueryValueExA(history, name, 0, &type,
                reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS &&
                type == REG_SZ) {
                value[_countof(value) - 1] = '\0';
                m_comboHost.AddString(value);
            }
        }
        RegCloseKey(history);
    }
    m_comboHost.AddString(LoadText(IDS_STRING_CLEAR_HISTORY));
    RegCloseKey(root);
    return TRUE;
}

void WinMTRDialog::SaveConfiguration()
{
    HKEY key = NULL;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinMTR\\Config", 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;
    WriteDword(key, "PingSize", pingSize);
    WriteDword(key, "MaxLRU", maxLRU);
    WriteDword(key, "UseDNS", useDNS ? 1 : 0);
    WriteDword(key, "Interval", static_cast<DWORD>(interval * 1000));
    WriteDword(key, "MaxHops", maxHops);
    WriteDword(key, "TimeoutMs", timeoutMs);
    WriteDword(key, "Cycles", cycles);
    WriteDword(key, "TOS", tos);
    WriteDword(key, "BitPattern", static_cast<DWORD>(bitPattern));
    WriteDword(key, "DontFragment", dontFragment ? 1 : 0);
    WriteDword(key, "LookupAsn", lookupAsn ? 1 : 0);
    WriteDword(key, "LookupPublicInfo", lookupPublicInfo ? 1 : 0);
    WriteDword(key, "UseIPv4", useIPv4 ? 1 : 0);
    WriteDword(key, "UseIPv6", useIPv6 ? 1 : 0);
    RegCloseKey(key);
}

void WinMTRDialog::SetHostName(const char* host)
{
    m_autostart = 1;
    strncpy_s(defaultHostName, host, _TRUNCATE);
}

void WinMTRDialog::SetInterval(float value)
{
    interval = std::max(0.1, std::min(static_cast<double>(value), 60.0));
    hasIntervalFromCmdLine = true;
}

void WinMTRDialog::SetPingSize(int value)
{
    pingSize = std::max(0, std::min(value, MAXPACKET));
    hasPingSizeFromCmdLine = true;
}

void WinMTRDialog::SetMaxLRU(int value)
{
    maxLRU = std::max(1, std::min(value, MaxHost));
    hasMaxLRUFromCmdLine = true;
}

void WinMTRDialog::SetUseDNS(BOOL value)
{
    useDNS = value;
    hasUseDNSFromCmdLine = true;
}

TraceConfig WinMTRDialog::CurrentTraceConfig() const
{
    TraceConfig config;
    config.intervalMs = static_cast<int>(interval * 1000.0);
    config.pingSize = pingSize;
    config.maxHops = maxHops;
    config.timeoutMs = timeoutMs;
    config.cycles = cycles;
    config.tos = tos;
    config.bitPattern = bitPattern;
    config.useDns = useDNS != FALSE;
    config.lookupAsn = lookupAsn != FALSE;
    config.dontFragment = dontFragment != FALSE;
    return config;
}

int WinMTRDialog::ResolveTraceTarget()
{
    CString hostText;
    m_comboHost.GetWindowText(hostText);
    CString status;
    status.Format(LoadText(IDS_STATUS_RESOLVING), static_cast<LPCTSTR>(hostText));
    SetTraceStatus(status);

    addrinfo hints = {};
    hints.ai_family = useIPv4 && useIPv6
        ? AF_UNSPEC : useIPv6 ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = NULL;
    if (getaddrinfo(hostText, NULL, &hints, &addresses) != 0 || !addresses) {
        SetTraceStatus(LoadText(IDS_STATUS_READY));
        AfxMessageBox(LoadText(IDS_ERROR_RESOLVE_FAILED), MB_ICONWARNING);
        return 0;
    }

    addrinfo* selected = addresses;
    ZeroMemory(&traceTarget, sizeof(traceTarget));
    memcpy(&traceTarget, selected->ai_addr,
        std::min(static_cast<size_t>(selected->ai_addrlen), sizeof(traceTarget)));
    freeaddrinfo(addresses);
    return 1;
}

void WinMTRDialog::OnRestart()
{
    if (m_comboHost.GetCurSel() == m_comboHost.GetCount() - 1) {
        ClearHistory();
        return;
    }

    if (state != IDLE) {
        Transit(STOPPING);
        return;
    }

    CString host;
    m_comboHost.GetWindowText(host);
    host.Trim();
    if (host.IsEmpty()) {
        AfxMessageBox(LoadText(IDS_ERROR_NO_HOST), MB_ICONWARNING);
        m_comboHost.SetFocus();
        return;
    }
    m_comboHost.SetWindowText(host);
    if (!ResolveTraceTarget())
        return;

    m_listMTR.DeleteAllItems();
    displayedHopRanges.clear();
    AdjustColumnWidths();
    AdjustWindowToContent();
    if (m_comboHost.FindStringExact(-1, host) == CB_ERR) {
        m_comboHost.InsertString(m_comboHost.GetCount() - 1, host);
        HKEY key = NULL;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinMTR\\LRU", 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
            if (nrLRU >= maxLRU)
                nrLRU = 0;
            ++nrLRU;
            char name[32] = {};
            sprintf_s(name, "Host%d", nrLRU);
            RegSetValueExA(key, name, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(static_cast<LPCSTR>(host)),
                static_cast<DWORD>(host.GetLength() + 1));
            WriteDword(key, "NrLRU", nrLRU);
            RegCloseKey(key);
        }
    }
    Transit(TRACING);
}

void WinMTRDialog::OnOptions()
{
    WinMTROptions options(this);
    options.SetPingSize(pingSize);
    options.SetInterval(interval);
    options.SetMaxLRU(maxLRU);
    options.SetMaxHops(maxHops);
    options.SetTimeout(timeoutMs);
    options.SetCycles(cycles);
    options.SetTos(tos);
    options.SetBitPattern(bitPattern);
    options.SetUseDNS(useDNS);
    options.SetDontFragment(dontFragment);
    options.SetLookupAsn(lookupAsn);
    options.SetLookupPublicInfo(lookupPublicInfo);
    options.SetUseIPv4(useIPv4);
    options.SetUseIPv6(useIPv6);
    if (options.DoModal() != IDOK)
        return;

    pingSize = options.GetPingSize();
    interval = options.GetInterval();
    maxLRU = options.GetMaxLRU();
    maxHops = options.GetMaxHops();
    timeoutMs = options.GetTimeout();
    cycles = options.GetCycles();
    tos = options.GetTos();
    bitPattern = options.GetBitPattern();
    useDNS = options.GetUseDNS();
    dontFragment = options.GetDontFragment();
    lookupAsn = options.GetLookupAsn();
    lookupPublicInfo = options.GetLookupPublicInfo();
    useIPv4 = options.GetUseIPv4();
    useIPv6 = options.GetUseIPv6();
    SaveConfiguration();
    if (lookupPublicInfo && !publicInfoQueryStarted)
        StartPublicInfoLookup();
}

void WinMTRDialog::Transit(STATES newState)
{
    const STATES oldState = state;
    state = newState;

    if (oldState == IDLE && newState == TRACING) {
        transition = IDLE_TO_TRACING;
        m_buttonStart.SetWindowText(LoadText(IDS_BUTTON_STOP));
        m_buttonStart.SetFocus();
        SetHostControlTracing(true);
        m_buttonOptions.EnableWindow(FALSE);
        SetTraceStatus(LoadText(IDS_STATUS_TRACING));
        DialogTraceContext* context = new DialogTraceContext;
        context->network = network;
        context->target = traceTarget;
        context->config = CurrentTraceConfig();
        const uintptr_t handle = _beginthreadex(NULL, 0, PingThread, context, 0, NULL);
        if (handle == 0) {
            delete context;
            state = IDLE;
            m_buttonStart.SetWindowText(LoadText(IDS_BUTTON_START));
            SetHostControlTracing(false);
            m_buttonOptions.EnableWindow(TRUE);
            return;
        }
        traceThread = reinterpret_cast<HANDLE>(handle);
    } else if (oldState == TRACING && newState == STOPPING) {
        transition = TRACING_TO_STOPPING;
        m_buttonStart.EnableWindow(FALSE);
        network->StopTrace();
        SetTraceStatus(LoadText(IDS_STATUS_STOPPING));
        DisplayRedraw();
    } else if ((oldState == STOPPING || oldState == TRACING) && newState == IDLE) {
        transition = STOPPING_TO_IDLE;
        state = IDLE;
        m_buttonStart.EnableWindow(TRUE);
        m_buttonStart.SetWindowText(LoadText(IDS_BUTTON_START));
        SetHostControlTracing(false);
        m_buttonOptions.EnableWindow(TRUE);
        SetTraceStatus(LoadText(IDS_STATUS_READY));
        DisplayRedraw();
    } else if (newState == EXIT) {
        transition = oldState == TRACING ? TRACING_TO_EXIT
            : oldState == STOPPING ? STOPPING_TO_EXIT : IDLE_TO_EXIT;
        m_buttonStart.EnableWindow(FALSE);
        m_comboHost.EnableWindow(FALSE);
        m_buttonOptions.EnableWindow(FALSE);
        network->StopTrace();
        if (traceThread)
            SetTraceStatus(LoadText(IDS_STATUS_STOPPING));
    }
}

int WinMTRDialog::DisplayRedraw()
{
    const int hops = network->GetMax();
    std::vector<std::pair<int, int> > ranges;
    for (int hopIndex = 0; hopIndex < hops; ++hopIndex) {
        const int start = hopIndex;
        if (!network->GetHopSnapshot(hopIndex).hasAddress) {
            while (hopIndex + 1 < hops &&
                !network->GetHopSnapshot(hopIndex + 1).hasAddress) {
                ++hopIndex;
            }
        }
        ranges.push_back(std::make_pair(start, hopIndex));
    }
    while (m_listMTR.GetItemCount() > static_cast<int>(ranges.size()))
        m_listMTR.DeleteItem(m_listMTR.GetItemCount() - 1);

    for (size_t rowIndex = 0; rowIndex < ranges.size(); ++rowIndex) {
        const int start = ranges[rowIndex].first;
        const int end = ranges[rowIndex].second;
        const HopSnapshot hop = network->GetHopSnapshot(start);
        CString name = hop.name.empty() ? Utf8ToLocal(hop.address) : Utf8ToLocal(hop.name);
        if (name.IsEmpty())
            name = LoadText(IDS_NO_RESPONSE);
        const int row = static_cast<int>(rowIndex);
        if (m_listMTR.GetItemCount() <= row)
            m_listMTR.InsertItem(row, name);
        else
            m_listMTR.SetItemText(row, 0, name);

        CString value;
        if (end > start)
            value.Format("%d-%d", start + 1, end + 1);
        else
            value.Format("%d", start + 1);
        m_listMTR.SetItemText(row, 1, value);
        const int numericValues[9] = {
            hop.lossPercent, hop.xmit, hop.returned, hop.best,
            hop.average, hop.worst, hop.last, hop.jitter, hop.standardDeviation
        };
        for (int column = 2; column <= 10; ++column) {
            if (column == 2)
                value.Format("%d%%", numericValues[column - 2]);
            else
                value.Format("%d", numericValues[column - 2]);
            m_listMTR.SetItemText(row, column, value);
        }
        m_listMTR.SetItemText(row, 11, Utf8ToLocal(hop.country));
        m_listMTR.SetItemText(row, 12, Utf8ToLocal(hop.asn));
        m_listMTR.SetItemText(row, 13, Utf8ToLocal(hop.isp));
    }
    displayedHopRanges.swap(ranges);
    AdjustColumnWidths();
    AdjustWindowToContent();
    return 0;
}

void WinMTRDialog::OnDblclkList(NMHDR*, LRESULT* result)
{
    POSITION position = m_listMTR.GetFirstSelectedItemPosition();
    if (position) {
        const int item = m_listMTR.GetNextSelectedItem(position);
        if (item < 0 || item >= static_cast<int>(displayedHopRanges.size())) {
            *result = 0;
            return;
        }
        const int start = displayedHopRanges[item].first;
        const int end = displayedHopRanges[item].second;
        const HopSnapshot hop = network->GetHopSnapshot(start);
        WinMTRProperties properties(this);
        strncpy_s(properties.host,
            hop.name.empty() ? hop.address.c_str() : hop.name.c_str(), _TRUNCATE);
        strncpy_s(properties.ip, hop.address.c_str(), _TRUNCATE);
        std::string comment;
        if (!hop.country.empty()) comment += hop.country;
        if (!hop.asn.empty()) comment += "  AS" + hop.asn;
        if (!hop.isp.empty()) comment += "  " + hop.isp;
        if (end > start) {
            CString range;
            range.Format(LoadText(IDS_NO_RESPONSE_RANGE), start + 1, end + 1);
            comment = CStringToUtf8(range);
        } else if (comment.empty() && !hop.hasAddress) {
            comment = CStringToUtf8(LoadText(IDS_NO_RESPONSE));
        }
        strncpy_s(properties.comment, comment.c_str(), _TRUNCATE);
        properties.ping_avrg = static_cast<float>(hop.average);
        properties.ping_last = static_cast<float>(hop.last);
        properties.ping_best = static_cast<float>(hop.best);
        properties.ping_worst = static_cast<float>(hop.worst);
        properties.pck_loss = hop.lossPercent;
        properties.pck_recv = hop.returned;
        properties.pck_sent = hop.xmit;
        properties.DoModal();
    }
    *result = 0;
}

void WinMTRDialog::OnResetStats()
{
    network->ResetHops();
    m_listMTR.DeleteAllItems();
    displayedHopRanges.clear();
    AdjustColumnWidths();
    AdjustWindowToContent();
}

void WinMTRDialog::StartPublicInfoLookup()
{
    if (publicInfoQueryStarted)
        return;
    publicInfoQueryStarted = true;
    SetPublicInfoPlaceholders(IDS_STATUS_PUBLIC_IP_QUERYING);
    PublicInfoContext* context = new PublicInfoContext;
    context->window = GetSafeHwnd();
    const uintptr_t handle = _beginthreadex(NULL, 0, PublicInfoThread, context, 0, NULL);
    if (handle) {
        publicInfoThread = reinterpret_cast<HANDLE>(handle);
    } else {
        delete context;
        publicInfoQueryStarted = false;
        SetPublicInfoPlaceholders(IDS_STATUS_PUBLIC_IP_FAILED);
    }
}

LRESULT WinMTRDialog::OnPublicNetworkInfoReady(WPARAM, LPARAM value)
{
    delete publicNetworkInfo;
    publicNetworkInfo = reinterpret_cast<PublicNetworkInfo*>(value);
    const std::string address = publicNetworkInfo->ipv4.available
        ? publicNetworkInfo->ipv4.address : publicNetworkInfo->ipv6.address;
    if (address.empty()) {
        SetPublicInfoPlaceholders(IDS_STATUS_PUBLIC_IP_FAILED);
    } else {
        UpdatePublicInfoSummary(publicNetworkInfo->ipv4.available
            ? publicNetworkInfo->ipv4 : publicNetworkInfo->ipv6);
        AdjustColumnWidths();
        AdjustWindowToContent();
        CRect client;
        GetClientRect(client);
        PostMessage(WM_SIZE, SIZE_RESTORED,
            MAKELPARAM(client.Width(), client.Height()));
    }
    return 0;
}

void WinMTRDialog::OnNetworkInfo()
{
    if (!publicNetworkInfo) {
        AfxMessageBox(LoadText(IDS_NETWORK_INFO_LOADING));
        return;
    }
    WinMTRNetworkInfoDialog dialog(*publicNetworkInfo, this);
    dialog.DoModal();
}

std::string WinMTRDialog::BuildTextReport() const
{
    std::ostringstream output;
    for (int i = 0; i < MTR_NR_COLS; ++i) {
        if (i) output << '\t';
        output << CStringToUtf8(LoadText(MTR_COL_RESOURCE_IDS[i]));
    }
    output << "\r\n";
    const int hops = network->GetMax();
    for (int i = 0; i < hops; ++i) {
        const HopSnapshot hop = network->GetHopSnapshot(i);
        const std::string name = hop.name.empty()
            ? (hop.address.empty() ? CStringToUtf8(LoadText(IDS_NO_RESPONSE)) : hop.address)
            : hop.name;
        output << name << '\t' << i + 1 << '\t' << hop.lossPercent << '\t'
               << hop.xmit << '\t' << hop.returned << '\t' << hop.best << '\t'
               << hop.average << '\t' << hop.worst << '\t' << hop.last << '\t'
               << hop.jitter << '\t' << hop.standardDeviation << '\t'
               << hop.country << '\t' << hop.asn << '\t' << hop.isp << "\r\n";
    }
    return output.str();
}

std::string WinMTRDialog::BuildHtmlReport() const
{
    std::ostringstream output;
    output << "<!doctype html><html lang=\"zh-Hant-TW\"><head><meta charset=\"utf-8\">"
           << "<title>" << HtmlEscape(WINMTR_PRODUCT_NAME)
           << "</title><style>body{font-family:'" << WINMTR_CODE_FONT_NAME
           << "',monospace}"
           << "table{border-collapse:collapse}th,td{border:1px solid #999;padding:4px 7px}"
           << "th{background:#eee}</style></head><body><table><thead><tr>";
    for (int i = 0; i < MTR_NR_COLS; ++i)
        output << "<th>" << HtmlEscape(CStringToUtf8(LoadText(MTR_COL_RESOURCE_IDS[i]))) << "</th>";
    output << "</tr></thead><tbody>";
    const int hops = network->GetMax();
    for (int i = 0; i < hops; ++i) {
        const HopSnapshot hop = network->GetHopSnapshot(i);
        const std::string name = hop.name.empty()
            ? (hop.address.empty() ? CStringToUtf8(LoadText(IDS_NO_RESPONSE)) : hop.address)
            : hop.name;
        output << "<tr><td>" << HtmlEscape(name) << "</td><td>" << i + 1
               << "</td><td>" << hop.lossPercent << "</td><td>" << hop.xmit
               << "</td><td>" << hop.returned << "</td><td>" << hop.best
               << "</td><td>" << hop.average << "</td><td>" << hop.worst
               << "</td><td>" << hop.last << "</td><td>" << hop.jitter
               << "</td><td>" << hop.standardDeviation << "</td><td>"
               << HtmlEscape(hop.country) << "</td><td>" << HtmlEscape(hop.asn)
               << "</td><td>" << HtmlEscape(hop.isp) << "</td></tr>";
    }
    output << "</tbody></table></body></html>";
    return output.str();
}

std::string WinMTRDialog::BuildCsvReport() const
{
    std::ostringstream output;
    for (int i = 0; i < MTR_NR_COLS; ++i) {
        if (i) output << ',';
        output << CsvEscape(CStringToUtf8(LoadText(MTR_COL_RESOURCE_IDS[i])));
    }
    output << "\r\n";
    const int hops = network->GetMax();
    for (int i = 0; i < hops; ++i) {
        const HopSnapshot hop = network->GetHopSnapshot(i);
        const std::string name = hop.name.empty() ? hop.address : hop.name;
        output << CsvEscape(name) << ',' << i + 1 << ',' << hop.lossPercent << ','
               << hop.xmit << ',' << hop.returned << ',' << hop.best << ','
               << hop.average << ',' << hop.worst << ',' << hop.last << ','
               << hop.jitter << ',' << hop.standardDeviation << ','
               << CsvEscape(hop.country) << ',' << CsvEscape(hop.asn) << ','
               << CsvEscape(hop.isp) << "\r\n";
    }
    return output.str();
}

std::string WinMTRDialog::BuildJsonReport() const
{
    std::ostringstream output;
    output << "{\n  \"target\": \"";
    CString target;
    m_comboHost.GetWindowText(target);
    output << JsonEscape(CStringToUtf8(target)) << "\",\n  \"hops\": [\n";
    const int hops = network->GetMax();
    for (int i = 0; i < hops; ++i) {
        const HopSnapshot hop = network->GetHopSnapshot(i);
        if (i) output << ",\n";
        output << "    {\"hop\":" << i + 1
               << ",\"host\":\"" << JsonEscape(hop.name)
               << "\",\"ip\":\"" << JsonEscape(hop.address)
               << "\",\"loss_percent\":" << hop.lossPercent
               << ",\"sent\":" << hop.xmit << ",\"received\":" << hop.returned
               << ",\"best_ms\":" << hop.best << ",\"average_ms\":" << hop.average
               << ",\"worst_ms\":" << hop.worst << ",\"last_ms\":" << hop.last
               << ",\"jitter_ms\":" << hop.jitter << ",\"stddev_ms\":" << hop.standardDeviation
               << ",\"country\":\"" << JsonEscape(hop.country)
               << "\",\"asn\":\"" << JsonEscape(hop.asn)
               << "\",\"isp\":\"" << JsonEscape(hop.isp) << "\"}";
    }
    output << "\n  ]\n}\n";
    return output.str();
}

void WinMTRDialog::CopyTextToClipboard(const std::string& text) const
{
    const int wideSize = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wideSize <= 0 || !::OpenClipboard(GetSafeHwnd()))
        return;
    ::EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, wideSize * sizeof(wchar_t));
    if (memory) {
        wchar_t* destination = static_cast<wchar_t*>(GlobalLock(memory));
        if (destination) {
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, destination, wideSize);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    ::CloseClipboard();
}

void WinMTRDialog::SaveReport(const std::string& text, LPCTSTR extension,
    LPCTSTR filter) const
{
    CFileDialog dialog(FALSE, extension, NULL,
        OFN_HIDEREADONLY | OFN_EXPLORER | OFN_OVERWRITEPROMPT, filter,
        const_cast<WinMTRDialog*>(this));
    if (dialog.DoModal() != IDOK)
        return;
    FILE* file = fopen(dialog.GetPathName(), "wb");
    if (!file)
        return;
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
}

bool WinMTRDialog::ConfirmShareReady() const
{
    if (network->GetHopSnapshot(0).xmit >= RECOMMENDED_SHARE_PACKETS)
        return true;
    CString message;
    message.Format(LoadText(IDS_WARNING_SHARE_INCOMPLETE),
        RECOMMENDED_SHARE_PACKETS);
    return AfxMessageBox(message,
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
}

void WinMTRDialog::OnCTTC()
{
    if (ConfirmShareReady())
        CopyTextToClipboard(BuildTextReport());
}

void WinMTRDialog::OnCHTC()
{
    if (ConfirmShareReady())
        CopyTextToClipboard(BuildHtmlReport());
}

void WinMTRDialog::OnEXPT()
{
    if (ConfirmShareReady())
        SaveReport(BuildTextReport(), "txt", LoadText(IDS_FILTER_TEXT));
}

void WinMTRDialog::OnEXPH()
{
    if (ConfirmShareReady())
        SaveReport(BuildHtmlReport(), "html", LoadText(IDS_FILTER_HTML));
}

void WinMTRDialog::OnEXPC()
{
    if (ConfirmShareReady())
        SaveReport(BuildCsvReport(), "csv", LoadText(IDS_FILTER_CSV));
}

void WinMTRDialog::OnEXPJ()
{
    if (ConfirmShareReady())
        SaveReport(BuildJsonReport(), "json", LoadText(IDS_FILTER_JSON));
}

void WinMTRDialog::OnReportMenu()
{
    CMenu menu;
    if (!menu.CreatePopupMenu())
        return;
    menu.AppendMenu(MF_STRING, ID_CTTC, LoadText(IDS_MENU_COPY_TEXT));
    menu.AppendMenu(MF_STRING, ID_CHTC, LoadText(IDS_MENU_COPY_HTML));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, ID_EXPT, LoadText(IDS_MENU_EXPORT_TEXT));
    menu.AppendMenu(MF_STRING, ID_EXPH, LoadText(IDS_MENU_EXPORT_HTML));
    menu.AppendMenu(MF_STRING, ID_EXPC, LoadText(IDS_MENU_EXPORT_CSV));
    menu.AppendMenu(MF_STRING, ID_EXPJ, LoadText(IDS_MENU_EXPORT_JSON));

    CRect button;
    m_buttonReportMenu.GetWindowRect(button);
    const UINT command = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN |
        TPM_RIGHTBUTTON | TPM_RETURNCMD, button.left, button.bottom, this);
    if (command)
        SendMessage(WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void WinMTRDialog::OnCaptureScreenshot()
{
    if (!ConfirmShareReady())
        return;
    CRect window;
    GetWindowRect(window);
    const int width = window.Width();
    const int height = window.Height();
    HDC screen = ::GetDC(NULL);
    HDC memory = screen ? CreateCompatibleDC(screen) : NULL;
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, width, height) : NULL;
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : NULL;
    bool captured = bitmap && ::PrintWindow(GetSafeHwnd(), memory, 0) != FALSE;
    if (previous)
        SelectObject(memory, previous);

    if (captured && !BitmapHasVisualContent(memory, bitmap, width, height))
        captured = false;
    if (!captured && bitmap) {
        previous = SelectObject(memory, bitmap);
        captured = BitBlt(memory, 0, 0, width, height, screen, window.left,
            window.top, SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(memory, previous);
    }

    bool copied = false;
    if (captured && ::OpenClipboard(GetSafeHwnd())) {
        ::EmptyClipboard();
        copied = SetClipboardData(CF_BITMAP, bitmap) != NULL;
        ::CloseClipboard();
        if (copied)
            bitmap = NULL;
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (memory)
        DeleteDC(memory);
    if (screen)
        ::ReleaseDC(NULL, screen);

    SetTraceStatus(
        LoadText(copied ? IDS_STATUS_SCREENSHOT_COPIED : IDS_STATUS_SCREENSHOT_FAILED));
}

void WinMTRDialog::AdjustColumnWidths()
{
    if (!::IsWindow(m_listMTR.GetSafeHwnd()))
        return;
    CClientDC dc(&m_listMTR);
    CFont* previous = dc.SelectObject(&m_tableFont);
    const int itemCount = m_listMTR.GetItemCount();
    for (int column = 0; column < MTR_NR_COLS; ++column) {
        int width = static_cast<int>(
            dc.GetTextExtent(LoadText(MTR_COL_RESOURCE_IDS[column])).cx) + 22;
        for (int item = 0; item < itemCount; ++item)
            width = std::max(width,
                static_cast<int>(dc.GetTextExtent(
                    m_listMTR.GetItemText(item, column)).cx) + 20);
        m_listMTR.SetColumnWidth(column, std::max(40, width));
    }
    dc.SelectObject(previous);
}

int WinMTRDialog::CalculateMinimumWindowWidth()
{
    if (!::IsWindow(m_comboHost.GetSafeHwnd()) ||
        !::IsWindow(m_buttonStart.GetSafeHwnd()) ||
        !::IsWindow(m_buttonOptions.GetSafeHwnd()) ||
        !::IsWindow(m_buttonReset.GetSafeHwnd()) ||
        !::IsWindow(m_buttonCapture.GetSafeHwnd()) ||
        !::IsWindow(m_buttonReportMenu.GetSafeHwnd())) {
        return minimumWindowSize.cx;
    }

    CRect host;
    CRect start;
    CRect options;
    CRect reset;
    CRect capture;
    CRect report;
    m_comboHost.GetWindowRect(host);
    m_buttonStart.GetWindowRect(start);
    m_buttonOptions.GetWindowRect(options);
    m_buttonReset.GetWindowRect(reset);
    m_buttonCapture.GetWindowRect(capture);
    m_buttonReportMenu.GetWindowRect(report);
    ScreenToClient(host);

    const int controlGap = 6;
    const int groupPadding = 6;
    const int groupGap = 6;
    const int rightMargin = 14;
    const int minimumHostWidth = 60;
    const int actionButtonsWidth = options.Width() + reset.Width() +
        capture.Width() + report.Width() + 3 * controlGap;
    const int minimumClientWidth = host.left + minimumHostWidth + controlGap +
        start.Width() + 2 * groupPadding + groupGap +
        actionButtonsWidth + rightMargin;

    CRect client;
    CRect window;
    GetClientRect(client);
    GetWindowRect(window);
    return minimumClientWidth + window.Width() - client.Width();
}

void WinMTRDialog::AdjustWindowToContent()
{
    if (adjustingWindow || !::IsWindow(m_listMTR.GetSafeHwnd()))
        return;
    const int itemCount = m_listMTR.GetItemCount();
    const bool hasRows = itemCount > 0;
    m_listMTR.ShowWindow(hasRows ? SW_SHOW : SW_HIDE);

    int contentWidth = 0;
    if (hasRows) {
        contentWidth = GetSystemMetrics(SM_CXVSCROLL) + 6;
        for (int column = 0; column < MTR_NR_COLS; ++column)
            contentWidth += m_listMTR.GetColumnWidth(column);
    }

    if (::IsWindow(m_publicIpSummary.GetSafeHwnd()) &&
        ::IsWindow(m_publicHostnameSummary.GetSafeHwnd()) &&
        ::IsWindow(m_publicCountrySummary.GetSafeHwnd()) &&
        ::IsWindow(m_publicCitySummary.GetSafeHwnd()) &&
        ::IsWindow(m_publicAsnSummary.GetSafeHwnd()) &&
        ::IsWindow(m_publicIspSummary.GetSafeHwnd()) &&
        ::IsWindow(m_buttonNetworkDetails.GetSafeHwnd())) {
        CClientDC dc(this);
        CFont* previous = dc.SelectObject(&m_codeFont);
        CString text;
        m_publicIpSummary.GetWindowText(text);
        const int ipWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        m_publicHostnameSummary.GetWindowText(text);
        const int hostnameWidth = std::max(110,
            static_cast<int>(dc.GetTextExtent(text).cx) + 8);
        dc.SelectObject(GetFont());
        m_publicCountrySummary.GetWindowText(text);
        const int countryWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        m_publicCitySummary.GetWindowText(text);
        const int cityWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        dc.SelectObject(&m_codeFont);
        m_publicAsnSummary.GetWindowText(text);
        const int asnWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        const int connectionWidth = std::max(150, std::max(ipWidth, hostnameWidth));
        const int locationWidth = std::max(100, std::max(countryWidth, cityWidth));
        const int organizationWidth = std::max(100, asnWidth);
        dc.SelectObject(previous);
        CRect details;
        m_buttonNetworkDetails.GetWindowRect(details);
        const int summaryWidth = connectionWidth + locationWidth +
            organizationWidth + details.Width() + 18;
        contentWidth = std::max(contentWidth, summaryWidth);
    }

    int requiredListHeight = 0;
    if (hasRows) {
        int rowHeight = 18;
        CRect row;
        if (m_listMTR.GetItemRect(0, row, LVIR_BOUNDS))
            rowHeight = std::max(rowHeight, static_cast<int>(row.Height()));
        int headerHeight = 20;
        CHeaderCtrl* header = m_listMTR.GetHeaderCtrl();
        CRect headerRect;
        if (header) {
            header->GetWindowRect(headerRect);
            headerHeight = std::max(headerHeight,
                static_cast<int>(headerRect.Height()));
        }
        CRect listClient;
        CRect listWindow;
        m_listMTR.GetClientRect(listClient);
        m_listMTR.GetWindowRect(listWindow);
        const int listFrameHeight = listWindow.Height() - listClient.Height();
        requiredListHeight = headerHeight + itemCount * rowHeight +
            listFrameHeight + 2;
    }

    CRect client;
    GetClientRect(client);
    CRect current;
    GetWindowRect(current);
    const int nonClientWidth = current.Width() - client.Width();
    int desiredWidth = contentWidth > 0
        ? contentWidth + nonClientWidth + 16
        : minimumWindowSize.cx;
    int desiredHeight = minimumWindowSize.cy;
    if (hasRows) {
        CRect listPosition;
        m_listMTR.GetWindowRect(listPosition);
        ScreenToClient(listPosition);
        int footerHeight = 0;
        if (::IsWindow(footerStatus.GetSafeHwnd())) {
            CRect footer;
            footerStatus.GetWindowRect(footer);
            footerHeight = footer.Height();
        }
        const int nonClientHeight = current.Height() - client.Height();
        desiredHeight = nonClientHeight + listPosition.top +
            requiredListHeight + footerHeight + 8;
    }
    const int minimumWidth = std::max(static_cast<int>(minimumWindowSize.cx),
        CalculateMinimumWindowWidth());
    desiredWidth = std::max(desiredWidth, minimumWidth);
    desiredHeight = std::max(desiredHeight, static_cast<int>(minimumWindowSize.cy));

    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfo(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST),
        &monitor)) {
        return;
    }
    const int workWidth = static_cast<int>(monitor.rcWork.right - monitor.rcWork.left);
    const int workHeight = static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top);
    if (desiredWidth > workWidth)
        desiredHeight += GetSystemMetrics(SM_CYHSCROLL);
    desiredWidth = std::min(desiredWidth, workWidth);
    desiredHeight = std::min(desiredHeight, workHeight);
    int left = std::max(static_cast<int>(monitor.rcWork.left),
        std::min(static_cast<int>(current.left),
            static_cast<int>(monitor.rcWork.right) - desiredWidth));
    int top = std::max(static_cast<int>(monitor.rcWork.top),
        std::min(static_cast<int>(current.top),
            static_cast<int>(monitor.rcWork.bottom) - desiredHeight));

    if (current.Width() != desiredWidth || current.Height() != desiredHeight ||
        current.left != left || current.top != top) {
        adjustingWindow = true;
        SetWindowPos(NULL, left, top, desiredWidth, desiredHeight,
            SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOZORDER);
        adjustingWindow = false;

        // A second layout after the outer resize prevents child controls from
        // retaining an intermediate position or copied resize artifacts.
        CRect resizedClient;
        GetClientRect(resizedClient);
        SendMessage(WM_SIZE, SIZE_RESTORED,
            MAKELPARAM(resizedClient.Width(), resizedClient.Height()));
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE |
            RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    StretchLastColumnToFill();
}

void WinMTRDialog::StretchLastColumnToFill()
{
    if (!::IsWindow(m_listMTR.GetSafeHwnd()))
        return;
    CRect client;
    m_listMTR.GetClientRect(client);
    int usedWidth = 0;
    for (int column = 0; column < MTR_NR_COLS; ++column)
        usedWidth += m_listMTR.GetColumnWidth(column);
    const int availableWidth = client.Width() - GetSystemMetrics(SM_CXVSCROLL) - 2;
    if (availableWidth > usedWidth) {
        const int lastColumn = MTR_NR_COLS - 1;
        m_listMTR.SetColumnWidth(lastColumn,
            m_listMTR.GetColumnWidth(lastColumn) + availableWidth - usedWidth);
    }
}

void WinMTRDialog::SetHostControlTracing(bool tracing)
{
    if (!::IsWindow(m_traceHostDisplay.GetSafeHwnd())) {
        m_comboHost.EnableWindow(tracing ? FALSE : TRUE);
        return;
    }
    if (tracing) {
        CString host;
        m_comboHost.GetWindowText(host);
        m_traceHostDisplay.SetWindowText(host);
        m_comboHost.ShowWindow(SW_HIDE);
        m_traceHostDisplay.ShowWindow(SW_SHOW);
    } else {
        m_traceHostDisplay.ShowWindow(SW_HIDE);
        m_comboHost.ShowWindow(SW_SHOW);
        m_comboHost.EnableWindow(TRUE);
    }
}

void WinMTRDialog::ClearHistory()
{
    HKEY key = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\WinMTR\\LRU", 0,
        KEY_WRITE, &key) == ERROR_SUCCESS) {
        for (int i = 1; i <= maxLRU; ++i) {
            char name[32] = {};
            sprintf_s(name, "Host%d", i);
            RegDeleteValueA(key, name);
        }
        nrLRU = 0;
        WriteDword(key, "NrLRU", 0);
        RegCloseKey(key);
    }
    m_comboHost.ResetContent();
    m_comboHost.AddString(LoadText(IDS_STRING_CLEAR_HISTORY));
}

void WinMTRDialog::OnCbnSelchangeComboHost() {}
void WinMTRDialog::OnCbnSelendokComboHost() {}

void WinMTRDialog::OnCbnCloseupComboHost()
{
    if (m_comboHost.GetCurSel() == m_comboHost.GetCount() - 1)
        ClearHistory();
}

void WinMTRDialog::OnTimer(UINT_PTR eventId)
{
    static unsigned int ticks = 0;
    ++ticks;
    if (traceThread && WaitForSingleObject(traceThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(traceThread);
        traceThread = NULL;
        if (state != EXIT)
            Transit(IDLE);
    }
    if (publicInfoThread &&
        WaitForSingleObject(publicInfoThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(publicInfoThread);
        publicInfoThread = NULL;
    }
    if (state == EXIT && !traceThread && !publicInfoThread) {
        OnOK();
        return;
    } else if ((state == TRACING || state == STOPPING) && ticks % 10 == 0) {
        DisplayRedraw();
    }
    CDialog::OnTimer(eventId);
}

void WinMTRDialog::OnClose() { Transit(EXIT); }
void WinMTRDialog::OnCancel() {}

void WinMTRDialog::OnSizing(UINT side, LPRECT rectangle)
{
    CDialog::OnSizing(side, rectangle);
    int minimumHeight = minimumWindowSize.cy;
    if (::IsWindow(m_listMTR.GetSafeHwnd()) && m_listMTR.GetItemCount() > 0) {
        int rowHeight = 18;
        CRect row;
        if (m_listMTR.GetItemRect(0, row, LVIR_BOUNDS))
            rowHeight = std::max(rowHeight, static_cast<int>(row.Height()));
        int headerHeight = 20;
        CHeaderCtrl* header = m_listMTR.GetHeaderCtrl();
        CRect headerRect;
        if (header) {
            header->GetWindowRect(headerRect);
            headerHeight = std::max(headerHeight,
                static_cast<int>(headerRect.Height()));
        }
        minimumHeight += headerHeight + rowHeight + 4;
    }
    const int minimumWidth = std::max(static_cast<int>(minimumWindowSize.cx),
        CalculateMinimumWindowWidth());
    if (minimumWidth > 0 &&
        rectangle->right - rectangle->left < minimumWidth)
        rectangle->right = rectangle->left + minimumWidth;
    if (minimumHeight > 0 && rectangle->bottom - rectangle->top < minimumHeight)
        rectangle->bottom = rectangle->top + minimumHeight;
}

void WinMTRDialog::OnSize(UINT type, int width, int height)
{
    CDialog::OnSize(type, width, height);
    if (type != SIZE_MINIMIZED && !adjustingWindow) {
        CRect window;
        GetWindowRect(window);
        const int minimumWidth = std::max(
            static_cast<int>(minimumWindowSize.cx),
            CalculateMinimumWindowWidth());
        if (minimumWidth > 0 && window.Width() < minimumWidth) {
            adjustingWindow = true;
            SetWindowPos(NULL, 0, 0, minimumWidth, window.Height(),
                SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOZORDER);
            adjustingWindow = false;
            RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE |
                RDW_ALLCHILDREN | RDW_UPDATENOW);
            return;
        }
    }
    CRect client;
    GetClientRect(&client);
    int footerHeight = 0;
    if (::IsWindow(footerStatus.GetSafeHwnd())) {
        CRect footer;
        footerStatus.GetWindowRect(footer);
        footerHeight = static_cast<int>(footer.Height());
    }
    const bool hasRows = ::IsWindow(m_listMTR.m_hWnd) &&
        m_listMTR.GetItemCount() > 0;
    CRect item;
    if (::IsWindow(m_staticJ.m_hWnd)) {
        m_staticJ.GetWindowRect(&item);
        ScreenToClient(&item);
        int groupHeight = item.Height();
        if (!hasRows && ::IsWindow(m_staticS.m_hWnd)) {
            CRect topGroup;
            m_staticS.GetWindowRect(topGroup);
            ScreenToClient(topGroup);
            const int sectionGap = item.top - topGroup.bottom;
            CRect dialogUnit(0, 0, 0, 1);
            ::MapDialogRect(GetSafeHwnd(), &dialogUnit);
            const int visualOffset = std::max(1,
                static_cast<int>(dialogUnit.bottom));
            groupHeight = std::max(1,
                static_cast<int>(client.Height() - footerHeight -
                    sectionGap - visualOffset - item.top));
        }
        m_staticJ.SetWindowPos(NULL, 0, 0, client.Width() - item.left - 8,
            groupHeight, SWP_NOMOVE | SWP_NOZORDER);
    }
    if (hasRows) {
        m_listMTR.GetWindowRect(&item);
        ScreenToClient(&item);
        m_listMTR.ShowWindow(SW_SHOW);
        m_listMTR.SetWindowPos(NULL, 0, 0, client.Width() - item.left - 8,
            client.Height() - item.top - footerHeight - 8,
            SWP_NOMOVE | SWP_NOZORDER);
        StretchLastColumnToFill();
    } else if (::IsWindow(m_listMTR.m_hWnd)) {
        m_listMTR.ShowWindow(SW_HIDE);
    }
    if (::IsWindow(m_buttonStart.m_hWnd) &&
        ::IsWindow(m_buttonOptions.m_hWnd) &&
        ::IsWindow(m_buttonCapture.m_hWnd) &&
        ::IsWindow(m_buttonReportMenu.m_hWnd) &&
        ::IsWindow(m_buttonReset.m_hWnd)) {
        CRect start;
        CRect options;
        CRect capture;
        CRect report;
        CRect reset;
        CRect host;
        m_buttonStart.GetWindowRect(start);
        m_buttonOptions.GetWindowRect(options);
        m_buttonCapture.GetWindowRect(capture);
        m_buttonReportMenu.GetWindowRect(report);
        m_buttonReset.GetWindowRect(reset);
        m_comboHost.GetWindowRect(host);
        ScreenToClient(start);
        ScreenToClient(options);
        ScreenToClient(capture);
        ScreenToClient(report);
        ScreenToClient(reset);
        ScreenToClient(host);
        const int gap = 6;
        const int reportLeft = client.Width() - 14 - report.Width();
        const int captureLeft = reportLeft - gap - capture.Width();
        const int resetLeft = captureLeft - gap - reset.Width();
        const int optionsLeft = resetLeft - gap - options.Width();
        const int groupPadding = 6;
        const int groupGap = 6;
        const int actionsLeft = optionsLeft - groupPadding;
        const int startLeft = actionsLeft - groupGap - groupPadding - start.Width();
        const int hostWidth = std::max(60,
            startLeft - gap - static_cast<int>(host.left));
        m_comboHost.SetWindowPos(NULL, 0, 0, hostWidth, host.Height(),
            SWP_NOMOVE | SWP_NOZORDER);
        if (::IsWindow(m_traceHostDisplay.GetSafeHwnd())) {
            m_traceHostDisplay.SetWindowPos(NULL, host.left, host.top,
                hostWidth, host.Height(), SWP_NOZORDER);
        }
        m_buttonStart.SetWindowPos(NULL, startLeft, start.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
        m_buttonOptions.SetWindowPos(NULL, optionsLeft, options.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
        m_buttonReset.SetWindowPos(NULL, resetLeft, reset.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
        m_buttonReportMenu.SetWindowPos(NULL, reportLeft, report.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
        m_buttonCapture.SetWindowPos(NULL, captureLeft, capture.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);

        if (::IsWindow(m_staticS.m_hWnd)) {
            CRect group;
            m_staticS.GetWindowRect(group);
            ScreenToClient(group);
            const int groupRight = startLeft + start.Width() + groupPadding;
            m_staticS.SetWindowPos(NULL, 0, 0, groupRight - group.left,
                group.Height(), SWP_NOMOVE | SWP_NOZORDER);
        }
        if (::IsWindow(m_staticActions.m_hWnd)) {
            CRect group;
            m_staticActions.GetWindowRect(group);
            ScreenToClient(group);
            m_staticActions.SetWindowPos(NULL, actionsLeft, group.top,
                client.Width() - 8 - actionsLeft, group.Height(), SWP_NOZORDER);
        }
    }
    if (::IsWindow(m_publicIpSummary.m_hWnd) &&
        ::IsWindow(m_publicHostnameSummary.m_hWnd) &&
        ::IsWindow(m_publicCountrySummary.m_hWnd) &&
        ::IsWindow(m_publicCitySummary.m_hWnd) &&
        ::IsWindow(m_publicAsnSummary.m_hWnd) &&
        ::IsWindow(m_publicIspSummary.m_hWnd) &&
        ::IsWindow(m_buttonNetworkDetails.m_hWnd)) {
        CRect ip;
        CRect hostname;
        CRect country;
        CRect city;
        CRect asn;
        CRect isp;
        CRect details;
        m_publicIpSummary.GetWindowRect(ip);
        m_publicHostnameSummary.GetWindowRect(hostname);
        m_publicCountrySummary.GetWindowRect(country);
        m_publicCitySummary.GetWindowRect(city);
        m_publicAsnSummary.GetWindowRect(asn);
        m_publicIspSummary.GetWindowRect(isp);
        m_buttonNetworkDetails.GetWindowRect(details);
        ScreenToClient(ip);
        ScreenToClient(hostname);
        ScreenToClient(country);
        ScreenToClient(city);
        ScreenToClient(asn);
        ScreenToClient(isp);
        ScreenToClient(details);

        const int gap = 6;
        const int detailsLeft = client.Width() - 14 - details.Width();
        m_buttonNetworkDetails.SetWindowPos(NULL, detailsLeft, details.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);

        CClientDC dc(this);
        CFont* previous = dc.SelectObject(&m_codeFont);
        CString text;
        m_publicIpSummary.GetWindowText(text);
        const int ipWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        m_publicHostnameSummary.GetWindowText(text);
        const int hostnameWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        dc.SelectObject(GetFont());
        m_publicCountrySummary.GetWindowText(text);
        const int countryWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        m_publicCitySummary.GetWindowText(text);
        const int cityWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        dc.SelectObject(&m_codeFont);
        m_publicAsnSummary.GetWindowText(text);
        const int asnWidth = static_cast<int>(dc.GetTextExtent(text).cx) + 8;
        dc.SelectObject(previous);

        int connectionWidth = std::max(150, std::max(ipWidth, hostnameWidth));
        int locationWidth = std::max(100, std::max(countryWidth, cityWidth));
        const int staticsWidth = detailsLeft - gap - ip.left - 2 * gap;
        int organizationWidth = std::max(asnWidth,
            staticsWidth - connectionWidth - locationWidth);
        if (organizationWidth < 64) {
            int overflow = 64 - organizationWidth;
            const int locationReduction = std::min(overflow,
                std::max(0, locationWidth - 90));
            locationWidth -= locationReduction;
            overflow -= locationReduction;
            const int connectionReduction = std::min(overflow,
                std::max(0, connectionWidth - 130));
            connectionWidth -= connectionReduction;
            organizationWidth = std::max(64,
                staticsWidth - connectionWidth - locationWidth);
        }

        int left = ip.left;
        m_publicIpSummary.SetWindowPos(NULL, left, ip.top,
            connectionWidth, ip.Height(), SWP_NOZORDER);
        m_publicHostnameSummary.SetWindowPos(NULL, left, hostname.top,
            connectionWidth, hostname.Height(), SWP_NOZORDER);
        left += connectionWidth + gap;
        m_publicCountrySummary.SetWindowPos(NULL, left, country.top,
            locationWidth, country.Height(), SWP_NOZORDER);
        m_publicCitySummary.SetWindowPos(NULL, left, city.top,
            locationWidth, city.Height(), SWP_NOZORDER);
        left += locationWidth + gap;
        m_publicAsnSummary.SetWindowPos(NULL, left, asn.top,
            organizationWidth, asn.Height(), SWP_NOZORDER);
        m_publicIspSummary.SetWindowPos(NULL, left, isp.top,
            organizationWidth, isp.Height(), SWP_NOZORDER);
    }
    RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0);
}

void WinMTRDialog::OnPaint()
{
    if (IsIconic()) {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
        const int x = (GetSystemMetrics(SM_CXICON) - GetSystemMetrics(SM_CXICON)) / 2;
        const int y = (GetSystemMetrics(SM_CYICON) - GetSystemMetrics(SM_CYICON)) / 2;
        dc.DrawIcon(x, y, m_hIcon);
    } else {
        CDialog::OnPaint();
    }
}

HCURSOR WinMTRDialog::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}
