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
    ON_BN_CLICKED(ID_CTTC, OnCTTC)
    ON_BN_CLICKED(ID_CHTC, OnCHTC)
    ON_BN_CLICKED(ID_EXPT, OnEXPT)
    ON_BN_CLICKED(ID_EXPH, OnEXPH)
    ON_BN_CLICKED(ID_EXPC, OnEXPC)
    ON_BN_CLICKED(ID_EXPJ, OnEXPJ)
    ON_BN_CLICKED(ID_RESET_STATS, OnResetStats)
    ON_BN_CLICKED(ID_NETWORK_INFO, OnNetworkInfo)
    ON_NOTIFY(NM_DBLCLK, IDC_LIST_MTR, OnDblclkList)
    ON_CBN_SELCHANGE(IDC_COMBO_HOST, OnCbnSelchangeComboHost)
    ON_CBN_SELENDOK(IDC_COMBO_HOST, OnCbnSelendokComboHost)
    ON_CBN_CLOSEUP(IDC_COMBO_HOST, OnCbnCloseupComboHost)
    ON_BN_CLICKED(IDCANCEL, OnBnClickedCancel)
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
      hasIntervalFromCmdLine(false), hasPingSizeFromCmdLine(false),
      hasMaxLRUFromCmdLine(false), hasUseDNSFromCmdLine(false),
      publicInfoQueryStarted(false), m_autostart(0), network(new WinMTRNet()),
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
    DDX_Control(pDX, IDCANCEL, m_buttonExit);
    DDX_Control(pDX, ID_RESTART, m_buttonStart);
    DDX_Control(pDX, ID_NETWORK_INFO, m_buttonNetworkInfo);
    DDX_Control(pDX, ID_RESET_STATS, m_buttonReset);
    DDX_Control(pDX, IDC_COMBO_HOST, m_comboHost);
    DDX_Control(pDX, IDC_LIST_MTR, m_listMTR);
    DDX_Control(pDX, IDC_STATICS, m_staticS);
    DDX_Control(pDX, IDC_STATICJ, m_staticJ);
    DDX_Control(pDX, ID_EXPH, m_buttonExpH);
    DDX_Control(pDX, ID_EXPT, m_buttonExpT);
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

BOOL WinMTRDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

#ifdef _WIN64
    const CString caption = LoadText(IDS_WINDOW_TITLE_64);
#else
    const CString caption = LoadText(IDS_WINDOW_TITLE_32);
