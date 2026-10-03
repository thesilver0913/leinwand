# Leinwand

デスクトップ向けのベクターグラフィックエディタ。Illustrator に近い操作感を目指す、趣味・学習目的の個人開発です。
A vector graphics editor for the desktop, with an Illustrator-like way of working (hobby project).

## できること(v0.1)

- ペン、ダイレクト選択、アンカーの追加・削除・切り替え、スマートガイド
- 長方形・楕円・多角形・スター・直線(ライブシェイプ)
- 塗りと線、カラー・スウォッチ・線・レイヤー・プロパティ・変形の各パネル
- 独自形式 `.lwd` の保存と読み込み、自動保存と復元
- SVG の読み書き、PNG の書き出し
- 日本語と英語の UI、ライトとダークのテーマ

計画は [docs/phase1-plan.md](docs/phase1-plan.md)、仕様は [docs/spec.md](docs/spec.md) にあります。

## インストール(Windows)

[Releases](https://github.com/thesilver0913/leinwand/releases) から `LeinwandSetup.exe` をダウンロードして実行します。インストーラーが最新版(または選んだ版)を GitHub Releases から取得し、ハッシュ値を確かめてからインストールします。

- 実行には Vulkan に対応したグラフィックスドライバーが必要です。
- 現在のインストーラーにはコード署名がないため、Windows の SmartScreen が警告を出します。「詳細情報」→「実行」で続けられます。

## ビルド

Windows では `tools/setup-windows.ps1` で開発環境(Visual Studio、CMake、Qt、vcpkg)をそろえ、次を実行します。

```
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

## ライセンス

GPL-3.0-or-later([LICENSE](LICENSE))。同梱しているライブラリ、フォント、アイコンのライセンスは [NOTICE.md](NOTICE.md) にあります。

Leinwand は Adobe とは関係がなく、Adobe の承認も受けていません。Adobe、Illustrator、Spectrum は Adobe Inc. の米国およびその他の国における商標または登録商標です。
