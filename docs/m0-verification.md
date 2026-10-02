# M0 検証記録

フェーズ1計画(phase1-plan.md)の M0 で確かめる前提と、その結果を記録する。結果が出たら各節の「結果」を埋め、M1 へ進むか方針を見直すかを判断する。

| 検証 | 状態 |
| --- | --- |
| 1. Skia と Qt Quick が同じ Vulkan コンテキストで描画できるか | 未実施(試作コードあり) |
| 2. 1万個のパスでズームとパンが滑らかか | 未実施(試作コードあり) |
| 3. KDDockWidgets の見た目を Spectrum に合わせられるか | 未着手 |
| 付随: vcpkg の Skia で Vulkan・PDF・PathOps が有効にできるか | ポートの定義上は可(ビルド未確認) |

## 付随: vcpkg の Skia

vcpkg のベースライン `9e593bb1`(タグ 2026.07.29)の `skia` ポートを調べた。

- `vulkan` と `pdf` はフィーチャーとして選べる。`vcpkg.json` では既定のフィーチャーを外し、`pdf`、`png`、`vulkan` だけを指定した。
- PathOps はポートのパッチ(`010-always-build-pathops.patch`)で常にビルドされる。
- ヘッダーは `include/skia` の下に置かれ、`#include "include/core/SkCanvas.h"` の形で参照する。

確認方法: `tests/skia_features_test.cpp` が PathOps の和演算と PDF の書き出しを実行する。Vulkan は `render` が `GrDirectContexts::MakeVulkan` をリンクできることで確かめる。

**結果:** (CI またはローカルでビルドが通ったら記入)

## 1. Skia と Qt Quick の統合

**方法**

- `QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan)` で Qt Quick を Vulkan で動かす。
- キャンバスは `QQuickRhiItem`(Qt 6.7 以降)。`QRhi::nativeHandles()` から Qt が作った `VkInstance`、`VkPhysicalDevice`、`VkDevice`、`VkQueue` を取り出し、それで Skia の `GrDirectContext` を作る。VkDevice とキューは Qt と共有し、新しく作らない。
- 毎フレーム、アイテムの描画先テクスチャ(`VkImage`)を Skia の面としてラップして描き、Skia 側で `SHADER_READ_ONLY_OPTIMAL` へ遷移させて提出する。変えたレイアウトは `QRhiTexture::setNativeLayout` で QRhi に伝える。
- 実装: `src/render/vulkan_canvas.*`(Skia 側、Skia の型は外に出さない)、`src/app/canvas_item.*`(Qt 側)。

**懸念点(実機で確かめること)**

- Skia はキューへ独自に提出し、Qt のフレームのコマンドバッファはその後に提出される。提出順とレイアウト遷移のバリアで同期が足りるか。検証レイヤー(Vulkan SDK の `VK_LAYER_KHRONOS_validation`)を有効にして警告が出ないか確かめる。Qt 側は環境変数 `QSG_RHI_DEBUG_LAYER=1` で有効になる。
- Qt が有効にしたデバイス拡張を Skia には伝えていない。必要になったら `VulkanExtensions` に渡す。
- 描画先のフォーマットは `QQuickRhiItem` の既定(RGBA8)を前提にしている。

**成立しない場合**(計画どおり): テクスチャ経由の受け渡し、それも難しければ OpenGL。

**結果:**

## 2. 描画性能(1万パス)

**方法**

- `TestScene` が乱数で1万個の閉じた3次ベジェ(アンカー3〜7個)を格子状に並べる。各パスは塗りと線を持ち、アンチエイリアスあり。画面外の除外やキャッシュはわざと入れていない(素の性能を先に測る)。
- 手動: マウスホイールでズーム、ドラッグでパン。左上に fps と1フレームの描画時間(CPU 側で Skia の記録と提出にかかった時間)を表示する。
- 自動: `leinwand --bench` で10秒間ズームとパンを動かし、平均を標準出力に出して終了する。

**記録すること**: GPU とドライバー、解像度と拡大率、パス数(1万、必要なら5万)、fps、描画時間。fps はディスプレイのリフレッシュレートが上限になる。

**成立しない場合**(計画どおり): 描画結果のタイルキャッシュと画面外オブジェクトの除外を M1 より前に設計する。

**結果:**

## 3. KDDockWidgets

未着手。着手前に確認が必要な点が1つある。

- KDDockWidgets を vcpkg から入れると、vcpkg が自前の Qt をビルドする。計画では Qt は公式インストーラーで入れるため、Qt が2つになる。KDDockWidgets だけは CMake の FetchContent などでソースから取り込むのがよさそう。

**結果:**

## ローカルでのビルド手順(Windows)

必要なもの: Visual Studio 2022 の「C++ によるデスクトップ開発」ワークロード(CMake と Ninja を含む)、Qt 6.8 以降(MSVC 2022 64-bit)、vcpkg、Vulkan SDK(検証レイヤー用。ビルド自体には不要)。

```
set VCPKG_ROOT=C:\path\to\vcpkg
set QT_ROOT_DIR=C:\Qt\6.8.x\msvc2022_64
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

コマンドは「x64 Native Tools Command Prompt for VS 2022」から実行する。実行時は `%QT_ROOT_DIR%\bin` を PATH に入れるか、`windeployqt` で DLL を集める。
