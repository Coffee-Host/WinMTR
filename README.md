# WinMTR

WinMTR 是適用於 Windows 7 至 Windows 11 64 位元版本的路由與網路延遲診斷工具，結合持續 ping 與 traceroute，並以台灣繁體中文呈現。程式使用精簡的原生 Windows／MFC 實作，可建置為單一執行檔，不需要安裝額外執行階段。

## 功能

- IPv4 與 IPv6 ICMP 路由追蹤。
- 每一跳的丟包率、已送／已收封包、最佳／平均／最差／最近延遲、抖動與標準差。
- 主機名稱反向解析，以及每一跳的國家／地區代碼、ASN 與 ISP。
- 每一跳保存最新回應位址與最多 128 條 ECMP 路徑；畫面顯示上限可調整，匯出會保留全部路徑。
- 可調整探測間隔、封包大小、起始／至少／最大 TTL、未知主機上限、回覆快取、逾時、循環次數、TOS、資料樣式與禁止分段（DF）。
- 可指定允許使用 IPv4、IPv6 或兩者，並可一鍵將所有追蹤選項恢復為預設值。
- 顯示目前公網 IPv4／IPv6、主機名稱、城市、地區、國家、ASN 與網路業者。
- 顯示本機 DNS 伺服器、遞迴 DNS 的 ASN／業者／地區，以及 EDNS 用戶端子網路（ECS）狀態。
- 表格依實際資料調整欄寬，主視窗與網路資訊視窗在螢幕工作區內依內容調整大小；螢幕可容納完整資料時不顯示捲軸。
- 截取完整視窗並複製至剪貼簿；低頻使用的複製與匯出操作收納於單一選單。
- 複製文字／HTML，以及匯出 UTF-8 文字、HTML、CSV、JSON。
- 主視窗第一排集中主機輸入與操作按鈕；第二區以雙行欄位顯示 IP／Hostname、國家／城市、ASN／ISP，並由「詳細資料」進入完整網路資訊。
- 連續無回應的節點會折疊為單列並標示跳數範圍。
- 主機歷程、統計重設、命令列啟動與 64 位元版本。
- 主機、IP、英文、數字與程式資料使用 Consolas；一般介面使用 Microsoft JhengHei UI。

## 與 mtr 的範圍差異

本版本以 [traviscross/mtr](https://github.com/traviscross/mtr) 的常用統計與報告能力為方向，但底層使用 Windows ICMP API。現階段支援 ICMP、IPv4／IPv6、DNS、ASN、ECMP 路徑、封包參數與多種報告；尚未實作 TCP／UDP／SCTP 探測、MPLS 標籤與 curses 介面。這些功能需要不同的封包引擎、管理員權限或顯著增加程式體積。

## 使用方式

1. 執行 `WinMTR.exe`。
2. 輸入主機名稱或 IP 位址。
3. 視需要在「選項」調整探測參數、DNS、ASN 與公網資訊查詢。
4. 按「開始」，再以表格或匯出報告檢視結果。
5. 按兩下任一節點可查看該跳詳細資料。

命令列範例：

```text
WinMTR.exe github.com
WinMTR.exe --numeric 1.1.1.1
WinMTR.exe --interval 0.5 --size 64 example.com
WinMTR.exe --maxLRU 64
WinMTR.exe --help
```

## 隱私與資料來源

「啟動時查詢目前公網 IP、地區與業者」預設開啟，可在選項中關閉。公網 IP、逐跳 ASN／ISP 優先使用 `ipinfo.io` 與 `v6.ipinfo.io`；遞迴 DNS／ECS 診斷只使用 Akamai 的 `whoami.ds.akahelp.net` TXT 記錄。ipinfo 無法使用時，公網 IP 依序由 ipify 與 ipapi 補齊，逐跳 ASN／ISP 則由 ipapi 與 Team Cymru 補齊。詳細資料視窗只列出該次查詢實際使用的服務。這些服務會看到必要的查詢來源資訊，且其可用性、快取與使用限制由各服務提供者決定。

所有端點、預設開關、品牌與字型都可在 [`WinMTRCustomization.h`](WinMTRCustomization.h) 修改。完整字串與自訂位置索引請見 [`CUSTOMIZATION.md`](CUSTOMIZATION.md)。

## 建置

需求：Visual Studio 2022 Build Tools、MSVC v143、Windows 10 SDK，以及 MFC C++ 元件。

```powershell
msbuild WinMTR.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

輸出位置：

- `Release/WinMTR.exe`

## 授權

本專案依 GNU General Public License v2 授權。WinMTR 最初由 Vasile Laurentiu Stanimir 開發，後續由 Dragos Manac、Appnor MSP 與社群維護。
