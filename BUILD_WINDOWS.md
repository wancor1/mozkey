# Mozkey Windows ビルド手順

## 1回だけやる環境準備

必要なもの:

- Visual Studio 2022（「C++ によるデスクトップ開発」ワークロード。Windows 11 SDK / MSVC v143 / C++ ATL を選択）
- Python 3.12 以降（`python` コマンドで使えること）
- .NET 6 以降（`dotnet` コマンド）
- PowerShell 7（`pwsh`）— 辞書準備スクリプトは PS 5.1 だと失敗する。無いなら:

```
winget install Microsoft.PowerShell
```

## 毎回のビルド

リポジトリのルートで一発だけ:

```
pwsh -File build_all.ps1
```

初回もこれ1つでOK。初回は Qt のビルドで1〜2時間かかるが、2回目以降は数分〜十数分。

やっていること:

1. bazelisk.exe が無ければダウンロード（tools\local_bazel に入る）
2. 辞書の準備（外部の merge-ut / nico-pixiv 辞書を取得して profiled 辞書を生成）
3. 依存の更新（update_deps.py、初回は LLVM/MSYS2/Ninja をダウンロード）
4. third_party\qt が無ければ Qt をソースからビルド
5. `bazelisk build package --config release_build`

## 完成品

```
src\bazel-bin\win32\installer\Mozc64.msi
```

インストールは src で:

```
python build_tools\open.py bazel-bin\win32\installer\Mozc64.msi
```

## オプション

- `pwsh -File build_all.ps1 -SkipDictionary`: 辞書の再取得をスキップ（辞書が既に生成済みのとき）
- `pwsh -File build_all.ps1 -SampleProfile`: ビルド後に辞書プロファイルを sample に戻す（commit 前などに推奨）

## ビルドがおかしいとき

```
cd src
bazelisk clean --expunge
```

その後もう一度 `build_all.ps1`。

## 補足

- ARM64 / ユニバーサルインストーラなどの応用は `docs\build_mozc_in_windows.md` を参照
- ネットワークダウンロードを伴うのは Step 1〜4 の初回のみで、以降は差分のみ
