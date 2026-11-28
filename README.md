# RenderingWorkspace

描画ライブラリ開発班の統合開発環境です。

## 目的

RenderingLibraryの開発、動作検証、性能計測および内部技術資料を一元管理します。

## Submodule構成

| Path | Repository |
| --- | --- |
| SDK/RenderingLibrary | RenderingLibrary |
| Tools/DevelopmentTools | DevelopmentTools |

## 内部管理ディレクトリ（予定）

- Docs/：内部技術資料
- Samples/：開発用サンプル
- Benchmarks/：性能検証
- Tests/：描画テスト
- Assets/：検証用素材

## 管理担当

Rendering-Team

## 開発状況

ATMERA の変更可能なソース、Native、Studio、サンプル、テスト、Visual Studio ソリューションはこのリポジトリで開発します。ルートの `ATMERA.sln` からまとめてビルドできます。`SDK/RenderingLibrary` は配布先リポジトリへの既存サブモジュールであり、ビルドはその内容を必要としません。完成した SDK と配布物だけを RenderingLibrary に公開します。現在の機能と未完了項目は [Roadmap](Docs/ROADMAP.md) を参照してください。

## ATMERA の確認手順

1. `git clone --recurse-submodules https://github.com/FUREQ-DEVELOP/RenderingWorkspace.git` で取得します。
2. Visual Studio 2022 で `ATMERA.sln` を開き、`Debug|x64` でビルドします。コマンドラインでは MSBuild に `/restore /p:Configuration=Debug /p:Platform=x64` を指定します。
3. `./Tools/Test.ps1 -Configuration Debug` を実行します。

既存のチェックアウトでは `git submodule update --init --recursive` で SDK を取得できます。Studio と Triangle の起動設定は `ATMERA.slnLaunch` にあります。

## 利用方法

- 作業は `main` からブランチを作成し、Pull Requestで提案してください。
- タスクはGitHub Issues / Projectsで管理します。
- 変更の責務: RenderingWorkspace。担当Team: `Rendering-Team`。
- 依存関係や開発状況は [DevelopmentDocs](https://github.com/FUREQ-DEVELOP/DevelopmentDocs) を参照してください。
