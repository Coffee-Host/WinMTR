# WinMTR

WinMTR 是適用於 Windows 10／11 的路由與網路延遲診斷工具，結合持續 ping 與 traceroute，並以台灣繁體中文呈現。程式使用原生 Win32／MFC，可建置為單一執行檔，不需要安裝額外執行階段。

## 功能

- IPv4 與 IPv6 ICMP 路由追蹤。
- 每一跳的遺失率、已送／已收封包、最佳／平均／最差／最近延遲、抖動與標準差。
- 主機名稱反向解析，以及每一跳的國家／地區代碼、ASN 與 ISP。
- 可調整探測間隔、封包大小、最大跳數、逾時、循環次數、TOS、資料樣式與禁止分段（DF）。
- 顯示目前公網 IPv4／IPv6、主機名稱、城市、地區、國家、ASN 與網路業者。
- 顯示本機 DNS 伺服器、遞迴 DNS 的 ASN／業者／地區，以及 EDNS 用戶端子網路（ECS）狀態。
- 複製文字／HTML，以及匯出 UTF-8 文字、HTML、CSV、JSON。
- 主機歷程、統計重設、命令列啟動與 32／64 位元版本。
- 主機、IP、英文、數字與程式資料使用 Consolas；一般介面使用 Microsoft JhengHei UI。

## 與 mtr 的範圍差異

本版本以 [traviscross/mtr](https://github.com/traviscross/mtr) 的常用統計與報告能力為方向，但底層使用 Windows ICMP API。現階段支援 ICMP、IPv4／IPv6、DNS、ASN、封包參數與多種報告；尚未實作 TCP／UDP／SCTP 探測、MPLS 標籤、ECMP 路徑探索與 curses 介面。這些功能需要不同的封包引擎、管理員權限或顯著增加程式體積。

## 使用方式

1. 執行對應平台的 `WinMTR.exe`。
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

「啟動時查詢目前公網 IP、地區與業者」預設開啟，可在選項中關閉。啟用時會連線至 ipify 與 ipapi；ASN／ISP 功能會透過 DNS 查詢 Team Cymru，遞迴 DNS／ECS 診斷會查詢 Akamai。這些服務會看到必要的查詢來源資訊，且其可用性與使用限制由各服務提供者決定。

所有端點、預設開關、品牌與字型都可在 [`WinMTRCustomization.h`](WinMTRCustomization.h) 修改。完整字串與自訂位置索引請見 [`CUSTOMIZATION.md`](CUSTOMIZATION.md)。

## 建置

需求：Visual Studio 2022 Build Tools、MSVC v143、Windows 10 SDK，以及 MFC C++ 元件。

```powershell
msbuild WinMTR.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
msbuild WinMTR.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

輸出位置：

- `Release_x64/WinMTR.exe`
- `Release_x32/WinMTR.exe`

## 授權

本專案依 GNU General Public License v2 授權。WinMTR 最初由 Vasile Laurentiu Stanimir 開發，後續由 Dragos Manac、Appnor MSP 與社群維護。
