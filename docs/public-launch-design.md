# Ari IME 公開宣傳版設計

## 目標

第一個公開宣傳版本的目標不是「功能最多」，而是讓第一次使用者在不安裝
額外工具、不學習一套複雜 UI 的情況下，立刻感受到 Ari 比傳統切換式輸入
法省事：

> 注音、英文、數字、網址和標點可以在同一個未送出文字中混合編輯，
> 想修改任何一個字時不必刪掉重打，也不需要切換輸入模式。

Ari 自己不新增工具列、浮動視窗或常駐服務。候選窗、預編輯文字、提示和
設定全部使用 Fcitx5 原生機制；學習資料只存在本機，不連線、不上傳。

## 首發產品承諾

宣傳頁只承諾以下五件可被實際驗證的事：

1. **混合輸入**：`aceru/6aj4` 可以得到 `acer螢幕`，不用切換中英文。
2. **整段可修正**：送出前可以移動到任一字、重選詞語、插入或刪除文字，
   最後再一次送出。
3. **上下文選字**：完整音節會使用 libchewing 的詞語和上下文模型；
   完成音節後原生候選窗立即預覽目前結果和替代候選；按 Down 才進入
   互動選字，詞語候選優先於單字候選。
4. **可恢復的個人化**：選字會學習，敏感欄位不學習；使用者可以忘記單一
   個人候選、安全重置整份學習資料，或用 `ari-ime-dict` 匯出／匯入個人
   詞語，不被單一安裝綁住。
5. **離線可靠**：沒有網路、帳號、模型下載或額外背景服務也能完整輸入。

不宣稱不同 libchewing 版本一定有完全相同的第一候選字；宣稱的是輸入碼、
詞語候選可用，且使用者的明確選字會被保留。

## 對標基準

| 對象 | 值得保留的基準 | Ari 的定位 |
|---|---|---|
| libchewing／酷音 | 成熟的注音解析、詞語分段、上下文和本機學習 | 直接使用其轉換能力，再補上混合文字和可編輯緩衝區 |
| Rime 注音 | 無模式輸入、即時候選、可管理的詞典 | 保留無模式精神，但把英文、網址、符號邊界做得更保守 |
| Windows 注音 | 使用者熟悉的候選窗、數字選字和桌面整合 | 使用 Fcitx5 原生候選窗，不另造一個 Ari 視窗 |
| Ari | 中文與英文同一個 preedit、送出前任意修正、離線學習 | 這是宣傳版的主賣點，不為追求傳統行為而犧牲它 |

設計基準來自 libchewing 對候選／編輯模式的說明、Rime 注音方案的無模式
與 Space 選字習慣，以及 Fcitx5 對 preedit 和 candidate list 的原生整合。

