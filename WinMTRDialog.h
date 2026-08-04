#ifndef WINMTRDIALOG_H_
#define WINMTRDIALOG_H_

#define WINMTR_DIALOG_TIMER 100
#define WM_PUBLIC_NETWORK_INFO (WM_APP + 101)

#include <afxlinkctrl.h>

#include "WinMTRStatusBar.h"
#include "WinMTRNet.h"
#include "WinMTRNetworkInfo.h"

#include <string>
#include <utility>
#include <vector>

class WinMTRDialog : public CDialog
{
public:
    WinMTRDialog(CWnd* pParent = NULL);
    ~WinMTRDialog();

    enum { IDD = IDD_WINMTR_DIALOG };
    enum STATES { IDLE, TRACING, STOPPING, EXIT };
    enum STATE_TRANSITIONS {
        IDLE_TO_IDLE,
        IDLE_TO_TRACING,
        IDLE_TO_EXIT,
        TRACING_TO_TRACING,
        TRACING_TO_STOPPING,
        TRACING_TO_EXIT,
        STOPPING_TO_IDLE,
        STOPPING_TO_STOPPING,
        STOPPING_TO_EXIT
    };

    void SetHostName(const char* host);
    void SetInterval(float value);
    void SetPingSize(int value);
    void SetMaxLRU(int value);
    void SetUseDNS(BOOL value);

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnCancel();

    afx_msg void OnPaint();
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnSizing(UINT side, LPRECT rectangle);
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnRestart();
    afx_msg void OnOptions();
    afx_msg void OnCTTC();
    afx_msg void OnCHTC();
    afx_msg void OnEXPT();
    afx_msg void OnEXPH();
    afx_msg void OnEXPC();
    afx_msg void OnEXPJ();
    afx_msg void OnCaptureScreenshot();
    afx_msg void OnReportMenu();
    afx_msg void OnResetStats();
    afx_msg void OnNetworkInfo();
    afx_msg void OnDblclkList(NMHDR* header, LRESULT* result);
    afx_msg void OnCbnSelchangeComboHost();
    afx_msg void OnCbnSelendokComboHost();
    afx_msg void OnCbnCloseupComboHost();
    afx_msg void OnTimer(UINT_PTR eventId);
    afx_msg void OnClose();
    afx_msg LRESULT OnPublicNetworkInfoReady(WPARAM, LPARAM value);
    DECLARE_MESSAGE_MAP()

private:
    BOOL InitRegistry();
    int ResolveTraceTarget();
    int DisplayRedraw();
    void Transit(STATES newState);
    void ClearHistory();
    void StartPublicInfoLookup();
    void SaveConfiguration();
    void AdjustColumnWidths();
    void AdjustWindowToContent();
    void StretchLastColumnToFill();
    void SetHostControlTracing(bool tracing);
    void SetTraceStatus(const CString& text);
    void SetPublicInfoPlaceholders(UINT ipTextId);
    void UpdatePublicInfoSummary(const IpNetworkDetails& details);
    TraceConfig CurrentTraceConfig() const;
    CString LoadText(UINT id) const;
    CString Utf8ToLocal(const std::string& value) const;
    void CopyTextToClipboard(const std::string& text) const;
    void SaveReport(const std::string& text, LPCTSTR extension, LPCTSTR filter) const;
    std::string BuildTextReport() const;
    std::string BuildHtmlReport() const;
    std::string BuildCsvReport() const;
    std::string BuildJsonReport() const;

    CButton m_buttonOptions;
    CButton m_buttonStart;
    CButton m_buttonReset;
    CButton m_buttonCapture;
    CButton m_buttonReportMenu;
    CButton m_buttonNetworkDetails;
    CStatic m_publicIpSummary;
    CStatic m_publicHostnameSummary;
    CStatic m_publicCountrySummary;
    CStatic m_publicCitySummary;
    CStatic m_publicAsnSummary;
    CStatic m_publicIspSummary;
    CComboBox m_comboHost;
    CEdit m_traceHostDisplay;
    CListCtrl m_listMTR;
    CStatic m_staticS;
    CStatic m_staticActions;
    CStatic m_staticJ;
    CFont m_codeFont;
    CFont m_tableFont;
    WinMTRStatusBar footerStatus;
    CMFCLinkCtrl companyLink;

    STATES state;
    STATE_TRANSITIONS transition;
    HANDLE traceThread;
    HANDLE publicInfoThread;
    sockaddr_storage traceTarget;
    double interval;
    int pingSize;
    int maxLRU;
    int nrLRU;
    int maxHops;
    int timeoutMs;
    int cycles;
    int tos;
    int bitPattern;
    BOOL useDNS;
    BOOL dontFragment;
    BOOL lookupAsn;
    BOOL lookupPublicInfo;
    BOOL useIPv4;
    BOOL useIPv6;
    bool hasIntervalFromCmdLine;
    bool hasPingSizeFromCmdLine;
    bool hasMaxLRUFromCmdLine;
    bool hasUseDNSFromCmdLine;
    bool publicInfoQueryStarted;
    bool adjustingWindow;
    CSize minimumWindowSize;
    int m_autostart;
    char defaultHostName[1000];
    HICON m_hIcon;
    WinMTRNet* network;
    PublicNetworkInfo* publicNetworkInfo;
    std::vector<std::pair<int, int> > displayedHopRanges;
};

#endif // WINMTRDIALOG_H_
