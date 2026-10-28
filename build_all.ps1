param(
    [switch]$SkipDictionary,
    [switch]$SampleProfile
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot)).Path
$Src = Join-Path $RepoRoot "src"
$ToolsDir = Join-Path $RepoRoot "tools\local_bazel"
$Bazelisk = Join-Path $ToolsDir "bazelisk.exe"
$Pwsh = "pwsh"

if (-not (Get-Command $Pwsh -ErrorAction SilentlyContinue)) {
    throw "PowerShell 7 (pwsh) が必要です。`winget install Microsoft.PowerShell` で入れてください。"
}

if (-not (Test-Path $Bazelisk)) {
    Write-Host "=== Step 0: bazelisk のダウンロード ==="
    New-Item -ItemType Directory -Force $ToolsDir | Out-Null
    Invoke-WebRequest -Uri "https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-windows-amd64.exe" -OutFile $Bazelisk
}

if (-not $SkipDictionary) {
    Write-Host "=== Step 1: 辞書準備 ==="
    & $Pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot "tools\dictionary\prepare_daily_dictionary.ps1")
    if ($LASTEXITCODE -ne 0) { throw "辞書準備に失敗しました。" }
}

Push-Location $Src
try {
    Write-Host "=== Step 2: 依存チェック (update_deps.py) ==="
    python build_tools\update_deps.py
    if ($LASTEXITCODE -ne 0) { throw "update_deps.py に失敗しました。" }

    if (-not (Test-Path (Join-Path $Src "third_party\qt\bin"))) {
        Write-Host "=== Step 3: Qt ビルド（初回のみ・長時間） ==="
        python build_tools\build_qt.py --release --confirm_license
        if ($LASTEXITCODE -ne 0) { throw "build_qt.py に失敗しました。" }
    }

    Write-Host "=== Step 4: Bazel ビルド ==="
    & $Bazelisk build package --config release_build
    if ($LASTEXITCODE -ne 0) { throw "Bazel ビルドに失敗しました。" }
}
finally {
    Pop-Location
}

if ($SampleProfile) {
    Write-Host "=== Step 5: プロファイルを sample に戻す ==="
    & $Pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot "tools\dictionary\use_merge_ut_profile.ps1") -Profile sample
}

$Msi = Join-Path $Src "bazel-bin\win32\installer\Mozc64.msi"
Write-Host ""
Write-Host "ビルド完了: $Msi"
Write-Host "インストール: src で python build_tools\open.py bazel-bin\win32\installer\Mozc64.msi"
