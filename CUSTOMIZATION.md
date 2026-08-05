# WinMTR 自訂指南

這份文件列出所有會影響使用者可見文字、品牌、網路資料來源與輸出格式的位置。修改 UTF-8 檔案時請保留原編碼；`WinMTR.rc` 使用 `#pragma code_page(65001)`。

## 介面與繁中文字串

所有一般介面文字集中在 [`WinMTR.rc`](WinMTR.rc)：

| 區段 | 可修改內容 |
| --- | --- |
| `IDD_WINMTR_DIALOG` | 主視窗按鈕、主機標籤與版面 |
| `IDD_DIALOG_OPTIONS` | 選項視窗的欄位名稱、核取方塊與按鈕 |
| `IDD_DIALOG_LICENSE` | 授權視窗標題、說明與網址 |
| `IDD_DIALOG_PROPERTIES` | 節點詳細資料視窗 |
| `IDD_DIALOG_HELP` | 命令列說明 |
| `IDD_DIALOG_NETWORK_INFO` | 目前網路資訊視窗 |
| `VS_VERSION_INFO` | Windows 檔案內容中的產品名稱、版本、描述與著作權 |
| `STRINGTABLE` | 狀態列、錯誤訊息、快顯選單、欄位名稱、匯出篩選器、視窗標題與網路資訊欄位 |

新增或刪除 `STRINGTABLE` 項目時，識別碼定義位於 [`resource.h`](resource.h)。若只要改文字，不必修改識別碼。

台灣繁體中文用詞目前統一採用「資訊、網路、設定、伺服器、位元組、逾時、丟包率、檔案、程式」等形式。請避免把字串改成簡體中文或中國大陸慣用詞。

## 品牌、字型與外部服務

集中設定檔是 [`WinMTRCustomization.h`](WinMTRCustomization.h)：

| 定義 | 用途 |
| --- | --- |
| `WINMTR_PRODUCT_NAME` | 產品名稱、HTTP User-Agent 與 HTML 報告標題 |
| `WINMTR_VERSION` | 內部版本與 HTTP User-Agent；Windows 資源中的版本仍須同步修改 |
| `WINMTR_HOMEPAGE` | 寫入使用者登錄的官方網站 |
| `WINMTR_COMPANY_URL` | 主視窗底部公司名稱連結的網址 |
| `WINMTR_CODE_FONT_NAME` | 主機、IP、數字、ASN、ISP 與程式資料字型 |
| `WINMTR_UI_FONT_NAME` | UI 字型名稱；資源對話方塊字型也須在 `WinMTR.rc` 同步修改 |
| `WINMTR_ENABLE_PUBLIC_IP_LOOKUP_DEFAULT` | `1` 為預設查詢公網資訊，`0` 為預設關閉 |
| `WINMTR_IPINFO_IPV4_HOST` | 公網 IPv4 與 IPv4 逐跳資訊端點，目前為 `ipinfo.io` |
| `WINMTR_IPINFO_IPV6_HOST` | 公網 IPv6 與 IPv6 逐跳資訊端點，目前為 `v6.ipinfo.io` |
| `WINMTR_IPINFO_TOKEN` | 選用的 ipinfo 存取權杖；空字串使用無權杖端點 |
| `WINMTR_DNS_DIAGNOSTIC_NAME` | Akamai 遞迴 DNS 與 ECS 診斷 TXT 記錄，目前為 `whoami.ds.akahelp.net` |
| `WINMTR_FALLBACK_IPV4_HOST`、`WINMTR_FALLBACK_IPV6_HOST` | ipinfo 失敗時使用的 ipify 公網 IP 端點 |
| `WINMTR_FALLBACK_IP_DETAILS_HOST` | ipinfo 失敗時使用的 ipapi 地區與網路資訊端點 |
| `WINMTR_FALLBACK_ASN_*` | ipinfo／ipapi 資料不足時使用的 Team Cymru ASN TXT 端點 |

ipinfo 端點必須回傳含 `ip`、`hostname`、`city`、`region`、`country`、`org` 的 JSON。Akamai TXT 記錄必須維持成對的 `ns`／`ecs` 欄位；`ecs` 可能省略。備援 ipapi 端點須回傳 `city`、`region`、`country_code`、`country_name`、`asn`、`org` JSON 欄位。

主視窗摘要的 IP／Hostname、國家／城市、ASN／ISP 格式位於 `WinMTR.rc` 的 `IDS_STATUS_PUBLIC_IP_*` 與 `IDS_NETWORK_SUMMARY_*`。網路資訊視窗的「資料來源」會由程式依該次實際成功的端點動態組成，分隔字串為 `IDS_NETWORK_INFO_SOURCE_SEPARATOR`；端點名稱本身來自 `WinMTRCustomization.h`。

## 預設值與表格

[`WinMTRGlobal.h`](WinMTRGlobal.h) 包含探測預設值、允許範圍相關常數、表格欄數與每欄寬度：

- `DEFAULT_*`：封包大小、間隔、最大跳數、逾時、循環次數、TOS、資料樣式、DF、ASN 查詢與 IPv4／IPv6 位址家族。
- `RECOMMENDED_SHARE_PACKETS`：截圖、複製或匯出前建議第一跳至少送出的封包數。
- `MTR_COL_RESOURCE_IDS`：表格欄位及順序；欄位文字仍在 `WinMTR.rc`。
- `MTR_COL_LENGTH`：各欄建立時的備援寬度；顯示後會依標題及內容自動調整。
- `MAXPACKET`、`MaxHost`：選項允許上限。

變更允許範圍時，也要同步檢查 [`WinMTROptions.cpp`](WinMTROptions.cpp) 的 `OnOK()` 驗證，以及 [`WinMTRDialog.cpp`](WinMTRDialog.cpp) 的登錄值範圍防護。

## 匯出格式與其他字串

[`WinMTRDialog.cpp`](WinMTRDialog.cpp) 包含以下輸出格式：

- `BuildTextReport()`：純文字報告。
- `BuildHtmlReport()`：HTML 標記、語系與 CSS。
- `BuildCsvReport()`：CSV 格式。
- `BuildJsonReport()`：JSON 欄位名稱，例如 `target`、`hops`、`loss_percent` 與 `average_ms`。

報告中的人類可讀欄位名稱會讀取 `WinMTR.rc` 的 `STRINGTABLE`。JSON 欄位名稱屬於資料介面，若要修改，請留意既有使用者的相容性。

其他可修改位置：

- [`WinMTRGlobal.h`](WinMTRGlobal.h)：`WINMTR_LICENSE` 與 `WINMTR_COPYRIGHT`。
- [`WinMTRLicense.cpp`](WinMTRLicense.cpp)：完整 GPL v2 授權文字。
- [`WinMTRDialog.cpp`](WinMTRDialog.cpp)：登錄路徑 `Software\\WinMTR` 與各設定鍵名稱。
- [`WinMTRDialog.cpp`](WinMTRDialog.cpp)：欄寬上下限、自動顯示列數、HTML 報告樣式，以及截圖／選單行為。
- [`WinMTRNetworkInfoDialog.cpp`](WinMTRNetworkInfoDialog.cpp)：網路資訊的欄位排列、空白行與自動尺寸留白。
- [`WinMTR.vcxproj`](WinMTR.vcxproj)：輸出資料夾、執行檔名稱、Windows SDK 與工具組版本。

每次修改資源、品牌或欄位後，請至少重建 `Release|x64`，並檢查主視窗、選項、網路資訊、截圖剪貼簿與四種匯出格式。支援目標是 Windows 7 至 Windows 11 的 64 位元版本。
