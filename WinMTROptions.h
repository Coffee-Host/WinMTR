#ifndef WINMTROPTIONS_H_
#define WINMTROPTIONS_H_

class WinMTROptions : public CDialog
{
public:
    WinMTROptions(CWnd* pParent = NULL);

    void SetUseDNS(BOOL value) { useDNS = value; }
    void SetInterval(double value) { interval = value; }
    void SetPingSize(int value) { pingSize = value; }
    void SetMaxLRU(int value) { maxLRU = value; }
    void SetMaxHops(int value) { maxHops = value; }
    void SetTimeout(int value) { timeoutMs = value; }
    void SetCycles(int value) { cycles = value; }
    void SetTos(int value) { tos = value; }
    void SetBitPattern(int value) { bitPattern = value; }
    void SetDontFragment(BOOL value) { dontFragment = value; }
    void SetLookupAsn(BOOL value) { lookupAsn = value; }
    void SetLookupPublicInfo(BOOL value) { lookupPublicInfo = value; }
    void SetUseIPv4(BOOL value) { useIPv4 = value; }
    void SetUseIPv6(BOOL value) { useIPv6 = value; }
    void SetFirstTtl(int value) { firstTtl = value; }
    void SetDueTtl(int value) { dueTtl = value; }
    void SetMaxUnknown(int value) { maxUnknown = value; }
    void SetMaxDisplayPaths(int value) { maxDisplayPaths = value; }
    void SetCacheSeconds(int value) { cacheSeconds = value; }

    BOOL GetUseDNS() const { return useDNS; }
    double GetInterval() const { return interval; }
    int GetPingSize() const { return pingSize; }
    int GetMaxLRU() const { return maxLRU; }
    int GetMaxHops() const { return maxHops; }
    int GetTimeout() const { return timeoutMs; }
    int GetCycles() const { return cycles; }
    int GetTos() const { return tos; }
    int GetBitPattern() const { return bitPattern; }
    BOOL GetDontFragment() const { return dontFragment; }
    BOOL GetLookupAsn() const { return lookupAsn; }
    BOOL GetLookupPublicInfo() const { return lookupPublicInfo; }
    BOOL GetUseIPv4() const { return useIPv4; }
    BOOL GetUseIPv6() const { return useIPv6; }
    int GetFirstTtl() const { return firstTtl; }
    int GetDueTtl() const { return dueTtl; }
    int GetMaxUnknown() const { return maxUnknown; }
    int GetMaxDisplayPaths() const { return maxDisplayPaths; }
    int GetCacheSeconds() const { return cacheSeconds; }

    enum { IDD = IDD_DIALOG_OPTIONS };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    afx_msg void OnLicense();
    afx_msg void OnRestoreDefaults();
    DECLARE_MESSAGE_MAP()

private:
    void PopulateControls();
    void ResetValuesToDefaults();

    CEdit m_editSize;
    CEdit m_editInterval;
    CEdit m_editMaxLRU;
    CEdit m_editMaxHops;
    CEdit m_editTimeout;
    CEdit m_editCycles;
    CEdit m_editTos;
    CEdit m_editPattern;
    CEdit m_editFirstTtl;
    CEdit m_editDueTtl;
    CEdit m_editMaxUnknown;
    CEdit m_editMaxDisplayPaths;
    CEdit m_editCacheSeconds;
    CButton m_checkDNS;
    CButton m_checkDF;
    CButton m_checkAsn;
    CButton m_checkPublicInfo;
    CButton m_checkIPv4;
    CButton m_checkIPv6;
    CFont m_codeFont;

    double interval;
    int pingSize;
    int maxLRU;
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
    int firstTtl;
    int dueTtl;
    int maxUnknown;
    int maxDisplayPaths;
    int cacheSeconds;
};

#endif // WINMTROPTIONS_H_
