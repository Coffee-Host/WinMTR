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
| `STRINGTABLE` | 狀態列、錯誤訊息、欄位名稱、匯出篩選器、視窗標題與網路資訊欄位 |

新增或刪除 `STRINGTABLE` 項目時，識別碼定義位於 [`resource.h`](resource.h)。若只要改文字，不必修改識別碼。

台灣繁體中文用詞目前統一採用「資訊、網路、設定、伺服器、位元組、逾時、遺失率、檔案、程式」等形式。請避免把字串改成簡體中文或中國大陸慣用詞。

## 品牌、字型與外部服務

集中設定檔是 [`WinMTRCustomization.h`](WinMTRCustomization.h)：

| 定義 | 用途 |
| --- | --- |
| `WINMTR_PRODUCT_NAME` | 產品名稱、HTTP User-Agent 與 HTML 報告標題 |
| `WINMTR_VERSION` | 內部版本與 HTTP User-Agent；Windows 資源中的版本仍須同步修改 |
| `WINMTR_HOMEPAGE` | 寫入使用者登錄的官方網站 |
| `WINMTR_CODE_FONT_NAME` | 主機、IP、數字、ASN、ISP 與程式資料字型 |
| `WINMTR_UI_FONT_NAME` | UI 字型名稱；資源對話方塊字型也須在 `WinMTR.rc` 同步修改 |
| `WINMTR_ENABLE_PUBLIC_IP_LOOKUP_DEFAULT` | `1` 為預設查詢公網資訊，`0` 為預設關閉 |
| `WINMTR_PUBLIC_IPV4_*`、`WINMTR_PUBLIC_IPV6_*` | 公網 IPv4／IPv6 查詢端點 |
| `WINMTR_IP_DETAILS_HOST` | IP 地理位置與網路業者查詢端點 |
| `WINMTR_DNS_DIAGNOSTIC_NAME` | 遞迴 DNS 與 ECS 診斷 TXT 記錄 |
| `WINMTR_ASN_*_ZONE` | Team Cymru ASN DNS 查詢網域 |

若更換外部服務，回傳格式也必須相容：公網 IP 端點回傳純文字 IP、IP 詳細資料端點回傳目前使用的 CSV 欄位、DNS 診斷端點回傳成對的 `ns`／`ecs` TXT 字串。

## 預設值與表格

[`WinMTRGlobal.h`](WinMTRGlobal.h) 包含探測預設值、允許範圍相關常數、表格欄數與每欄寬度：

- `DEFAULT_*`：封包大小、間隔、最大跳數、逾時、循環次數、TOS、資料樣式、DF 與 ASN 查詢。
- `MTR_COL_RESOURCE_IDS`：表格欄位及順序；欄位文字仍在 `WinMTR.rc`。
- `MTR_COL_LENGTH`：各欄初始寬度。
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
- [`WinMTR.vcxproj`](WinMTR.vcxproj)：輸出資料夾、執行檔名稱、Windows SDK 與工具組版本。

每次修改資源、品牌或欄位後，請至少重建 `Release|Win32` 與 `Release|x64`，並檢查主視窗、選項、網路資訊與四種匯出格式。