#endif
    SetWindowText(caption);
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    SetTimer(1, WINMTR_DIALOG_TIMER, NULL);

    if (!statusBar.Create(this))
        return FALSE;
    statusBar.GetStatusBarCtrl().SetMinHeight(24);
    const UINT indicators[2] = { IDS_STATUS_READY, IDS_STATUS_PUBLIC_IP_QUERYING };
    statusBar.SetIndicators(indicators, 2);
    statusBar.SetPaneInfo(0, statusBar.GetItemID(0), SBPS_STRETCH, 0);
    statusBar.SetPaneInfo(1, statusBar.GetItemID(1), SBPS_NORMAL, 220);

    m_codeFont.CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, WINMTR_CODE_FONT_NAME);
    m_listMTR.SetFont(&m_codeFont);
    m_comboHost.SetFont(&m_codeFont);
    m_listMTR.SetExtendedStyle(m_listMTR.GetExtendedStyle() |
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < MTR_NR_COLS; ++i)
        m_listMTR.InsertColumn(i, LoadText(MTR_COL_RESOURCE_IDS[i]), LVCFMT_LEFT,
            MTR_COL_LENGTH[i], -1);

    CRect clientStart;
    CRect clientNow;
    GetClientRect(clientStart);
    RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0,
        reposQuery, clientNow);
    const CPoint offset(clientNow.left - clientStart.left,
        clientNow.top - clientStart.top);
    CRect childRect;
    for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow()) {
        child->GetWindowRect(childRect);
        ScreenToClient(childRect);
        childRect.OffsetRect(offset);
        child->MoveWindow(childRect, FALSE);
    }
    CRect window;
    GetWindowRect(window);
    window.right += clientStart.Width() - clientNow.Width();
    window.bottom += clientStart.Height() - clientNow.Height();
    MoveWindow(window, FALSE);
    RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0);

    InitRegistry();
    m_buttonNetworkInfo.EnableWindow(FALSE);
    if (lookupPublicInfo)
        StartPublicInfoLookup();
    else
        statusBar.SetPaneText(1, LoadText(IDS_NETWORK_INFO_UNAVAILABLE));

    m_comboHost.SetFocus();
    if (m_autostart) {
        m_comboHost.SetWindowText(defaultHostName);
        OnRestart();
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
    statusBar.SetPaneText(0, status);

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = NULL;
    if (getaddrinfo(hostText, NULL, &hints, &addresses) != 0 || !addresses) {
        statusBar.SetPaneText(0, LoadText(IDS_STATUS_READY));
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
        m_comboHost.EnableWindow(FALSE);
        m_buttonOptions.EnableWindow(FALSE);
        statusBar.SetPaneText(0, LoadText(IDS_STATUS_TRACING));
        DialogTraceContext* context = new DialogTraceContext;
        context->network = network;
        context->target = traceTarget;
        context->config = CurrentTraceConfig();
        const uintptr_t handle = _beginthreadex(NULL, 0, PingThread, context, 0, NULL);
        if (handle == 0) {
            delete context;
            state = IDLE;
            m_buttonStart.SetWindowText(LoadText(IDS_BUTTON_START));
            m_comboHost.EnableWindow(TRUE);
            m_buttonOptions.EnableWindow(TRUE);
            return;
        }
        traceThread = reinterpret_cast<HANDLE>(handle);
    } else if (oldState == TRACING && newState == STOPPING) {
        transition = TRACING_TO_STOPPING;
        m_buttonStart.EnableWindow(FALSE);
        network->StopTrace();
        statusBar.SetPaneText(0, LoadText(IDS_STATUS_STOPPING));
        DisplayRedraw();
    } else if ((oldState == STOPPING || oldState == TRACING) && newState == IDLE) {
        transition = STOPPING_TO_IDLE;
        state = IDLE;
        m_buttonStart.EnableWindow(TRUE);
        m_buttonStart.SetWindowText(LoadText(IDS_BUTTON_START));
        m_comboHost.EnableWindow(TRUE);
        m_buttonOptions.EnableWindow(TRUE);
        statusBar.SetPaneText(0, LoadText(IDS_STATUS_READY));
        DisplayRedraw();
    } else if (newState == EXIT) {
        transition = oldState == TRACING ? TRACING_TO_EXIT
            : oldState == STOPPING ? STOPPING_TO_EXIT : IDLE_TO_EXIT;
        m_buttonStart.EnableWindow(FALSE);
        m_comboHost.EnableWindow(FALSE);
        m_buttonOptions.EnableWindow(FALSE);
        network->StopTrace();
        if (traceThread)
            statusBar.SetPaneText(0, LoadText(IDS_STATUS_STOPPING));
    }
}

int WinMTRDialog::DisplayRedraw()
{
    const int hops = network->GetMax();
    while (m_listMTR.GetItemCount() > hops)
        m_listMTR.DeleteItem(m_listMTR.GetItemCount() - 1);

    for (int i = 0; i < hops; ++i) {
        const HopSnapshot hop = network->GetHopSnapshot(i);
        CString name = hop.name.empty() ? Utf8ToLocal(hop.address) : Utf8ToLocal(hop.name);
        if (name.IsEmpty())
            name = LoadText(IDS_NO_RESPONSE);
        if (m_listMTR.GetItemCount() <= i)
            m_listMTR.InsertItem(i, name);
        else
            m_listMTR.SetItemText(i, 0, name);

        CString value;
        const int numericValues[10] = {
            i + 1, hop.lossPercent, hop.xmit, hop.returned, hop.best,
            hop.average, hop.worst, hop.last, hop.jitter, hop.standardDeviation
        };
        for (int column = 1; column <= 10; ++column) {
            value.Format("%d", numericValues[column - 1]);
            m_listMTR.SetItemText(i, column, value);
        }
        m_listMTR.SetItemText(i, 11, Utf8ToLocal(hop.country));
        m_listMTR.SetItemText(i, 12, Utf8ToLocal(hop.asn));
        m_listMTR.SetItemText(i, 13, Utf8ToLocal(hop.isp));
    }
    return 0;
}

