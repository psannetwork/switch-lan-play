# switch-lan-play

[English](README.md) | [中文](README_zh.md)

LANで遊ぶ感覚で、遠くの友達とゲームを楽しみましょう。

## プロジェクト概要
このプロジェクトは、Switchのローカル通信（LANプレイ）をインターネット経由で可能にするツールです。

## GUI版について
現在、**Dear ImGui + SDL2** を採用して開発中です。これにより、単一のバイナリ（+依存DLL）で動作するクロスプラットフォームなGUIアプリを目指しています。

---

## ビルド手順

GUIおよびCLIの両方を含むビルド手順です。

### 1. 必要なツールのインストール (Windows / MSYS2)

MSYS2環境（**MinGW 64-bit** ターミナル）で、以下のコマンドを実行して必要なツールとライブラリを全てインストールします。

```bash
pacman -Sy
pacman -S base-devel \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-SDL2 \
    mingw-w64-x86_64-SDL2_image \
    git
```

### 2. ビルドコマンド

用意されたスクリプトを使用すると、自動的にクリーンビルドと配布用パッケージの作成が行われます。

```bash
# プロジェクトルートで実行
bash build_win.sh
```

### 3. 生成物
*   **配布用パッケージ:** `dist/` フォルダの中に、実行ファイル `lan-play-gui.exe` と必要な DLL がまとめられます。
*   この `dist` フォルダ全体を Zip に圧縮すれば、他のPCでも動作するようになります。

---

## 使用方法

### サーバー
Switch LAN Playサーバーを起動します。

```bash
# Dockerを使用する場合
docker run -d -p 11451:11451/udp -p 11451:11451/tcp spacemeowx2/switch-lan-play
```

### クライアント
友達と同じサーバーに接続し、Switch側でIPを静的割り当てに設定してください。PCとSwitchは同じルーター（またはLAN/VPN）に接続されている必要があります。

詳細は [lan-play.com](https://www.lan-play.com/) を参照してください。
