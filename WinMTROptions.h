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

    enum { IDD = IDD_DIALOG_OPTIONS };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    afx_msg void OnLicense();
    DECLARE_MESSAGE_MAP()

private:
    CEdit m_editSize;
    CEdit m_editInterval;
    CEdit m_editMaxLRU;
    CEdit m_editMaxHops;
    CEdit m_editTimeout;
    CEdit m_editCycles;
    CEdit m_editTos;
    CEdit m_editPattern;
    CButton m_checkDNS;
    CButton m_checkDF;
    CButton m_checkAsn;
    CButton m_checkPublicInfo;
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
};

#endif // WINMTROPTIONS_H_