void WinMTRDialog::OnDblclkList(NMHDR*, LRESULT* result)
{
    POSITION position = m_listMTR.GetFirstSelectedItemPosition();
    if (position) {
        const int item = m_listMTR.GetNextSelectedItem(position);
        const HopSnapshot hop = network->GetHopSnapshot(item);
        WinMTRProperties properties(this);
        strncpy_s(properties.host,
            hop.name.empty() ? hop.address.c_str() : hop.name.c_str(), _TRUNCATE);
        strncpy_s(properties.ip, hop.address.c_str(), _TRUNCATE);
        std::string comment;
        if (!hop.country.empty()) comment += hop.country;
        if (!hop.asn.empty()) comment += "  AS" + hop.asn;
        if (!hop.isp.empty()) comment += "  " + hop.isp;
        if (comment.empty() && !hop.hasAddress)
            comment = CStringToUtf8(LoadText(IDS_NO_RESPONSE));
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
}

void WinMTRDialog::StartPublicInfoLookup()
{
    if (publicInfoQueryStarted)
        return;
    publicInfoQueryStarted = true;
    statusBar.SetPaneText(1, LoadText(IDS_STATUS_PUBLIC_IP_QUERYING));
    PublicInfoContext* context = new PublicInfoContext;
    context->window = GetSafeHwnd();
    const uintptr_t handle = _beginthreadex(NULL, 0, PublicInfoThread, context, 0, NULL);
    if (handle) {
        publicInfoThread = reinterpret_cast<HANDLE>(handle);
    } else {
        delete context;
        publicInfoQueryStarted = false;
        statusBar.SetPaneText(1, LoadText(IDS_STATUS_PUBLIC_IP_FAILED));
    }
}

LRESULT WinMTRDialog::OnPublicNetworkInfoReady(WPARAM, LPARAM value)
{
    delete publicNetworkInfo;
    publicNetworkInfo = reinterpret_cast<PublicNetworkInfo*>(value);
    const std::string address = publicNetworkInfo->ipv4.available
        ? publicNetworkInfo->ipv4.address : publicNetworkInfo->ipv6.address;
    if (address.empty()) {
        statusBar.SetPaneText(1, LoadText(IDS_STATUS_PUBLIC_IP_FAILED));
    } else {
        CString status;
        status.Format(LoadText(IDS_STATUS_PUBLIC_IP_FORMAT),
            static_cast<LPCTSTR>(Utf8ToLocal(address)));
        statusBar.SetPaneText(1, status);
        m_buttonNetworkInfo.EnableWindow(TRUE);
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

void WinMTRDialog::OnCTTC() { CopyTextToClipboard(BuildTextReport()); }
void WinMTRDialog::OnCHTC() { CopyTextToClipboard(BuildHtmlReport()); }
void WinMTRDialog::OnEXPT() { SaveReport(BuildTextReport(), "txt", LoadText(IDS_FILTER_TEXT)); }
void WinMTRDialog::OnEXPH() { SaveReport(BuildHtmlReport(), "html", LoadText(IDS_FILTER_HTML)); }
void WinMTRDialog::OnEXPC() { SaveReport(BuildCsvReport(), "csv", LoadText(IDS_FILTER_CSV)); }
void WinMTRDialog::OnEXPJ() { SaveReport(BuildJsonReport(), "json", LoadText(IDS_FILTER_JSON)); }

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
void WinMTRDialog::OnBnClickedCancel() { Transit(EXIT); }
void WinMTRDialog::OnCancel() {}

void WinMTRDialog::OnSizing(UINT side, LPRECT rectangle)
{
    CDialog::OnSizing(side, rectangle);
    if (rectangle->right - rectangle->left < 850)
        rectangle->right = rectangle->left + 850;
    if (rectangle->bottom - rectangle->top < 500)
        rectangle->bottom = rectangle->top + 500;
}

void WinMTRDialog::OnSize(UINT type, int width, int height)
{
    CDialog::OnSize(type, width, height);
    CRect client;
    GetClientRect(&client);
    CRect item;
    if (::IsWindow(m_staticS.m_hWnd)) {
        m_staticS.GetWindowRect(&item);
        ScreenToClient(&item);
        m_staticS.SetWindowPos(NULL, 0, 0, client.Width() - item.left - 10,
            item.Height(), SWP_NOMOVE | SWP_NOZORDER);
    }
    if (::IsWindow(m_staticJ.m_hWnd)) {
        m_staticJ.GetWindowRect(&item);
        ScreenToClient(&item);
        m_staticJ.SetWindowPos(NULL, 0, 0, client.Width() - item.left - 10,
            item.Height(), SWP_NOMOVE | SWP_NOZORDER);
    }
    if (::IsWindow(m_listMTR.m_hWnd)) {
        m_listMTR.GetWindowRect(&item);
        ScreenToClient(&item);
        m_listMTR.SetWindowPos(NULL, 0, 0, client.Width() - item.left - 10,
            client.Height() - item.top - 24, SWP_NOMOVE | SWP_NOZORDER);
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
