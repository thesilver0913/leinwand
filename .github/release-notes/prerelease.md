**A test build of phase 2 (alpha).** Things may still change or break; keep copies of files you care about. The web installer keeps installing the latest regular release; this build is only here.

**フェーズ2の試用版(アルファ版)です。** 動作や仕様はまだ変わります。大事なファイルは控えを取っておいてください。Web インストーラーは引き続き正式版を入れます。この版はここからだけ入手できます。

### New in this build / この版で増えたもの

Since 0.2.0-alpha.1 / 0.2.0-alpha.1 からの追加:

- Linear and radial gradients, gradient tool — 線形・円形グラデーション、グラデーションツール
- Clipping masks, opacity masks, blend modes, Transparency panel — クリッピングマスク、不透明マスク、描画モード、透明パネル
- Point text with Japanese input (IME), Character and Paragraph panels, OS fonts with fallback; Create Outlines that can turn back into text — ポイント文字(日本語入力対応)、文字パネル・段落パネル、OS のフォントとフォールバック、文字に戻せるアウトライン化
- PDF export (fonts embedded), printing, Japanese trim marks, preflight check, live corners — PDF 書き出し(フォント埋め込み)、プリント、日本式トンボ、入稿チェック、ライブコーナー
- Cut, copy and paste (in front, in back, in place) — カット・コピー・ペースト(前面へ、背面へ、同じ位置に)

Earlier in phase 2 (alpha.1): Pathfinder, Align panel, multiple artboards, Japanese paper sizes and the book cover template, macOS.
フェーズ2でこれまでに入ったもの(alpha.1): パスファインダー、整列パネル、複数アートボード、日本の判型と表紙テンプレート、macOS 版。

Files are saved in format 1.4. Leinwand 0.1.0 still opens them and keeps what it does not know (text shows as frames there).
ファイルは形式 1.4 で保存されます。0.1.0 でも開けて、知らない内容は保ったままになります(文字は枠で表示されます)。

**Please try / 試してほしいこと**: typing Japanese with your IME, PDF export and printing, the preflight check. Report problems on GitHub Issues.
IME での日本語入力、PDF 書き出しとプリント、入稿チェックを特に試してください。不具合は GitHub の Issues へ。

### Windows

Unzip **Leinwand-…-windows-x64-preview.zip** anywhere and run `bin\leinwand.exe`. Needs a graphics driver with Vulkan. SmartScreen may warn: "More info" → "Run anyway".
zip を好きな場所に展開して `bin\leinwand.exe` を起動します。Vulkan に対応したグラフィックスドライバーが必要です。

### macOS (Apple Silicon, macOS 12 or later)

Open the **.dmg** and drag Leinwand to Applications. The app is not notarized by Apple yet, so the first start is blocked: open **System Settings → Privacy & Security** and choose **Open Anyway**.
dmg を開いて Leinwand を「アプリケーション」へドラッグします。Apple の公証を受けていないため、初回は起動が止められます。「システム設定 → プライバシーとセキュリティ」で「このまま開く」を選んでください。

Leinwand is not affiliated with or endorsed by Adobe. Adobe, Illustrator and Spectrum are trademarks of Adobe Inc.