參考：[libchewing 官方說明與版本紀錄](https://github.com/chewing/libchewing/releases)、
[Rime UserGuide](https://github.com/rime/home/wiki/UserGuide)、
[Fcitx5 candidate list／preedit 開發說明](https://fcitx-im.org/wiki/Develop_an_simple_input_method)。

## 首發鍵盤契約

首發版只保留一個預設行為，避免使用者第一次安裝就要選模式：

- 字母和數字先依目前鍵盤配置判斷為注音或英文。
- 完整音節轉成目前的上下文結果；未完成音節仍顯示原始鍵。
- Space 在有待完成的一聲音節時代表一聲，否則是文字空格。
- Enter 是唯一送出整段 preedit 的按鍵。
- Down／Left／Right 進入整段編輯和候選重選；數字鍵、滑鼠和翻頁使用
  Fcitx5 原生候選窗。
- `y` 這類單鍵不可在按下瞬間強制變成某一個字，因為它仍可能是下一個
  音節的開頭；`y` 加 Space 要依實際字典結果轉換，`資` 應可由候選或
  個人學習取得，不寫死成 `y → 資`。

未來若要支援習慣傳統注音的使用者，再增加可選的「Space 選字相容模式」；
不改變目前混合模式的預設鍵義。

## 首發必做項目

### P0：輸入核心

- 保持即時顯示的結果和候選窗第一候選一致。
- 完成中文音節後顯示非搶鍵的候選預覽；數字仍可直接開始下一個注音音節。
- 候選窗在句尾開啟時仍保留包含游標字元的完整詞語候選。
- 選完詞後可以直接繼續輸入，Right 可以到達句尾插入點。
- 補齊一聲、單字母、數字 `5`、符號音鍵、英文網址和大寫縮寫的回歸測試。
- 候選排序不要用 Ari 的靜態字表硬蓋掉 libchewing 的上下文結果；只有在
  邊界不合理時做保守的 Ari 修正。

### P0：學習和信任

- 預設保留本機學習，但提供可關閉的自動學習設定。
- 未選字結果只做弱學習；明確選字才做強學習，避免錯誤候選無限自我強化。
- 使用者明確選過或匯入的詞要提升到實際輸入結果；不依賴發行版所附
  libchewing 的頻率排序差異。普通接受結果仍只做一般學習，不能整批變成
  Ari 的硬排序。
- `Shift+Delete` 忘記單一個人候選的行為保留並寫入手冊。
- 重置腳本必須備份 `userdict.dat`、`preferences.tsv`、`chewing.dat` 和
  `chewing-deleted.dat`，並明確警告 shared libchewing 資料可能影響其他 IME。
- 敏感欄位不得寫入學習資料，並加入公開版手動驗收案例。

### P0：安裝和第一次成功輸入

- Arch 提供不需要編譯器的 `fcitx5-ari-ime-bin`，source AUR 另列為開發者選項。
- GitHub Release 同時提供 Arch binary archive、校驗檔和 Debian/Ubuntu `.deb`。
- `.deb`／AUR 安裝後，Fcitx5 能掃到 addon、input method descriptor 和 icon；
  不要求使用者手動設定 `FCITX_ADDON_DIRS`。
- InputMethod descriptor 必須標記為可設定，讓使用者能從既有的 Fcitx5
  設定流程找到鍵盤配置、標點和 AutoLearn，不另造 Ari 設定視窗。
- 安裝文件只保留一條推薦路徑，清楚列出「安裝 → 重載 Fcitx5 → 執行
  `ari-ime-enable --make-default` → 輸入 `su3`」四步；仍保留
  `fcitx5-configtool` 作為圖形化替代方案。
- `ari-ime-enable` 必須是明確執行的使用者命令，不得由套件 post-install
  偷改 profile；修改前備份現有 profile，且不刪除或重排其他輸入法。
- 發布前在 Arch、Ubuntu 的實際安裝環境驗證，不只驗證 CMake install 目錄。
- 統一公開名稱為 **Ari IME**；文件、Fcitx5 清單和手動測試不再混用「知字」。

### P1：使用者感受

- 貼上、刪除和游標以 Unicode grapheme cluster 為單位，不能拆壞 Emoji、
  變體選擇符和 ZWJ 序列。
- 候選窗在 GTK、Qt、Chromium/Electron，Wayland 和 X11 都完成一次手動驗收。
- 長 preedit 超過 libchewing active window 時，仍可編輯，但文件要明確說明
  詞語模型的上下文上限。
- 標點快捷鍵必須可在不攔截瀏覽器、編輯器常用快捷鍵的前提下工作；中文頓號
  `、` 的快捷鍵要有明確文件和回歸測試。

## 不列入首發

- 網路／雲端候選、帳號同步或遠端遙測。
- 自製工具列、候選浮窗、字典管理 GUI。
- Pinyin 模式和 Windows/macOS 原生移植；首發先把 Fcitx5 Linux 體驗做完整。
- 為了宣傳而保證所有 Linux 發行版的候選第一名完全相同。
- 自動修改桌面全域輸入法環境；安裝程式不應偷偷改變使用者的其他 IME。

## 宣傳版驗收指標

### Golden tasks

在全新學習資料下，以下案例都必須通過：

| 類別 | 操作 | 必須結果 |
|---|---|---|
| 混合 | `aceru/6aj4`、Enter | `acer螢幕` |
| 英文保留 | `README.md`、網址、版本號、Enter | 原樣保留 |
| 詞語 | `hk4g4`、句尾 Down | `測試` 是詞語候選，能選 `策士` 等替代詞 |
| 推薦 | `hk4g4`、不按 Down | 原生候選窗先顯示 `測試`，按數字仍可繼續輸入下一個音節 |
| 一聲 | `y`、Space | 產生有效漢字，`資` 可從候選取得 |
| 修正 | 句中游標、重選、Right 到句尾、再輸入 | 不需刪除整句即可完成 |
| 標點 | `Ctrl+Shift` 頓號／逗號等 | 產生正確中文標點，不吃掉一般 app 快捷鍵 |
| 隱私 | 敏感欄位選非預設候選、重試 | 不影響普通欄位的排序 |

### 發布門檻

- CTest、Sanitizer、bounded fuzz、Arch package simulation 全部通過。
- Arch binary、source AUR、Debian package 的版本和 checksum 一致。
- 至少一個 GTK、Qt、Chromium/Electron 程式在 Wayland 或 X11 完成候選、
  貼上、游標和 Enter 驗收。
- 從乾淨使用者帳號到第一次成功輸入，不需要閱讀開發文件。
- 所有已知限制集中在 `ISSUES.md`，不藏在宣傳文案裡。

## 開發順序

1. **公開版核心封鎖線**：一聲／混合輸入／候選句尾／游標插入／標點和
   Unicode 編輯回歸測試。
2. **學習可控性**：自動學習開關、個人候選忘記流程、重置備份和升級測試。
3. **安裝封鎖線**：`.deb` Release asset、AUR binary 驗證、乾淨帳號安裝手冊。
4. **實際桌面驗收**：GTK、Qt、Electron、Wayland/X11，修正只會在真實
   InputContext 出現的問題。
5. **宣傳版發布**：固定 Ari IME 名稱、更新 README／截圖案例、建立 release
   checklist，再決定是否加入傳統 Space 選字相容模式。

這個順序刻意把「輸入法本體的不可替代性」排在新增功能前面：先讓使用者
能放心把中文、英文、修正流程和個人詞語交給 Ari，再擴展其他輸入方案。
