#ifndef WINMTRNETWORKINFODIALOG_H_
#define WINMTRNETWORKINFODIALOG_H_

#include "WinMTRNetworkInfo.h"

class WinMTRNetworkInfoDialog : public CDialog
{
public:
    WinMTRNetworkInfoDialog(const PublicNetworkInfo& info, CWnd* parent = NULL);
    enum { IDD = IDD_DIALOG_NETWORK_INFO };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    afx_msg void OnCopy();
    DECLARE_MESSAGE_MAP()

private:
    void AdjustToContent();
    CString BuildText() const;
    CString ToDisplayText(const std::string& utf8) const;
    void AppendField(CString& output, UINT labelId, const std::string& value) const;
    void AppendDetails(CString& output, UINT titleId, const IpNetworkDetails& details) const;

    PublicNetworkInfo networkInfo;
    CEdit m_information;
    CFont m_codeFont;
};

#endif // WINMTRNETWORKINFODIALOG_H_
