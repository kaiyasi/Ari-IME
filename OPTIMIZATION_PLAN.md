# 知字 Ari IME 優化方案

這份文件整理目前已落地的優化，以及後續值得繼續做的項目。分類以輸入法本身的可用性為主：功能完整度、UX、效能與工程品質。

## 已完成

### 功能與輸入法完整度

- 支援多種注音鍵盤配置：大千、倚天、許氏、IBM、精業、Dvorak、Carpalx、Colemak-DH、Workman、Colemak。
- 將鍵盤配置抽成共用 layout 層，輸入狀態機與 libchewing 鍵盤型別共用同一套設定。
- 保留混合中英文 pre-edit，只有 Enter 送出，避免中文候選與英文片段提早打到應用程式。
- 支援任意位置候選重選，包含片語候選、單字候選、raw key 還原、點選/觸控候選。
- 讓 live pre-edit 顯示結果與候選窗第一候選一致，降低「打字時看到 A，選字時第一個是 B」的落差。
- 候選詞翻頁、Tab/Shift+Tab 導覽、PageUp/PageDown、數字鍵與候選點選走一致邏輯。
- 支援 Home/End/Delete、方向鍵、mid-string 插入與刪除。
- 候選窗會以 `原始鍵 ...` 標示 raw-key 還原項，降低最後一個候選用途不明的 UX 問題。
- 支援貼上到目前 caret，並把 ASCII 控制字元折成可見空白，避免多行或控制字元破壞 pre-edit。
- 貼上正規化補強 Unicode NBSP、line separator、paragraph separator，避免從網頁/PDF/聊天工具貼入時出現不可見分隔或多行 pre-edit。
- 支援 Ctrl+Space 強制英文模式，且模式跨 reset、Esc、commit 保持。
- 支援可選全形標點，並保留注音韻母鍵與 numeric keypad 的原本語意。
- 修正 numeric keypad：NumLock on 時輸入 literal 數字/符號，NumLock off 時作為導覽鍵。
- 補上 email、URL、版本號、檔名、英文縮寫接中文等常見混輸 regression case。
- 修正英文尾端注音剝離 heuristic，避免版本號 `.2.3` 被誤轉中文，同時保留 `catsu3` 這類英文單字友善行為與 `HTTPsu3` 縮寫接中文行為。
- 補強英文 cell 重新解讀注音的 regression，避免檔名、英文單字、版本號標點被 ↑ 誤轉中文，同時保留 `catsu3` 救回 `cat你` 的行為。
- 擴充英文 cell 重新解讀注音測試到路徑、命令列參數、程式碼片段，避免 ↑ 把 literal `su3` 片段誤轉成中文。
- 持續擴充英文 cell 重新解讀注音測試到 shell 管線、URL query string、程式語言泛型語法、Markdown inline code、JSON key/value、含底線識別字、log key-value、SQL identifier、CSS selector，避免真實技術文字被 ↑ 誤轉中文。
- 英文 cell 重新解讀注音再補 YAML/TOML key、Docker image tag、regex token、Git ref、hostname/host:port，行首 technical literal 不會因 ↑ 被誤轉中文。
- 英文 cell 重新解讀注音再補 Makefile target、IPv6-like literal、templating variable closing delimiter，避免行首 `su3` 類變數/target 被 ↑ 誤轉中文。

### UX

- pre-edit auxiliary line 顯示中/英模式、鍵盤配置、半形/全形標點狀態。
- 全形/半形標點設定變更時顯示短暫提示，和鍵盤配置切換提示一致。
- 已評估快速切換全形標點快捷鍵：目前不加入固定預設鍵，避免攔截
  `Ctrl+.` 等應用程式常用快捷鍵；後續若要加入，應先做成可配置快捷鍵並明確定義 per-context / global 設定語意。
