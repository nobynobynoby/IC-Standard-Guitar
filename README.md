# IC Standard Guitar

インスタコード（InstaChord）を USB MIDI で Mac / Logic Pro に接続してリアルタイム演奏するために最適化された、オープンソースの Audio Unit (`.component`) および Standalone アプリケーションです（Apple Silicon / Intel Universal Binary 対応）。

**Windows 版**（VST3 / Standalone、ASIO 対応）もあります。手順は「[Windows 版](#windows-版)」を参照してください。

Unreal Instruments の **Standard Guitar**（各自で取得したローカルの FLAC サンプル）を `sforzando` を介さず直接読み込み、インスタコード特有のレイテンシー（発音の遅れ）やキーオフ時の唐突な音切れを解消します。

> **免責・非公式表記**: 本ソフトウェアは個人開発による非公式プラグインであり、インスタコード（InstaChord / 株式会社KANTAN）および Unreal Instruments とは一切関係ありません。また、Standard Guitar の音声サンプルデータ（`.flac` 等）は本リポジトリおよび配布バイナリに一切同梱していません。

> **Fork 版について**: 本リポジトリは [AraJiru40/IC-Standard-Guitar](https://github.com/AraJiru40/IC-Standard-Guitar)（v1.0.0）を Fork し、Windows 対応を加えた改変版です。元作者による公式版ではありません。Mac 版については元リポジトリも参照してください。変更点は README 末尾の「[改変内容](#改変内容)」にまとめています。

## 主な機能

- **自動プリ・アタック除去 (`Attack Tightness`)**
  - 各サンプル先頭に含まれるピッキング前の無音・立ち上がり遅延を解析してトリミングし、短時間フェードインによりクリックノイズなしで即座に発音します。
- **キーオフ・ダンパー物理 (`Key-Off Release` / `Damper Softness`)**
  - `Note Off` 時に一律で音を切る代わりに、指数減衰＋ボイスごとの 1次ローパス・ダンパーフィルターにより、弦を手で押さえたときのように高域から丸く自然に減衰させます。
- **和音リリースノイズ間引き (`Release Noise`)**
  - コード解放時に全弦分のリリースノイズが同時に鳴るのを防ぐため、80ms のクールダウンで和音あたり最大 1 回のみ控えめに鳴らします。
- **自動ストローク方向検出 (`Auto Strum`)**
  - 85ms 以内の連続ノートのピッチ進行からダウン／アップストロークを自動判定し、`Sus_Down` / `Sus_Up`（または `Mute_Down` / `Mute_Up`）を切り替えます。
- **同音クロスフェード**
  - 同じ音程が連打された際、既存のボイスを 8ms でクロスフェード・アウトさせ、途切れや不自然なクリックを防ぎます。

---

## インストール手順（配布 ZIP から使う場合）

1. **Standard Guitar 本体の準備**
   - Unreal Instruments 公式から `UI_Standard_Guitar` をダウンロードし、解凍したフォルダを `~/Music/Logic/UI_Standard_Guitar`（または `~/Music/UI_Standard_Guitar` / `~/Downloads/UI_Standard_Guitar` 等）に配置します。
2. **プラグインの配置**
   - GitHub Releases から `IC-Standard-Guitar-v1.0.0-macOS-Universal.zip` をダウンロードして解凍します。
   - `IC Standard Guitar.component` を以下のフォルダへコピーします：
     - `~/Library/Audio/Plug-Ins/Components/`（または `/Library/Audio/Plug-Ins/Components/`）
3. **macOS Gatekeeper の隔離属性を解除（初回のみ）**
   - 本プラグインは Apple Developer ID による公証を行っていないため、そのまま起動すると「壊れているため開けません」「開発元を検証できません」と表示されます。配置後にターミナルで以下のコマンドを実行してください：
   ```bash
   xattr -cr ~/Library/Audio/Plug-Ins/Components/"IC Standard Guitar.component"
   ```
   - Standalone アプリ（`IC Standard Guitar.app`）を使う場合は同様に以下を実行してください：
   ```bash
   xattr -cr "IC Standard Guitar.app"
   ```
4. **Logic Pro での選択とサンプル指定**
   - Logic Pro を再起動し、ソフトウェア音源トラックの音源スロットから **AU 音源 ＞ InstaChordTools ＞ IC Standard Guitar** を選択します。
   - もしサンプルが自動検出されない場合は、プラグイン画面右上の **「フォルダ変更...」** から `UI_Standard_Guitar/Samples`（または `UI_Standard_Guitar`）フォルダを選択してください。一度設定すると `~/Library/Application Support/ICStandardGuitar/settings.xml` に保存され、次回以降も自動的に読み込まれます。

---

## Windows 版

Windows 10/11 (x64) 用の **VST3** プラグインと **Standalone アプリ**（ASIO / WASAPI 対応）です。機能は Mac 版と同じです。

### インストール手順（配布 ZIP から使う場合）

1. **Standard Guitar 本体の準備**
   - `UI_Standard_Guitar` を解凍し、`ミュージック\UI_Standard_Guitar`（`%USERPROFILE%\Music\UI_Standard_Guitar`）に置きます。次の場所も自動で探します：`ドキュメント` / `ダウンロード` / `デスクトップ` 直下の `UI_Standard_Guitar`、`C:\UI_Standard_Guitar`。
   - 自動で見つからない場合は、プラグイン画面右上の **「フォルダ変更...」** から指定してください。設定は `%APPDATA%\ICStandardGuitar\settings.xml` に保存されます。
2. **VST3 の配置（DAW で使う場合）**
   - GitHub Releases から `IC-Standard-Guitar-v1.0.0-Windows-x64.zip` をダウンロードして解凍します。
   - `IC Standard Guitar.vst3` フォルダを丸ごと `C:\Program Files\Common Files\VST3\` にコピーします（管理者権限が必要です）。
   - DAW でプラグインを再スキャンし、**InstaChordTools ＞ IC Standard Guitar** を選択します。
3. **Standalone アプリ（DAW なしで使う場合）**
   - `IC Standard Guitar.exe` を好きな場所に置いて起動します。インストールは不要です。
   - コード署名をしていないため、初回は SmartScreen の警告（「Windows によって PC が保護されました」）が出ることがあります。「詳細情報」→「実行」で起動できます。
   - 画面左上の **Options** → **Audio/MIDI Settings** で、オーディオデバイス（低遅延なら **ASIO**）と MIDI 入力（**InstaChord**）を選びます。
   - 設定はウィンドウを **×** で閉じたときに `%APPDATA%\IC Standard Guitar\IC Standard Guitar.settings` に保存されます。タスクマネージャーなどで強制終了すると保存されません。

> **注意**: Windows の MIDI デバイスは同時に 1 つのアプリしか開けません。Standalone と DAW で同時にインスタコードを使うことはできません。

### ソースからのビルド（開発者向け）

- Visual Studio 2022（Build Tools 可）の「C++ によるデスクトップ開発」、CMake 3.22+、JUCE 8

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR=C:/path/to/JUCE
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

成果物は `build\ICStandardGuitar_artefacts\Release\` の `VST3\` と `Standalone\` に出力されます。Windows では VST3 の自動インストールはしません（管理者権限が必要なため）。MSVC ランタイムは静的リンクしているので、利用者側に VC++ 再頒布可能パッケージは不要です。

配布用 ZIP アーカイブを作成する場合：

```powershell
pwsh Scripts/package-release.ps1
```

`dist\IC-Standard-Guitar-v1.0.0-Windows-x64.zip` が生成されます（VST3・Standalone・README・LICENSE を同梱）。

---

## ソースからのビルド手順（開発者向け）

### 必要環境

- macOS 12.0+（Universal Binary: `arm64` + `x86_64`）
- CMake 3.22+ / Xcode Command Line Tools
- ローカル JUCE 8（リポジトリ内の `./JUCE`、`$HOME/JUCE`、または `-DJUCE_DIR=/path/to/JUCE` で指定）

### ビルド・パッケージング

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release -j8
```

ビルドが成功すると、`IC Standard Guitar.component` が自動的に `~/Library/Audio/Plug-Ins/Components/` にインストールされます（`COPY_PLUGIN_AFTER_BUILD TRUE`）。

配布用 ZIP アーカイブを作成する場合：

```bash
./Scripts/package-release.sh
```

`dist/IC-Standard-Guitar-v1.0.0-macOS-Universal.zip` が生成されます。

### テスト・検証

```bash
ctest --test-dir build --output-on-failure
auval -v aumu Icsg Icht
```

## ディレクトリ構成

- `CMakeLists.txt` — JUCE Universal Binary ビルド・テスト構成
- `Source/SampleBank.{h,cpp}` — FLAC 非同期ローダー、音名/RR解析、オンセット検出、パス設定保存
- `Source/GuitarSynthEngine.{h,cpp}` — 24音ポリフォニックエンジン、ダンパー減衰、Tone EQ、空間リバーブ
- `Source/PluginProcessor.{h,cpp}` — AU / Standalone プロセッサ、APVTS パラメータ、状態保存、試奏トリガー
- `Source/PluginEditor.{h,cpp}` — 日本語対応 UI、ステータス表示、ワンクリック試奏ボタン
- `Tests/EngineVerificationTest.cpp` — C++ エンジン検証テスト
- `Scripts/package-release.sh` — 配布用 ZIP パッケージ生成スクリプト（macOS）
- `Scripts/package-release.ps1` — 配布用 ZIP パッケージ生成スクリプト（Windows）
- `LICENSE` — GNU Affero General Public License v3.0 (AGPL-3.0)

## ライセンス

本プロジェクトは **GNU Affero General Public License v3.0 (AGPL-3.0)** のもとで公開されています（詳細は [LICENSE](LICENSE) を参照）。

### 改変内容

AGPL-3.0 第5条に基づき、元プロジェクト（[AraJiru40/IC-Standard-Guitar](https://github.com/AraJiru40/IC-Standard-Guitar) v1.0.0）からの改変内容を示します。詳細な差分は git の履歴を参照してください。

- **2026-11-25** Windows 対応
  - `CMakeLists.txt` — Windows で VST3 / Standalone をビルド、MSVC 用の `/utf-8` 指定、MSVC ランタイムの静的リンク、ASIO 対応（`JUCE_ASIO=1`）
  - `Source/PluginEditor.cpp` — Windows 用の日本語フォント（Yu Gothic UI）
  - `Source/SampleBank.cpp` — Windows 用の設定ファイルの保存先とサンプルフォルダの自動検出候補
  - `Scripts/package-release.ps1` — Windows 用の配布 ZIP 作成スクリプト（新規）
  - `README.md` — Windows 版の手順、Fork 版の表記、第三者コンポーネント、本節を追加

### 第三者コンポーネント

- [JUCE](https://juce.com/)（AGPLv3 で利用）
- Windows 版の ASIO 対応には、JUCE 同梱の Steinberg ASIO SDK ヘッダーを使っています（GPLv3 で利用）。ASIO は Steinberg Media Technologies GmbH の商標です。
