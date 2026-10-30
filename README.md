# OBS Cursor Filter

Windows 11 の OBS Studio 33 向けの、ゲームキャプチャにカーソルを重ねるエフェクトフィルターです。OBS 本体の変更は不要です。

## 使い方

1. ゲームキャプチャの「カーソルをキャプチャする」をオフにします。
2. ゲームキャプチャの「フィルタ」→「エフェクトフィルタ」に「カーソル描画」を追加します。
3. ゲームキャプチャを「特定のウィンドウをキャプチャ」に設定すると、フィルターはその対象を使用します。必要に応じて「サイズの微調整」を使います。
4. ゲーム上のカーソルが録画・配信の映像でも同じ位置と大きさになるか確認します。

自動対象はゲームキャプチャの設定にあるタイトル・クラス・実行ファイルの完全一致で探します。キャプチャ中のウィンドウそのものを取得する仕組みではないため、タイトルが変化するゲームや一致優先順位の設定によっては対象を見つけられません。全画面自動検出などでは「ゲームキャプチャの対象ウィンドウを使用」をオフにして手動指定してください。手動一覧はフィルタのプロパティを開いたときに更新され、OBS 自身のウィンドウは対象外です。

## 現在の制限

- Windows のカーソルサイズ設定とモニターの DPI を使って描画サイズを推定します。標準カーソルに設定された `.cur` は元ファイルから読み込みます。それ以外やファイルを読み込めない場合は取得画像を拡大するため、特殊なカーソルでは表示が異なる場合や輪郭がぼける場合があります。
- 選択ウィンドウのクライアント領域全体がゲームキャプチャの映像に対応する前提です。描画範囲が異なるゲーム、特殊な全画面モード、DPI や異なる解像度では位置がずれる場合があります。
- ゲーム自身が映像内に描くカーソルを消すことはできません。
- 通常の SDR 映像を対象とした試作です。HDR、ゲーム内カーソル、異なる DPI の複数モニターは未検証です。

## ビルド

Visual Studio 2026 の C++ 開発環境、CMake、OBS Studio 33 のソースと Release ビルド済みの libobs が必要です。

```powershell
cmake -S . -B build -G 'Visual Studio 18 2026' -A x64 -DOBS_SOURCE_DIR=C:/path/to/obs-studio -DOBS_BUILD_DIR=C:/path/to/obs-studio/build_x64
cmake --build build --config Release
cmake --install build --config Release --prefix out
```

## インストール

OBS を終了して、プロジェクトフォルダーで以下を実行します。書き込みを拒否された場合は、PowerShell を管理者として開いて実行してください。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

DLL は `C:\ProgramData\obs-studio\plugins\obs-cursor-filter\obs-cursor-filter.dll` に配置されます。Release ビルドがあればそれを優先し、なければ `out` 内の DLL を使用します。OBS を再起動してフィルターを追加してください。ポータブル版 OBS は ProgramData のプラグインを読み込まないため、ポータブル版のルートにある `plugins` フォルダーを `-PluginRoot` で指定してください。

## ライセンス

GPL-2.0-or-later。`src/cursor-capture.c` と `src/cursor-capture.h` は OBS Studio の `plugins/win-capture` に由来します。詳細は `COPYING` を参照してください。