- 候選詞多頁時顯示目前頁數與總頁數。
- 點選候選後與數字鍵選字行為一致。
- UI 傳入過期候選索引時吸收事件但不關閉候選窗，避免 stale click 造成候選窗消失。
- 切換鍵盤配置時清掉未送出的 pre-edit 並顯示短暫提示，避免舊 layout reading 被新 layout 誤解。
- 安裝 hicolor SVG icon，讓設定工具與輸入法列表不再使用空白圖示。
- 補強全形標點 regression 覆蓋，包含逗號、句號、問號、括號、角引號、雙角引號、驚嘆號與冒號。
- 全形標點補上頓號與省略號 fallback，libchewing 未回傳中文標點時仍可輸出 `、` 與 `……`。
- 全形標點補上常見 ASCII 符號全形化：`@ # $ % & * + = | ~`。
- 新增 `docs/manual-qa.md`，把 GTK/Qt/Electron、Wayland/X11、候選點選、剪貼簿、數字鍵盤、設定切換等實機驗證流程整理成 checklist。

### 效能與穩定性

- layout slot table lazy init，只在實際使用某個 layout 時探測 libchewing key slot。
- 候選頁面 accessor 使用 `visibleCandidateCount()` 集中計算頁面大小，避免 `highlight()` / direct selection 為了判斷狀態重建候選字串 vector。
- 測試隔離 libchewing 使用者字典，避免本機學習資料影響測試結果。
- 加入 `-Wall -Wextra -Wpedantic` 編譯警告門檻，讓未來改動更早暴露可疑程式碼。
- 加入 `INPUTER_ENABLE_SANITIZERS` CMake 選項，可用 ASan/UBSan 跑狀態機測試。
- 加入 `scripts/check.sh` 本地 CI 腳本，整合一般 build、CTest、install smoke check、sanitizer profile、PKGBUILD syntax check，並可選擇跑離線 Arch package 模擬。
- 加入 GitHub Actions workflow，在 Arch Linux container 中重用本地檢查腳本並跑 package simulation。
- GitHub Actions 拆成 release、sanitizer、package simulation 三個 jobs，各自回報失敗位置，同時共用 `scripts/check.sh` 的 mode 入口。
- 加入 deterministic key-sequence stress test，固定 seed 跑大量按鍵序列並檢查 preedit UTF-8、caret、selection、candidate page/highlight 等公開 invariant。

### 發佈與包裝

- addon descriptor 由 CMake configure 產生，版本跟專案版本同步。
- 安裝 addon conf、input method conf、icon、fcitx5 module。
- PKGBUILD 使用 tagged source tarball，`check()` 會跑 CTest。
- `.SRCINFO` 與 PKGBUILD 同步。
- 本地 release/package checks 會驗證 CMake project version、PKGBUILD `pkgver`、`.SRCINFO` `pkgver` 一致，避免 release 漏改版本。
- package simulation 不再硬編碼 source tree 版本目錄，升版後會跟著 PKGBUILD `pkgver` 建立模擬 tarball 內容。

## 後續優先順序

### P0: 實機驗證與相容性

- 依 `docs/manual-qa.md` 在至少三種實際應用程式驗證 pre-edit/candidate/caret 行為：GTK、Qt、Electron 或 browser。
- 依 `docs/manual-qa.md` 驗證不同 Fcitx5 UI theme 對 auxiliary line、candidate highlight、page text 的顯示。
- 依 `docs/manual-qa.md` 驗證 Wayland 與 X11 下 clipboard paste、candidate click、numeric keypad 行為一致。

### P1: 輸入法完整度

- 補上標點設定的 UI 說明，並持續擴充更多符號測試。
- 若要加入快速切換全形標點，先設計可配置快捷鍵，不提供固定預設鍵；
  實作時需更新持久化 config 或明確標示為 per-context override，避免 buffer runtime 狀態被下一次 config 套用覆蓋。
- 針對更多真實文字樣本持續擴充英文 cell 重新解讀注音測試，例如 shell variable、environment assignment、template filter、framework route pattern。

### P2: UX 細節

- 評估在候選窗關閉後保留 caret 位置的更多直覺行為，特別是 Esc、點選候選、Delete 後的後續輸入。
- 補充 README 的短範例錄屏或 GIF，展示混輸、重選、貼上、鍵盤配置切換。

### P3: 效能與維護

- 可再替 GitHub Actions 加上 cache 或矩陣測試不同 compiler / build type。
- 若未來 layout 探測成本變高，可把 slot table cache 持久化在 process lifetime 中並加測初始化次數。
- 若要再提高狀態機韌性，可加入 libFuzzer/AFL harness 與 coverage 報表，專門探索 `Buffer::handleKey` 的長序列邊界。
