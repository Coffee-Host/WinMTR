# traviscross/mtr 功能對照與復刻計畫

## 1. 目的與比較基準

本文件定義 WinMTR 復刻 [traviscross/mtr](https://github.com/traviscross/mtr) 的範圍、目前差異、Windows 平台限制與實作順序。這裡的「復刻」以功能與統計語意等價為主，不要求把 curses 或 GTK 的外觀原樣搬到 MFC。

三方比較基準：

- **原始 WinMTR**：本 repo `main` 的第一個提交 `06ece5f2c922f587b1962648d018ac81178ece39`，提交訊息為 `Initial commit: WinMTR v0.92 source`。這是匯入本 repo 的 WinMTR 上游基線。
- **目前 WinMTR**：本專案 `main`，基準提交 `8bd8765`。
- **upstream mtr**：`traviscross/mtr` 提交 `7b017733aef06bb3d8e3573b2e964cc876644fad`，2026-06-16。
- WinMTR 支援目標：Windows 7 至 Windows 11，僅 x64。
- WinMTR 產品限制：維持原生 MFC、單一精簡執行檔；除非使用者明確選擇進階封包模式，否則不要求管理員權限或預先安裝驅動程式。

狀態標記：

| 標記 | 意義 |
| --- | --- |
| `已有` | 功能與主要語意已相同 |
| `近似` | 有相似功能，但行為、統計定義或輸出格式不同 |
| `回歸` | 原始 WinMTR 的行為較合理，目前版本反而退步 |
| `可實作` | 目前純粹尚未實作，Windows 原生 API 可完成 |
| `有條件` | 可完成，但需要管理員權限、封包擷取驅動程式或額外元件 |
| `無直接等價` | Linux／Unix 專屬機制在 Windows 沒有相同介面 |
| `不需照搬` | 上游的介面技術不適用於原生 Windows GUI，應復刻能力而非技術 |

## 2. 結論摘要

目前 WinMTR 已經具備實用的 IPv4／IPv6 ICMP 路由追蹤、常用統計、DNS／ASN／ISP、網路資訊、匯出與截圖功能，但核心探測模型尚未與 mtr 等價。最優先的工作不是增加欄位，而是重寫排程器與封包狀態模型。

三方的定位如下：

| 類別 | 原始 WinMTR `06ece5f` | 目前 WinMTR `8bd8765` | upstream mtr `7b01773` |
| --- | --- | --- | --- |
| 平台／介面 | Windows MFC，IPv4，原本發行 x86／x64 | Windows 7–11 x64 MFC，IPv4／IPv6，compact GUI | Unix／Linux 為主，curses／GTK／report；另有 Cygwin ICMP backend |
| 核心排程 | 每 TTL 一條同步執行緒 | 沿用每 TTL 一條同步執行緒 | 單一 batch event loop，非同步多 probe |
| DNS 隔離 | 每個新位址另開 DNS thread | DNS／ASN／ISP 首次查詢會在該 TTL probe thread 同步執行 | 獨立 resolver process 與 pipe |
| 路徑資料 | IPv4、每跳第一個位址 | IPv4／IPv6、每跳第一個位址 | 最新位址、ECMP、多路徑與最近 400 筆結果 |
| 統計 | Loss、Sent、Recv、Best、Avg、Worst、Last | 原欄位加 Jitter Avg、StDev、ASN／ISP | 可自訂完整 Loss／Drop／RTT／jitter 統計 |
| 分享／輸出 | TXT／HTML 複製及匯出 | TXT／HTML／CSV／JSON、截圖、分享提示 | report／wide／XML／CSV／JSON／raw／split |
| 網路資訊 | 無 | 公網 IP、地理資訊、DNS／ECS、ASN／ISP 與備援來源 | 逐跳 ASN／prefix／country／RIR／allocation date |

原始 WinMTR 到目前版本的主要演進：

- **確實改善**：IPv6、可調最大跳數／timeout／cycles／TOS／pattern／DF、更多統計、ASN／ISP、公網資訊、CSV／JSON、截圖、現代化繁體中文 compact UI、x64 目標及較安全的字串／執行緒 handle 使用。
- **架構未改善**：每 TTL 一條同步探測執行緒、第一個位址鎖定、整數毫秒 RTT、非 mtr 輪次與丟包公式仍然存在。
- **明確回歸**：原始 WinMTR 把反向 DNS 放在獨立 thread；目前首次 DNS／ASN／ISP 查詢在 probe thread 同步執行，會使該跳 Sent 落後。
- **可觀察語意改變**：原始版在同步 `IcmpSendEcho` 返回後才增加 Sent，所以正在等待的 probe 尚未出現在 Sent；目前版在呼叫前增加 Sent，因此等待中的 probe 會立即提高目前的丟包率。兩者都不等同 mtr 的 `transit` 公式。

多數 mtr 功能沒有 Windows 技術障礙，可在不增加外部相依性的情況下完成，包括：

- mtr 式共用輪次排程與平衡的各跳 Sent。
- 非同步 ICMP、mtr 相容的 transit／丟包語意、結束寬限期。
- 路徑變動、ECMP、多回應位址與最近延遲歷程。
- 完整統計、欄位選擇與順序、圖表顯示。
- first TTL、due TTL、max unknown、cache、來源位址／介面。
- report、raw、XML、mtr 相容 CSV／JSON、批次目標與大部分命令列選項。

真正受平台限制的主要是：

- TCP SYN、UDP traceroute 與 MPLS RFC 4950：要可靠接收並解析中間路由器的 ICMP 回覆，通常需要 raw capture、Npcap 或 WinDivert，會增加權限、安裝需求與體積。
- SCTP：Windows 沒有內建 Winsock SCTP，需要第三方協定堆疊，與目前精簡單一執行檔目標衝突。
- Linux `SO_MARK`：Windows 沒有封包 mark 的直接等價介面；只能以來源位址、介面、路由或 WFP 驅動程式近似。
- `mtr-packet` 的 Unix 權限隔離模型：目前 ICMP 使用 Windows IP Helper API，不需管理員權限，因此沒有必要照搬；進階 raw 模式才需要另行設計權限邊界。

## 3. 最重要的核心差異

### 3.1 原始 WinMTR

原始 WinMTR v0.92 的 `WinMTRNet.cpp` 固定建立 30 條執行緒，每條負責一個 TTL，使用同步 `IcmpSendEcho` 與固定 5 秒 timeout。它只支援 IPv4，Sent 在同步呼叫返回後才累加。當每跳第一次取得位址時，另開 `DnsResolverThread` 做反向 DNS，因此 DNS 不會阻塞 probe thread；但它仍只接受第一個回應位址。

原始版沒有 cycles、grace、sequence table、ECMP 或非同步 probe。目的地跳數依「找到目標位址」或尾端重複位址的 heuristic 縮短。timeout 本身會阻塞該 TTL 執行緒；有回覆時才以 `interval - RTT` 睡眠，無回覆則在 5 秒同步 timeout 後立即開始下一次。

### 3.2 目前 WinMTR

目前 `WinMTRNet.cpp` 會為每個 TTL 建立一條執行緒。每條執行緒以同步 `IcmpSendEcho`／`Icmp6SendEcho2` 送出，等待回覆或逾時後才送下一包。首次得到某跳位址時，該執行緒還會同步進行反向 DNS、ASN 與 ISP 查詢。

流程可簡化為：

```text
TTL 1 執行緒：送出 -> 等回覆/逾時 -> DNS/ASN -> 等 interval -> 再送
TTL 2 執行緒：送出 -> 等回覆/逾時 -> DNS/ASN -> 等 interval -> 再送
TTL N 執行緒：各自獨立執行
```

直接後果：

- 各跳不是同一個測量輪次，Sent 可以相差很多。
- 某跳回得快就能較快進入下一次探測；逾時或名稱查詢較慢的跳會落後。
- 找到目的地主機前，較後面的執行緒已經開始送包，因此後面跳數的 Sent 可能大於前面。
- `RecordSent()` 在同步等待前就增加 Sent，目前丟包率直接使用 `Sent - Received`，所以尚未回覆的封包會暫時顯示為丟包。
- 每個 TTL 只保留第一個回應位址，後續路徑變更或 ECMP 回應會被忽略。

相較原始版，目前改用 `_beginthreadex`、stop event 與直接連結 IP Helper，並加入 IPv6 及更多選項；但反向 DNS、ASN、ISP 改由 `ResolveHop()` 在 probe thread 同步執行，形成前述回歸。

### 3.3 upstream mtr

mtr 使用單一事件迴圈及共用 `batch_at`。它按 TTL 1、2、3……依序送出一輪，再回到第一跳。整輪間隔會除以目前路徑跳數，讓各跳均勻分散在輪次時間內。送出後不等待前一包完成，可以同時保留多個 in-flight probe；非同步回覆再經 sequence table 配回原 TTL。

```text
一輪：TTL 1 -> TTL 2 -> ... -> 目的地/上限 -> 下一輪 TTL 1
回覆：sequence -> 原 TTL -> 更新該跳統計
```

直接結果：

- 一般情況下各跳 Sent 最多只差約一包；cache、路徑變更或剛開始／停止時例外。
- interval 表示「同一跳兩輪之間」的時間，不是每個 TTL 各自的同步等待週期。
- sequence table 會追蹤每一筆非同步 probe，但畫面上的丟包公式另以每跳一個 `transit` 旗標處理：分母最多扣掉目前仍視為傳輸中的一包。這不是「精確扣除所有 pending probe」；短 interval、長 timeout 時兩者會有差異。
- 一輪完整排程完才增加 report cycle。
- DNS 在獨立程序處理，不會阻塞探測排程。
- 每跳保存多個回應位址、MPLS 資料與最近 400 次探測結果。

upstream 自己也包含 Windows／Cygwin 的 ICMP backend。`packet/probe_cygwin.c` 明確寫明 Windows backend 只支援 ICMP，並採一條 ICMP service thread 呼叫非同步 `IcmpSendEcho2`／`Icmp6SendEcho2`；該 thread 使用 `WaitForSingleObjectEx(..., TRUE)` 進入 alertable wait 以接收 APC completion。這是目前原生 MFC 重構可直接參考的已驗證設計。

### 3.4 必須先修正的數值語意

| 項目 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 判定與原因 |
| --- | --- | --- | --- | --- |
| Sent | 每 TTL 自行累加；同步呼叫返回後才加 | 每 TTL 自行累加；同步呼叫前先加 | 共用輪次逐 TTL 累加 | 兩版 WinMTR 都不是同一批樣本；目前版還會立即把等待中的 probe 顯示進 Sent |
| Cycles | 無上限，直到手動停止 | 每條 TTL thread 各跑指定次數 | 完成整條路徑的一輪才算一次 | `近似`；目前相同設定值不代表 mtr 的相同測量過程 |
| Interval | 有回覆時補足週期；timeout 時由固定 5 秒主導 | 每跳同步呼叫的最小週期，逾時可使週期更長 | 同一跳兩輪間隔，輪內探測均勻分散 | 三者語意不同 |
| Timeout | 固定 5 秒同步等待 | 可調同步 API 單次等待上限 | 非同步 probe socket 的生命週期 | 目前只改善可設定性，尚未改成 mtr 模型 |
| Loss | `(Sent - Recv) / Sent`；等待中的呼叫尚未列入 Sent | `(Sent - Recv) / Sent`；剛送出即列入 Sent | `(Sent - transit - Recv) / (Sent - transit)`，`transit` 每跳為 0 或 1 | 兩版 WinMTR 都與 mtr 不同；目前執行中更容易暫時高估丟包 |
| DNS 對探測的影響 | 另開 thread，不阻塞 | 首次 DNS／ASN／ISP 同步阻塞該 TTL thread | 獨立 resolver process | 目前相對原始版是 `回歸` |
| 路徑位址 | 每跳只接受第一個 IPv4 | 每跳只接受第一個 IPv4／IPv6 | 最新位址加多路徑歷程 | 目前只有位址族群進步，仍無 ECMP／路徑切換 |
| RTT 精度 | Windows ICMP.DLL 整數毫秒 | Windows IP Helper 整數毫秒 | Unix backend 微秒；Cygwin backend仍是整數毫秒乘 1000 | Windows 原生 API 有精度限制；raw capture 才能接近 Unix backend |

## 4. 核心探測與路徑功能矩陣

| 功能 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 判定與建議 |
| --- | --- | --- | --- | --- |
| IPv4 ICMP | 有 | 有 | 有 | `已有`；Windows IP Helper API 不需管理員權限 |
| IPv6 ICMP | 無 | 有 | 有 | 目前已補上；仍須驗證來源位址與 IPv6 選項 |
| IPv4／IPv6 選擇 | 僅 IPv4 | 兩者皆勾時由 `AF_UNSPEC` 取第一筆 | 一次明確使用 `-4` 或 `-6` | `近似`；目前是自動選一個，不是同時測兩種協定 |
| 共用輪次排程 | 無 | 無 | 有 | `可實作`；以單一 scheduler 依 TTL 送出 |
| 非同步多筆 in-flight probe | 無 | 無 | 有 | `可實作`；上游 Cygwin backend 已示範 APC service thread |
| sequence／probe 生命週期 | 無 | 無 | 有 | `可實作`；建立 Pending、Replied、Expired、Cancelled 與 probe ID |
| mtr transit 丟包語意 | 無 | 無 | 每跳以 0／1 marker 暫時排除最新探測 | `可實作`；相容欄位須沿用 upstream 可觀察公式 |
| 結束寬限期 `--gracetime` | 無 | 無 | 有 | `可實作` |
| 動態路徑長度 | 目標位址或尾端重複位址 heuristic | 找到目的地後縮短 | 依目的地、unknown 與 TTL 上限調整 | `近似`；目前移除原始 heuristic，但仍受 thread 競速影響 |
| First TTL | 固定 1 | 固定 1 | `--first-ttl` | `可實作` |
| Due TTL | 無 | 無 | `--due-ttl` | `可實作`；支援 ECMP／異常路徑診斷 |
| Max TTL | 固定 30 | GUI 可調 1–64 | 可調 | 目前 GUI 已改善；命令列尚未相容 |
| Max unknown | 有未使用常數，沒有行為 | 無 | `--max-unknown` | `可實作`；不能把原始未使用常數算成已有功能 |
| Probe cache | 無 | 無 | `--cache` | `可實作` |
| 路徑變動 | 只保留第一個 IPv4 | 只保留第一個 IPv4／IPv6 | 最新位址與歷程 | `可實作`；目前資料模型仍承襲原版限制 |
| ECMP 多路徑 | 無 | 無 | 每跳最多保存 128 條，顯示上限預設 8 | `可實作`；ICMP 回覆已有來源位址，不需 raw socket |
| 無回應跳合併 | 無 | 有，顯示跳數範圍 | 一般逐跳，另有 compact | 目前 WinMTR 額外功能；底層統計仍須逐跳保存 |
| 最近探測歷程 | 只有未使用的 `SAVED_PINGS 100` 常數 | 無 | 每跳保存最近 400 次 | `可實作`；原始版並未真正配置或更新歷程 |
| MPLS RFC 4950 | 無 | 無 | 有 | `有條件`；通常需 Npcap／WinDivert 或 raw capture |
| 封包大小 | ICMP payload bytes | ICMP payload bytes | 含 IP 與 ICMP header | 兩版 WinMTR 都是 `近似`；相同數字的線上封包大小不同 |
| 隨機封包大小 | 無 | 無，負值不接受 | 負值表示每輪隨機 | `可實作` |
| 固定／隨機資料樣式 | 固定 byte 32 | 可設；`-1` 依 TTL／probe 產生可預測值 | 可設；負值表示隨機 | 目前有進步但仍非完全等價 |
| TOS／DSCP | 固定 0 | 可設定 0–255 | 可設定 | IPv4 可實作等價；IPv6 Traffic Class 需跨版本驗證 |
| Don't Fragment | IPv4 永遠開啟 | 可選 | 無同名主要選項 | WinMTR 額外功能；目前比原版更合理 |
| ICMP 錯誤原因 | 顯示多種 Windows IP status 文字 | 非成功／TTL expired 多半只成為無回應 | 解析並回報部分 network／host 錯誤 | 目前相對原始版是 `回歸`；應恢復結構化錯誤狀態 |

## 5. 傳輸協定與路由控制

| 功能 | 原始 WinMTR | 目前 WinMTR | upstream mtr | Windows 限制與建議 |
| --- | --- | --- | --- | --- |
| ICMP ECHO | IPv4 | IPv4／IPv6 | 預設；Cygwin backend 也是 ICMP only | Phase 1 做到排程與統計語意等價 |
| UDP traceroute | 無 | 無 | Unix backend 有 `--udp`；Cygwin backend 無 | `有條件`；Windows 通常需 raw capture／Npcap／WinDivert |
| TCP SYN traceroute | 無 | 無 | Unix backend 有 `--tcp`；Cygwin backend 無 | `有條件`；需配對中間 ICMP 與 SYN-ACK／RST |
| SCTP traceroute | 無 | 無 | 支援平台可用 `--sctp` | `有條件`；Windows 無內建 SCTP stack，建議排除核心版 |
| 目的連接埠 | 無 | 無 | TCP／UDP／SCTP 可設 | `有條件`；隨進階 transport 實作 |
| UDP 來源連接埠 | 無 | 無 | `--localport` | `有條件` |
| `HOST:PORT`／IPv6 `[ADDR]:PORT` | 無 | 無 | 有 | parser 可直接做，但沒有非 ICMP transport 時沒有作用 |
| 指定來源位址 | 無 | 無 | `--address` | `可實作`；IPv4 用 `IcmpSendEcho2Ex`，IPv6 傳入明確 source |
| 指定網路介面 | 無 | 無 | `--interface` | `可實作`；Windows 介面名稱／索引需映射到來源位址與 route |
| Linux socket mark | 無 | 無 | `--mark`／`SO_MARK` | `無直接等價`；來源位址、介面或路由只能近似 |
| 權限隔離 packet helper | 無；ICMP.DLL 不需系統管理員 | 無；IP Helper 不需系統管理員 | Unix 以 `mtr-packet` 隔離 raw 權限；Cygwin ICMP 不需 privileged init | ICMP `不需照搬`；進階 transport 再設計權限邊界 |

## 6. 統計欄位矩陣

| mtr 欄位 | upstream mtr 意義 | 原始 WinMTR | 目前 WinMTR | 判定與差異 |
| --- | --- | --- | --- | --- |
| `L` Loss% | `(Sent - transit - Recv) / (Sent - transit)` | 有，簡單 `(Sent - Recv) / Sent` | 有「丟包」，沿用簡單公式 | 兩版都 `近似`；原始版延後累加 Sent，目前版先累加，瞬時顯示也不同 |
| `D` Drop | `Sent - transit - Recv` | 無 | 無 | `可實作`；transit 是每跳 0／1，不是完整 pending count |
| `R` Recv | 收到數 | 有 | 有 | 欄位本身 `已有`，但樣本排程不同 |
| `S` Sent | 送出數 | 有 | 有 | 兩版都不是 mtr 共同輪次 |
| `N` Last | 最新 RTT | 有 | 有 | Windows 兩版都是整數毫秒 |
| `B` Best | 最低 RTT | 有 | 有 | Windows 兩版都是整數毫秒 |
| `A` Avg | 算術平均 RTT | 有 | 有 | Windows 兩版都是整數毫秒 |
| `W` Worst | 最高 RTT | 有 | 有 | Windows 兩版都是整數毫秒 |
| `V` StDev | 樣本標準差，分母 `n-1` | 無 | 有，但用母體標準差 `n` | 目前 `近似`，需改公式 |
| `G` Gmean | RTT 幾何平均 | 無 | 無 | `可實作` |
| `J` Jttr | 最新兩筆 RTT 的絕對差 | 無 | 內部有 `lastJitter`，未輸出 | `可實作` |
| `M` Javg | 相鄰 RTT 差的移動平均，第一筆以 0 納入樣本數 | 無 | 「抖動」除以 `Recv - 1` | `近似`；mtr 除以 `Recv`，數值不完全相同 |
| `X` Jmax | 最大相鄰 RTT 差 | 無 | 無 | `可實作` |
| `I` Jint | RFC 1889 interarrival jitter | 無 | 無 | `可實作` |

其他統計差異：

- mtr Unix backend 內部以微秒保存 RTT；原始與目前 WinMTR，以及 mtr Cygwin backend，最終都受 ICMP.DLL 整數毫秒限制。
- mtr 可用 `--order` 自訂欄位與順序；目前 WinMTR 欄位固定。
- mtr 可在延遲欄位與 jitter 欄位組合間即時切換；目前需固定顯示所有既有欄位。
- 原始 WinMTR 固定 9 欄；目前固定 14 欄，TXT／HTML／CSV／JSON 尚未共用一份可自訂欄位模型。

## 7. DNS、ASN 與 IP 資訊

| 功能 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 判定與差異 |
| --- | --- | --- | --- | --- |
| 反向 DNS | 每個新位址另開 DNS thread | 發現位址時同步查 DNS／ASN／ISP | 獨立 resolver process 與快取 | 目前相對原版 `回歸`；Phase 1/2 必須重新隔離 |
| 執行期間切換 DNS 顯示 | 無 | 無；追蹤期間選項停用 | `n` 即時切換 | `可實作` |
| 同時顯示 Hostname 與 IP | 主表只顯示其一；雙擊詳細資料可看兩者 | 主表只顯示其一；JSON 可保存兩者 | `--show-ips` | `近似`；應提供欄位或顯示模式 |
| ASN | 無 | 有 | 有 | 目前已補上，但資料來源、格式與失敗語意不同 |
| Country code | 無 | 有 | IP info field 2 | 目前已補上；provider 不同 |
| IP prefix | 無 | 無 | IP info field 1 | `可實作`；現有 Cymru 回覆解析可擴充 |
| RIR | 無 | 無 | IP info field 3 | `可實作` |
| Prefix allocation date | 無 | 無 | IP info field 4 | `可實作` |
| 自訂 IPv4／IPv6 IP info provider | 無 | 編譯期巨集 | 命令列可指定 Cymru provider | `近似`；可增加執行期進階設定 |
| ISP 完整名稱 | 無 | 有 | 主要提供 AS／prefix 類欄位 | 目前 WinMTR 額外功能，保留 |
| 公網 IPv4／IPv6、Hostname、國家、城市、地區 | 無 | 有 | 無整合摘要 | 目前 WinMTR 額外功能，保留 |
| DNS resolver／ECS 診斷 | 無 | 有，Akamai TXT | 無 | 目前 WinMTR 額外功能，保留 |
| 查詢備援 | 無 | ipinfo 優先，ipify／ipapi／Team Cymru 備援 | 主要為 Cymru provider | 目前 WinMTR 額外功能；保留並揭露速率、隱私與可用性 |

建議把 DNS、ASN、ISP 查詢改為有限併發的工作佇列，並以 IP 為 key 做快取。查詢結果回來只更新 metadata，不得延遲或改變探測輪次。

## 8. 輸出、命令列與互動功能

### 8.1 輸出格式

| 功能 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 判定與建議 |
| --- | --- | --- | --- | --- |
| GUI 即時表格 | MFC 固定 9 欄 | MFC compact、自動列寬、14 欄 | curses／GTK | 目前 GUI 明顯改善；不需照搬 toolkit |
| 文字報告 | 複製／匯出 TXT | 複製／匯出 TXT | `--report` | 兩版 WinMTR 都不是無頭 CLI report，格式不相容 |
| Wide report | 固定 GUI 欄寬，文字以固定版型產生 | 表格依內容展開且不省略 | `--report-wide` | 目前 GUI 能力近似，CLI 語意未實作 |
| Report on exit | 手動複製／匯出 | 手動複製／匯出 | `--report-on-exit` | `可實作`自動輸出 |
| HTML | 複製／匯出，舊式 HTML | UTF-8、escaping、複製／匯出 | 無主要 HTML 模式 | WinMTR 額外功能；目前較安全，保留 |
| CSV | 無 | 有 | 有 | 目前 schema 與 mtr 不相容 |
| JSON | 無 | 有 | 有 | 目前 schema 與 mtr 不相容 |
| XML | 無 | 無 | 有 | `可實作` |
| Raw event stream | 無 | 無 | 有 | `可實作`；需先完成共用 probe event model |
| Split protocol | 無 | 無 | 有 | `可實作但低價值`；主要供外部 Unix UI |
| 截圖到剪貼簿 | 無 | 有 | 無 | 目前 WinMTR 額外功能，保留 |
| 分享前未滿 100 包提示 | 無 | 有 | 無 | 目前 WinMTR 額外功能；共同輪次後判斷才穩定 |
| 編碼與輸出安全 | ANSI clipboard、固定陣列及 `strcat`／`sprintf` | Unicode clipboard、UTF-8、stream 與 escaping | 依輸出格式處理 | 目前相對原版有實質改善，不應為相容舊格式而退回 |

### 8.2 命令列

原始 WinMTR 已支援目標、`--numeric`、`--interval`、`--size`、`--maxLRU`、`--help`，但使用手寫字串搜尋 parser。當前版改用 `CommandLineToArgvW`，參數集合大致相同，Unicode 與 quoting 較可靠，而且仍開啟 GUI。upstream mtr 另外提供：

| 選項 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 相容決策 |
| --- | --- | --- | --- | --- |
| 目標主機 | 有 | 有 | 有 | 保留；補上 mtr 的 `HOST:PORT` parser |
| `-h`／`--help` | 有 | 有 | 有 | 可直接相容 |
| `-n` | `--numeric` | `--numeric` | `--no-dns` | 短參數相容；兩個 long name 都可接受 |
| `-i`／`--interval` | 有 | 有 | 有 | 名稱相容，核心 interval 語意尚未相容 |
| `-s` | `--size`，payload bytes | `--size`，payload bytes | `--psize`，含 headers | 接受兩個 long name，但 mtr 相容模式採線上封包總大小 |
| `-m` | `--maxLRU` | `--maxLRU` | `--max-ttl` | **直接衝突**；`-m` 應改為 max TTL，歷程上限只保留 `--maxLRU` |
| report／exit code | 無，一律 GUI | 無，一律 GUI | 有多種無頭模式 | `可實作`，但需處理 GUI subsystem 的 console 等待行為 |

- `-4`、`-6`、版本資訊與同義選項。
- report cycles、report／wide／XML／raw／CSV／JSON／split。
- 欄位順序、IP info 欄位與 provider。
- grace、TOS、MPLS、interface、source address。
- first／due／max TTL、max unknown、max display path、cache。
- UDP／TCP／SCTP、目的與來源連接埠、timeout、mark。
- 從檔案批次讀取目標。
- `MTR_OPTIONS` 與 `MTR_PACKET` 環境變數。

大部分 parser 與輸出模式都是 `可實作`，沒有平台障礙。需要注意：目前 EXE 是 GUI subsystem；若要求像 Unix mtr 一樣可直接接 pipeline 並同步等待結束，可採以下其中一種方式：

1. 同一 EXE 在 report 模式附加到父 console 並寫 stdout。可維持單一檔案，但 Windows shell 對 GUI subsystem 的等待行為需要額外處理與測試。
2. 增加很小的 console frontend EXE，共用核心 library。CLI 體驗最好，但不再是單一執行檔。

優先採方案 1，若 Windows 7 的 cmd／PowerShell 重導向行為無法一致，再評估方案 2。

### 8.3 執行期間控制

| 功能 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 判定 |
| --- | --- | --- | --- | --- |
| 開始／停止 | 有 | 有 | 有 | `已有`；兩版 WinMTR 都要等同步 API 返回才能完全停下 |
| 暫停／繼續且保留工作階段 | 無 | 無；停止後重新開始 | 有 | `可實作` |
| 重設統計 | 無獨立按鈕；新追蹤才重設 | 有 | 有 | 目前已補上 |
| 即時切換 DNS | 無 | 無 | 有 | `可實作` |
| 即時調整 interval／TTL／size／pattern／TOS | 無 | 無，追蹤時選項停用 | 有 | `可實作`；從下一輪生效 |
| 即時切換 ICMP／UDP | 無 | 無 | 有 | `有條件`；取決於進階 transport |
| 自訂欄位與順序 | 無 | 無 | 有 | `可實作` |
| Statistics／Jitter 顯示切換 | 無 | 無 | 有 | `可實作` |
| Strip chart／scale | 無 | 無 | 有 | `可實作`；用輕量 MFC custom draw |
| Compact display | 一般可縮放 MFC 視窗 | compact GUI、無回應列合併 | compact curses | 目前已大幅改善，能力近似但呈現不同 |
| GTK | 無 | 無 | 選用 | `不需照搬`；MFC 已是 Windows GUI |
| curses／braille chart | 無 | 無 | 有 | `不需照搬`；在 GUI 提供等價歷程圖即可 |

### 8.4 Windows GUI 專屬能力

| 功能 | 原始 WinMTR | 目前 WinMTR | upstream mtr | 決策 |
| --- | --- | --- | --- | --- |
| 目標歷程 | Registry LRU | 保留並可設定上限／清除 | 無 GUI 歷程 | WinMTR 優勢，保留 |
| 每跳詳細資料 | 雙擊顯示 Host、IP 與基本統計 | 保留並擴充國家／ASN／ISP | 直接在畫面切換欄位 | 保留 modal 詳情，同時補上可選欄位 |
| 恢復預設選項 | 無 | 有 | 重啟即採預設或用環境選項 | WinMTR 優勢，保留 |
| 自動欄寬／視窗大小 | 固定欄寬、一般 resize | 依完整內容調欄寬與視窗 | 依 terminal／wide mode | 目前能力保留；ECMP 與圖表加入後須重新驗證 compact 寬度 |
| 繁體中文與字型 | 英文硬編碼、ANSI | 台灣繁體中文資源，中文 UI 字型與 Consolas 資料字型 | 主要為英文 terminal／GTK | 目前產品需求，保留 |
| 可自訂品牌與端點 | 原始常數散落 | 集中於 customization header／文件 | build／CLI options | 目前能力保留，新增字串也必須沿用集中管理 |
| 快捷鍵與分享工具 | 基本按鈕 | Alt 快捷鍵、複製／匯出選單、截圖 | curses runtime keys | 各自符合平台；功能語意需要對齊，不要求相同按鍵 |

## 9. 為什麼後面跳數的 Sent 會大於前面

這不是網路封包「跳過」前面的路由器，也不是路由器把 Sent 改掉。Sent 是本機程式對每個 TTL 實際呼叫探測 API 的次數。

原始與目前 WinMTR 每個 TTL 都有獨立執行緒：

- TTL 1 若持續逾時，每次同步呼叫可能花 3 秒。
- TTL 8 若快速收到 `TTL expired`，可能約 1 秒就能送下一包。
- TTL 8 因此會比 TTL 1 累積更多 Sent。
- 原始版的 DNS 在另一條 thread，不會造成這項延遲；目前版的 DNS／ASN／ISP 查詢會讓首次回覆後的該條 probe thread 額外落後。
- 目的地被某執行緒發現之前，所有更深 TTL 都已同時開始工作。

mtr 的共用輪次設計會從根本解決這個問題：每輪每個 TTL 最多排程一次，逾時由非同步狀態追蹤，不會阻塞下一跳，也不會讓某一跳自行超前多輪。

## 10. 建議的 Windows 架構

### 10.1 核心元件

| 元件 | 責任 |
| --- | --- |
| `TraceSession` | 工作階段狀態、開始／暫停／停止、設定快照 |
| `ProbeScheduler` | mtr 式 batch／TTL 排程、cycles、grace、cache、動態路徑長度 |
| `IcmpProbeTransport` | Windows 非同步 ICMPv4／ICMPv6 送出與完成通知 |
| `ProbeTracker` | probe ID、TTL、送出時間、Pending／Replied／Expired／Cancelled，以及 mtr 相容 transit marker |
| `HopModel` | 每跳 counters、RTT 統計、最新 responder、多路徑與最近 400 筆歷程 |
| `ResolverQueue` | 反向 DNS、ASN、ISP 的有限併發查詢與 IP 快取 |
| `TraceSnapshot` | 對 UI／匯出提供一致且唯讀的資料快照 |
| `ReportWriter` | TXT／HTML 與 mtr 相容 CSV／JSON／XML／raw 輸出 |

### 10.2 執行緒與完成通知

建議採 upstream `packet/probe_cygwin.c` 已驗證的 Windows 模式，並移除 Cygwin pipe 相依性：

1. 一條 scheduler thread 負責 batch、TTL、cycle 與 timer，不再建立一個 TTL 一條永久執行緒。
2. 一條 ICMP service thread 從 request queue 取得 probe，呼叫 `IcmpSendEcho2`／`Icmp6SendEcho2`，每筆 probe 擁有獨立 reply buffer 與 context。
3. ICMP service thread 以 `WaitForSingleObjectEx(requestEvent, timeout, TRUE)` 進入 alertable wait，讓 Windows 以 APC completion routine 回報 reply 或 timeout。
4. APC 只把結果放入 completion queue 並喚醒 scheduler；統計、路徑模型與 UI snapshot 一律由 scheduler 順序更新。

這個設計只需要等待一個 request event，不受 `WaitForMultipleObjects` 64 handles 上限影響，也比 thread-pool callback 更接近 upstream Windows backend。Windows 7 已具備所需 ICMP 與 alertable wait API。

內部應精確保存：

```text
sent       = 已提交給 transport 的總數
received   = 成功回覆數
pending    = 尚未回覆且尚未逾時的完整數量
expired    = 已確認逾時的數量
cancelled  = 因工作階段取消而停止等待的數量
```

但 mtr 相容欄位必須依 upstream 的可觀察公式另行計算：

```text
transit    = 每跳 0 或 1；送出時設為 1，收到該跳回覆時設為 0
loss_base  = sent - transit
dropped    = loss_base - received
loss       = dropped / loss_base
```

這表示 mtr 不會排除所有 pending，只暫時排除每跳的一包。精確 pending model 仍值得保留，可用於事件生命週期、資源回收與除錯；預設表格及相容輸出則採 mtr 公式，避免「看似復刻、數字卻不同」。report cycles 結束時先停止新增 probe，進入 grace period；離開事件迴圈後 upstream 會清除 transit marker，使最終未回覆的最後一包也納入丟包。

### 10.3 精度

第一階段可沿用 IP Helper 回覆的整數毫秒，先確保排程與統計正確。另用 `QueryPerformanceCounter` 記錄送出及完成時間可提供較細顯示，但完成 callback 的排程延遲會混入結果，不能宣稱與 raw packet timestamp 完全等價。真正接近 mtr 微秒精度的模式應與進階封包擷取 transport 一起評估。

## 11. 分階段實作與驗收

每一階段完成後皆直接在 `main` 建立獨立 commit 並 push。

### Phase 0：功能稽核與規格

- 交付本文件。
- 鎖定原始 WinMTR、目前 WinMTR、upstream mtr 三個基準與 Windows 支援範圍。
- 明確區分核心、Windows 原生、選用進階與不直接移植的功能。

驗收：文件涵蓋功能、語意、限制、實作可行性與優先順序。

### Phase 1：mtr 等價 ICMP 核心

- 以共用 batch scheduler 取代每 TTL 永久執行緒。
- 改用非同步 ICMP，建立 probe 狀態與完成佇列。
- 正確處理 Sent、sequence、transit marker、Drop、Loss 與 cycles。
- 加入 grace period，並明確區分 report 正常結束與使用者立即取消的 finalization 行為；mtr 相容輸出必須遵循 upstream 清除 transit marker 的結果。
- 先建立最小 resolver worker queue，確保 DNS／ASN 不再於 probe 完成路徑同步執行。

驗收：

- 無 cache 且穩定追蹤時，各有效跳 Sent 相差不超過 1。
- 人工延遲或逾時某一跳，不會使其他跳多跑數輪。
- Loss／Drop 與 upstream 的每跳 transit 公式一致，並以短 interval、長 timeout 測試多筆 pending 情境。
- 100 cycles 的語意是 100 個完整路徑輪次。
- Windows 7／10／11 x64 Release build 可執行，ICMP 不需管理員權限。

### Phase 2：解析佇列、路徑變動與 ECMP

- 擴充 resolver queue 的有限併發、DNS／ASN／ISP 快取與取消行為。
- 每跳保存最新 responder 與最多 128 個曾回覆位址。
- 新增 ECMP 顯示上限，預設 8。
- 加入 first TTL、due TTL、max unknown、動態目的地跳數與 cache。
- 保留無回應連續列合併，但底層統計仍逐跳獨立。

驗收：名稱服務延遲不影響 Sent；模擬多來源回覆時能顯示並匯出所有路徑。

### Phase 3：完整統計與顯示欄位

- Drop、Gmean、Jttr、Javg、Jmax、Jint。
- 將 StDev 改為與 mtr 相同的 `n-1` 定義。
- 保存最近 400 次結果。
- 自訂欄位、順序與 Statistics／Jitter presets。
- 增加輕量 strip chart 與 scale，維持 compact 視窗。

驗收：以固定 RTT 測試向量逐欄比對 mtr 計算結果；GUI、匯出共用同一欄位模型。

### Phase 4：CLI 與輸出相容

- report、wide report、report on exit。
- mtr schema 的 CSV、JSON、XML、raw；保留現有 HTML 與使用者友善格式。
- `-4`／`-6`、count、size、grace、TOS、first／due／max TTL、max unknown、cache、order、IP info 等選項。
- 批次目標檔案與 `MTR_OPTIONS`。
- 評估同一 GUI EXE 附加 console 的重導向相容性。

驗收：相同固定資料集輸出可由既有 mtr parser 讀取；錯誤參數回傳非零 exit code。

### Phase 5：Windows 原生進階路由控制

- 指定來源 IPv4／IPv6 位址。
- 以介面名稱／索引選擇網路介面。
- 清楚顯示兩者皆勾是自動選擇，或新增 IPv4／IPv6 雙工作階段模式。
- 驗證 packet size、TOS、IPv6 Traffic Class 與 DF 的跨版本行為。

驗收：多網卡環境能確認封包由指定來源送出；不需安裝第三方驅動。

### Phase 6：選用進階封包 transport

- 先做技術驗證，比較 Npcap 與 WinDivert 的 Windows 7 支援、授權、安裝體積、簽章與維護成本。
- 在明確提示權限及相依性的前提下實作 UDP、TCP SYN、目的／來源連接埠。
- 若擷取層可取得完整 ICMP extension，再加入 MPLS RFC 4950 與較高精度 timestamp。
- SCTP 另做可行性決策，不應綁進預設核心版。

驗收：未安裝進階元件時核心 ICMP 仍完整可用；進階模式缺少權限或驅動時要顯示可理解的錯誤，不可靜默退回不同語意。

## 12. 測試策略

- `ProbeScheduler` 使用可注入的 clock 與 fake transport 做單元測試。
- 覆蓋依序回覆、亂序回覆、重複回覆、晚到回覆、逾時、取消、grace 與多筆 in-flight。
- 覆蓋目的地跳數變短／變長、連續未知、cache、due TTL 與 ECMP。
- 以固定 RTT 序列驗證所有統計，尤其是 StDev、Gmean 與四種 jitter。
- 同一網路、同一目標以 `mtr -n -c 100 -i 1` 比對 Sent、Recv、Drop、Loss 及 RTT 趨勢；不要求兩個工具在不同時間收到完全相同網路結果。
- 驗證 TXT／HTML 舊格式不意外破壞，另以 schema fixture 驗證 mtr CSV／JSON／XML／raw。
- 每階段執行 x64 Debug／Release build；里程碑版本在 Windows 7、10、11 實機驗證。
- 每次 UI 變動均編譯、開啟實際 EXE 並檢查 compact 版面、表格完整顯示與 100%／125%／150% DPI。

## 13. 建議取捨

為了同時達成「盡量復刻 mtr」與「精簡、Windows 7–11 x64、單一執行檔」，建議正式產品範圍如下：

1. **必須等價**：ICMP 排程、probe 生命週期、丟包語意、cycles、路徑變動、ECMP、完整統計、欄位、主要 report 格式。
2. **Windows 原生完成**：來源位址／介面、批次、CLI、圖表、DNS／ASN 非同步化。
3. **選用進階功能**：TCP、UDP、MPLS；只有在外部封包元件的授權、簽章與 Windows 7 支援可接受時加入。
4. **預設不納入**：SCTP、Linux `SO_MARK`、GTK、curses、`DISPLAY`；前兩者有實質平台成本，後三者已有 MFC 等價介面。
5. **保留 WinMTR 優勢**：公網 IP／網路資訊、DNS／ECS、ISP、HTML、截圖、分享提示、無回應列合併、compact 自動尺寸與完整繁體中文介面。

依此順序，Phase 1 至 Phase 5 不需因 TCP／UDP 的驅動選擇而停住，也不會為了追求選項數量而犧牲目前的免安裝與精簡特性。

## 14. 原始碼依據

本稽核不是只依 README 或畫面名稱判斷，主要程式依據如下：

| 比較方 | 來源 | 用途 |
| --- | --- | --- |
| 原始 WinMTR | `git show 06ece5f:WinMTRNet.cpp` 的 `DoTrace`、`TraceThread`、`SetAddr`、`DnsResolverThread` | 固定 30 TTL threads、同步 IPv4 ICMP、Sent 累加時機、獨立 DNS thread、第一位址鎖定 |
| 原始 WinMTR | `git show 06ece5f:WinMTRGlobal.h` | 固定 9 欄、預設設定、未實際使用的 `SAVED_PINGS` 常數 |
| 原始 WinMTR | `git show 06ece5f:WinMTRDialog.cpp`、`WinMTRMain.cpp` | TXT／HTML、詳細資料、Registry LRU、舊命令列 parser |
| 目前 WinMTR | `WinMTRNet.cpp` 的 `TraceThread`、`DoTrace`、`RecordSent`、`GetHopSnapshot` | 每 TTL thread、同步 IPv4／IPv6、目前 Loss／Jitter／StDev 公式 |
| 目前 WinMTR | `WinMTRDialog.cpp` 的 `ResolveTraceTarget`、`DisplayRedraw`、report builders | 位址族群選擇、無回應列合併、固定輸出 schema |
| 目前 WinMTR | `WinMTRNetworkInfo.cpp`、`WinMTROptions.cpp`、`WinMTRMain.cpp` | metadata provider、選項與目前命令列 |
| upstream mtr | `ui/net.c` 的 `calc_deltatime`、`save_sequence`、`net_process_ping`、`net_loss`、`net_send_batch` | batch 排程、sequence、transit、統計、多路徑 |
| upstream mtr | `ui/select.c`、`ui/dns.c`、`ui/cmdpipe.c` | event loop、resolver process、`mtr-packet` 非同步命令 |
| upstream mtr | `packet/probe_cygwin.c` | Windows ICMP-only 限制、APC ICMP service thread、毫秒精度 |
| upstream mtr | `ui/mtr.c`、`ui/report.c`、`man/mtr.8.in` | 完整統計欄位、輸出格式、命令列與互動功能 |

upstream 的 Windows／Cygwin backend 本身只支援 ICMP，這是 TCP／UDP／SCTP 未列入原生 Phase 1 的直接程式證據，而不是因為本專案暫時不想實作。相反地，共用排程、非同步 ICMP、ECMP、統計與輸出都沒有同等平台障礙，屬於可以完成但尚未完成的工作。
