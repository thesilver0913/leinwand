# Leinwand

Adobe Illustrator 相当を最終目標とする、デスクトップ向けベクターグラフィックエディタ。趣味・学習目的の個人開発。

## 最初に読むもの

- `docs/spec.md` — 仕様書(全10章)。設計判断の根拠はすべてここにある。
- `docs/phase1-plan.md` — フェーズ1の計画(リポジトリ構成、ビルド環境、マイルストーン M0〜M7、CI)。
- `docs/implementation-notes.md` — 実装時の合意事項(依存ライブラリの使い方、パッチ、制約)。

仕様書と実装が食い違う場合は、勝手にどちらかへ合わせず、食い違いを報告して確認を取ること。

## 決定事項の要約

| 項目 | 決定 |
| --- | --- |
| 言語・ライブラリ | C++20、Skia、Qt 6(Qt Quick / QML)。Qt は LGPLv3 で動的リンク。GPL 専用の Qt モジュールは使わない |
| 描画 | 最初から GPU。Vulkan が第一候補(macOS 対応時に Metal を追加) |
| 対応 OS | Windows → Linux → macOS の順。フェーズ1のリリースは Windows のみ。Linux はビルドとテストを通し続ける |
| ライセンス | GPL-3.0-or-later。ソースファイル先頭に `SPDX-License-Identifier: GPL-3.0-or-later` |
| 保存形式 | 拡張子 `.lwd`。ZIP コンテナ内に `document.json`(インデント付き、キー順固定) |
| 座標系 | 単位はポイント、倍精度、原点は左上で Y 軸は下向き |
| 操作体系 | ほぼ Illustrator 互換(ツール、修飾キー、メニュー、パネル名、ショートカット) |
| デザイン言語 | Adobe Spectrum 2。デザイントークンとワークフローアイコン(ともに Apache 2.0)を使う。Adobe Clean フォントは使用不可。UI フォントは Source Sans 3 と源ノ角ゴシック |
| UI 言語 | 日本語と英語。文言は Qt の翻訳(.ts)で管理し、ソースに直接書かない |
| 配布 | Windows は Inno Setup の Web インストーラー、Linux は deb。配布元は GitHub Releases |

## アーキテクチャの約束

- 層の依存は一方向: `app` → `ui` / `render` / `io` → `geometry` → `core`。
- `core`(ドキュメントモデル、コマンド、取り消し履歴)は Qt にも Skia にも依存させない。
- Skia の型は `render` の外に出さない。パス演算は `geometry` のインターフェース越しに呼ぶ(最初の実装は Skia PathOps)。
- OS 固有の処理(フォント列挙、設定フォルダ、IME など)は `platform` の裏に置く。
- すべての編集はコマンドとして記録し、取り消し/やり直しできるようにする。
- モデルは不変データ構造(構造共有)を基本とする。
- 読み込みで対応できなかった要素は黙って捨てない(読み込みレポートに出す)。
- `.lwd` の未知のフィールドは保持して書き戻す。

## 開発ルール

- ビルドは CMake + Ninja(CMakePresets)。依存ライブラリは vcpkg のマニフェストモード。
- テストは Catch2。`core` と `geometry` の公開関数には単体テストを付ける。描画と入出力は基準画像との比較テスト。
- 書式は clang-format、静的解析は clang-tidy。
- `main` は常にビルドとテストが通る状態を保つ。作業はブランチで行い、プルリクエストで CI を通してからマージする。
- コミットメッセージは英語。

## 現在の位置

フェーズ1の M0(環境構築と技術検証)。技術検証3件は結論が出た(結果は `docs/m0-verification.md`)。

1. Skia と Qt Quick のシーングラフが同じ Vulkan コンテキストで描画できるか → 成立。
2. 1万個のパスを表示して、ズームとパンが滑らかに動くか → 60Hz で成立。
3. KDDockWidgets の見た目を Spectrum に合わせられるか → パッチを当てて採用。

M0 と M1(モデルと描画)は完了。次は M2(選択と変形、コマンドと取り消し)。実装で合意したことは `docs/implementation-notes.md` にまとめる。

## 未検証の前提(鵜呑みにしないこと)

- 仕様書 4.2(ショートカット、ペンツールの操作表)と 7.1・7.2(メニュー、パネル)は、Illustrator 実機と未照合。
- Crashpad が Windows と Linux の両方で使えるか、vcpkg で取得できるかは未確認。
- Fontsource の CDN のフォントで、日本語のグリフと縦組み用 OpenType 機能がそろうかは未確認。
- PDFium から取り出せる描画オブジェクトの範囲(ソフトマスク、メッシュグラデーション)は未確認。
