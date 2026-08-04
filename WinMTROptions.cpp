#include "WinMTRGlobal.h"
#include "WinMTROptions.h"
#include "WinMTRLicense.h"

BEGIN_MESSAGE_MAP(WinMTROptions, CDialog)
    ON_BN_CLICKED(ID_LICENSE, OnLicense)
END_MESSAGE_MAP()

WinMTROptions::WinMTROptions(CWnd* pParent)
    : CDialog(WinMTROptions::IDD, pParent), interval(DEFAULT_INTERVAL),
      pingSize(DEFAULT_PING_SIZE), maxLRU(DEFAULT_MAX_LRU),
      maxHops(DEFAULT_MAX_HOPS), timeoutMs(DEFAULT_TIMEOUT_MS),
      cycles(DEFAULT_CYCLES), tos(DEFAULT_TOS), bitPattern(DEFAULT_BIT_PATTERN),
      useDNS(DEFAULT_DNS), dontFragment(DEFAULT_DONT_FRAGMENT),
      lookupAsn(DEFAULT_ASN_LOOKUP),
      lookupPublicInfo(WINMTR_ENABLE_PUBLIC_IP_LOOKUP_DEFAULT ? TRUE : FALSE)
{
}

void WinMTROptions::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_SIZE, m_editSize);
    DDX_Control(pDX, IDC_EDIT_INTERVAL, m_editInterval);
    DDX_Control(pDX, IDC_EDIT_MAX_LRU, m_editMaxLRU);
    DDX_Control(pDX, IDC_EDIT_MAX_HOPS, m_editMaxHops);
    DDX_Control(pDX, IDC_EDIT_TIMEOUT, m_editTimeout);
    DDX_Control(pDX, IDC_EDIT_CYCLES, m_editCycles);
    DDX_Control(pDX, IDC_EDIT_TOS, m_editTos);
    DDX_Control(pDX, IDC_EDIT_PATTERN, m_editPattern);
    DDX_Control(pDX, IDC_CHECK_DNS, m_checkDNS);
    DDX_Control(pDX, IDC_CHECK_DF, m_checkDF);
    DDX_Control(pDX, IDC_CHECK_ASN, m_checkAsn);
    DDX_Control(pDX, IDC_CHECK_PUBLIC_INFO, m_checkPublicInfo);
}

BOOL WinMTROptions::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_codeFont.CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, WINMTR_CODE_FONT_NAME);
    m_editInterval.SetFont(&m_codeFont);
    m_editSize.SetFont(&m_codeFont);
    m_editMaxLRU.SetFont(&m_codeFont);
    m_editMaxHops.SetFont(&m_codeFont);
    m_editTimeout.SetFont(&m_codeFont);
    m_editCycles.SetFont(&m_codeFont);
    m_editTos.SetFont(&m_codeFont);
    m_editPattern.SetFont(&m_codeFont);
    CString text;
    text.Format("%.1f", interval);
    m_editInterval.SetWindowText(text);
    text.Format("%d", pingSize);
    m_editSize.SetWindowText(text);
    text.Format("%d", maxLRU);
    m_editMaxLRU.SetWindowText(text);
    text.Format("%d", maxHops);
    m_editMaxHops.SetWindowText(text);
    text.Format("%d", timeoutMs);
    m_editTimeout.SetWindowText(text);
    text.Format("%d", cycles);
    m_editCycles.SetWindowText(text);
    text.Format("%d", tos);
    m_editTos.SetWindowText(text);
    text.Format("%d", bitPattern);
    m_editPattern.SetWindowText(text);
    m_checkDNS.SetCheck(useDNS);
    m_checkDF.SetCheck(dontFragment);
    m_checkAsn.SetCheck(lookupAsn);
    m_checkPublicInfo.SetCheck(lookupPublicInfo);
    m_editInterval.SetFocus();
    return FALSE;
}

void WinMTROptions::OnOK()
{
    CString text;
    m_editInterval.GetWindowText(text);
    const double newInterval = atof(text);
    m_editSize.GetWindowText(text);
    const int newPingSize = atoi(text);
    m_editMaxLRU.GetWindowText(text);
    const int newMaxLRU = atoi(text);
    m_editMaxHops.GetWindowText(text);
    const int newMaxHops = atoi(text);
    m_editTimeout.GetWindowText(text);
    const int newTimeout = atoi(text);
    m_editCycles.GetWindowText(text);
    const int newCycles = atoi(text);
    m_editTos.GetWindowText(text);
    const int newTos = atoi(text);
    m_editPattern.GetWindowText(text);
    const int newPattern = atoi(text);

    if (newInterval < 0.1 || newInterval > 60.0 ||
        newPingSize < 0 || newPingSize > MAXPACKET ||
        newMaxLRU < 1 || newMaxLRU > MaxHost ||
        newMaxHops < 1 || newMaxHops > 64 ||
        newTimeout < 100 || newTimeout > 10000 ||
        newCycles < 0 || newCycles > 100000 ||
        newTos < 0 || newTos > 255 ||
        newPattern < -1 || newPattern > 255) {
        CString error;
        error.LoadString(IDS_ERROR_INVALID_OPTIONS);
        AfxMessageBox(error, MB_ICONWARNING);
        return;
    }

    interval = newInterval;
    pingSize = newPingSize;
    maxLRU = newMaxLRU;
    maxHops = newMaxHops;
    timeoutMs = newTimeout;
    cycles = newCycles;
    tos = newTos;
    bitPattern = newPattern;
    useDNS = m_checkDNS.GetCheck();
    dontFragment = m_checkDF.GetCheck();
    lookupAsn = m_checkAsn.GetCheck();
    lookupPublicInfo = m_checkPublicInfo.GetCheck();
    CDialog::OnOK();
}

void WinMTROptions::OnLicense()
{
    WinMTRLicense dialog;
    dialog.DoModal();
}
