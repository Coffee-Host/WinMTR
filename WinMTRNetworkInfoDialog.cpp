#include "WinMTRGlobal.h"
#include "WinMTRCustomization.h"
#include "WinMTRNetworkInfoDialog.h"

#include <algorithm>
#include <vector>

BEGIN_MESSAGE_MAP(WinMTRNetworkInfoDialog, CDialog)
    ON_BN_CLICKED(ID_COPY_NETWORK_INFO, OnCopy)
END_MESSAGE_MAP()

WinMTRNetworkInfoDialog::WinMTRNetworkInfoDialog(const PublicNetworkInfo& info,
    CWnd* parent)
    : CDialog(WinMTRNetworkInfoDialog::IDD, parent), networkInfo(info)
{
}

void WinMTRNetworkInfoDialog::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_NETWORK_INFO, m_information);
}

BOOL WinMTRNetworkInfoDialog::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_codeFont.CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, WINMTR_CODE_FONT_NAME);
    m_information.SetFont(&m_codeFont);
    m_information.SetWindowText(BuildText());
    AdjustToContent();
    CWnd* closeButton = GetDlgItem(IDOK);
    if (closeButton) {
        closeButton->SetFocus();
        return FALSE;
    }
    return TRUE;
}

void WinMTRNetworkInfoDialog::AdjustToContent()
{
    const CString text = BuildText();
    CClientDC dc(this);
    CFont* previousFont = dc.SelectObject(&m_codeFont);
    int widestLine = 0;
    int lineCount = 0;
    CString line;
    for (int index = 0; AfxExtractSubString(line, text, index, '\n'); ++index) {
        line.TrimRight("\r");
        widestLine = std::max(widestLine,
            static_cast<int>(dc.GetTextExtent(line).cx));
        ++lineCount;
    }
    TEXTMETRIC metrics = {};
    dc.GetTextMetrics(&metrics);
    dc.SelectObject(previousFont);

    CRect originalClient;
    CRect originalWindow;
    CRect edit;
    GetClientRect(originalClient);
    GetWindowRect(originalWindow);
    m_information.GetWindowRect(edit);
    ScreenToClient(edit);
    const int desiredEditWidth = std::max(static_cast<int>(edit.Width()),
        widestLine + 30);
    const int desiredEditHeight = std::max(static_cast<int>(edit.Height()),
        lineCount * static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading) + 16);
    int desiredWidth = originalWindow.Width() + desiredEditWidth - edit.Width();
    int desiredHeight = originalWindow.Height() + desiredEditHeight - edit.Height();

    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfo(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST),
        &monitor)) {
        return;
    }
    const int workWidth = static_cast<int>(monitor.rcWork.right - monitor.rcWork.left);
    const int workHeight = static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top);
    desiredWidth = std::min(desiredWidth, workWidth);
    desiredHeight = std::min(desiredHeight, workHeight);
    const int left = std::max(static_cast<int>(monitor.rcWork.left),
        std::min(static_cast<int>(originalWindow.left),
            static_cast<int>(monitor.rcWork.right) - desiredWidth));
    const int top = std::max(static_cast<int>(monitor.rcWork.top),
        std::min(static_cast<int>(originalWindow.top),
            static_cast<int>(monitor.rcWork.bottom) - desiredHeight));
    SetWindowPos(NULL, left, top, desiredWidth, desiredHeight,
        SWP_NOACTIVATE | SWP_NOZORDER);

    CRect client;
    GetClientRect(client);
    const int rightMargin = originalClient.right - edit.right;
    const int controlsArea = originalClient.bottom - edit.bottom;
    m_information.SetWindowPos(NULL, 0, 0,
        client.Width() - edit.left - rightMargin,
        client.Height() - edit.top - controlsArea,
        SWP_NOMOVE | SWP_NOZORDER);

    CWnd* copyButton = GetDlgItem(ID_COPY_NETWORK_INFO);
    CWnd* closeButton = GetDlgItem(IDOK);
    if (copyButton && closeButton) {
        CRect copy;
        CRect close;
        copyButton->GetWindowRect(copy);
        closeButton->GetWindowRect(close);
        ScreenToClient(copy);
        ScreenToClient(close);
        const int gap = close.left - copy.right;
        const int groupWidth = copy.Width() + gap + close.Width();
        const int buttonTop = client.bottom - (originalClient.bottom - copy.top);
        const int copyLeft = (client.Width() - groupWidth) / 2;
        copyButton->SetWindowPos(NULL, copyLeft, buttonTop, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER);
        closeButton->SetWindowPos(NULL, copyLeft + copy.Width() + gap,
            buttonTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

CString WinMTRNetworkInfoDialog::ToDisplayText(const std::string& utf8) const
{
    if (utf8.empty()) {
        CString unavailable;
        unavailable.LoadString(IDS_NETWORK_INFO_NOT_AVAILABLE);
        return unavailable;
    }
    const int wideSize = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wideSize <= 0)
        return CString(utf8.c_str());
    std::vector<wchar_t> wide(wideSize);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wideSize);
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

void WinMTRNetworkInfoDialog::AppendField(CString& output, UINT labelId,
    const std::string& value) const
{
    CString label;
    label.LoadString(labelId);
    output.AppendFormat("%-12s %s\r\n", static_cast<LPCTSTR>(label),
        static_cast<LPCTSTR>(ToDisplayText(value)));
}

void WinMTRNetworkInfoDialog::AppendDetails(CString& output, UINT titleId,
    const IpNetworkDetails& details) const
{
    CString title;
    title.LoadString(titleId);
    output += title;
    output += "\r\n";
    if (!details.available) {
        CString unavailable;
        unavailable.LoadString(IDS_NETWORK_INFO_UNAVAILABLE);
        output += unavailable;
        output += "\r\n";
        return;
    }
    AppendField(output, IDS_NETWORK_INFO_ADDRESS, details.address);
    AppendField(output, IDS_NETWORK_INFO_HOSTNAME, details.hostname);
    AppendField(output, IDS_NETWORK_INFO_CITY, details.city);
    AppendField(output, IDS_NETWORK_INFO_REGION, details.region);
    AppendField(output, IDS_NETWORK_INFO_COUNTRY,
        details.country.empty() ? details.countryCode : details.country);
    AppendField(output, IDS_NETWORK_INFO_ASN, details.asn);
    AppendField(output, IDS_NETWORK_INFO_ISP, details.isp);
}

CString WinMTRNetworkInfoDialog::BuildText() const
{
    CString output;
    AppendDetails(output, IDS_NETWORK_INFO_IPV4, networkInfo.ipv4);
    output += "\r\n";
    AppendDetails(output, IDS_NETWORK_INFO_IPV6, networkInfo.ipv6);
    output += "\r\n";
    AppendDetails(output, IDS_NETWORK_INFO_DNS_RESOLVER, networkInfo.dnsResolver);

    CString ecsLabel;
    ecsLabel.LoadString(IDS_NETWORK_INFO_ECS);
    CString ecsValue;
    if (!networkInfo.dnsDiagnosticAvailable) {
        ecsValue.LoadString(IDS_NETWORK_INFO_ECS_UNKNOWN);
    } else if (networkInfo.dnsEcs.empty()) {
        ecsValue.LoadString(IDS_NETWORK_INFO_ECS_NOT_SUPPORTED);
    } else {
        CString format;
        format.LoadString(IDS_NETWORK_INFO_ECS_SUPPORTED_FORMAT);
        ecsValue.Format(format,
            static_cast<LPCTSTR>(ToDisplayText(networkInfo.dnsEcs)));
    }
    output.AppendFormat("%-12s %s\r\n\r\n", static_cast<LPCTSTR>(ecsLabel),
        static_cast<LPCTSTR>(ecsValue));

    CString dnsTitle;
    dnsTitle.LoadString(IDS_NETWORK_INFO_DNS);
    output += dnsTitle;
    output += "\r\n";
    if (networkInfo.dnsServers.empty()) {
        CString unavailable;
        unavailable.LoadString(IDS_NETWORK_INFO_NOT_AVAILABLE);
        output += unavailable;
        output += "\r\n";
    } else {
        for (size_t i = 0; i < networkInfo.dnsServers.size(); ++i) {
            output += "  ";
            output += ToDisplayText(networkInfo.dnsServers[i]);
            output += "\r\n";
        }
    }
    output += "\r\n";
    CString sourceLabel;
    CString sourceValue;
    CString sourceSeparator;
    sourceLabel.LoadString(IDS_NETWORK_INFO_SOURCE);
    sourceSeparator.LoadString(IDS_NETWORK_INFO_SOURCE_SEPARATOR);
    if (networkInfo.usedSources.empty()) {
        sourceValue.LoadString(IDS_NETWORK_INFO_NOT_AVAILABLE);
    } else {
        for (size_t i = 0; i < networkInfo.usedSources.size(); ++i) {
            if (i)
                sourceValue += sourceSeparator;
            sourceValue += ToDisplayText(networkInfo.usedSources[i]);
        }
    }
    output.AppendFormat("%-12s %s\r\n", static_cast<LPCTSTR>(sourceLabel),
        static_cast<LPCTSTR>(sourceValue));
    return output;
}

void WinMTRNetworkInfoDialog::OnCopy()
{
    const CString text = BuildText();
    if (!OpenClipboard())
        return;
    EmptyClipboard();
#ifdef _UNICODE
    const SIZE_T bytes = (text.GetLength() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* destination = GlobalLock(memory);
        if (destination) {
            memcpy(destination, static_cast<LPCWSTR>(text), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
#else
    const SIZE_T bytes = text.GetLength() + 1;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* destination = GlobalLock(memory);
        if (destination) {
            memcpy(destination, static_cast<LPCSTR>(text), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_TEXT, memory))
                GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
#endif
    CloseClipboard();
}
