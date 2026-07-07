#!/bin/bash
set -e

# 設定
BUILD_DIR="build"
DIST_DIR="dist"
EXE_NAME="lan-play-gui.exe"
SRC_EXE="build/gui/$EXE_NAME"

echo "=== クリーンアップ ==="
rm -rf $BUILD_DIR $DIST_DIR

echo "=== ビルド開始 (Windowsネイティブ) ==="
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# コンパイラを明示的に指定してCMakeを実行
cmake -G "Unix Makefiles" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_BUILD_TYPE=Release ..

# ビルド実行
make -j$(nproc)

cd ..

echo "=== 配布用パッケージ作成 ==="
if [ -f "$SRC_EXE" ]; then
    mkdir -p $DIST_DIR
    cp $SRC_EXE $DIST_DIR/
    
    # 依存DLLの収集 (MSYS2環境のパスから取得)
    echo "DLLをコピー中..."
    DLL_PATH="/mingw64/bin"
    cp $DLL_PATH/SDL2.dll $DIST_DIR/
    cp $DLL_PATH/libgcc_s_seh-1.dll $DIST_DIR/
    cp $DLL_PATH/libstdc++-6.dll $DIST_DIR/
    cp $DLL_PATH/libwinpthread-1.dll $DIST_DIR/

    # サーバーとクライアント実行ファイルの配置
    echo "アプリケーション本体を配置中..."
    mkdir -p $DIST_DIR/server
    
    # Node.js 依存関係の解決
    if [ -f "server/package.json" ]; then
        echo "サーバーの依存関係をインストール中..."
        (cd server && npm install)
        
        # インストール後にディレクトリが存在するか確認してからコピー
        if [ -d "server/node_modules" ]; then
            echo "node_modules が確認されました。実ファイルのみをコピーします..."
            cp -r server/src $DIST_DIR/server/
            
            # node_modules配下の実ファイルのみをコピーする（リンクは無視）
            mkdir -p $DIST_DIR/server/node_modules
            # リンク以外のファイル/ディレクトリをコピー
            # -not -type l でシンボリックリンクを除外
            find server/node_modules -mindepth 1 -maxdepth 1 -not -type l -exec cp -rL {} $DIST_DIR/server/node_modules/ \;
            
            cp server/package.json $DIST_DIR/server/
        else
            echo "エラー: server/node_modules が作成されませんでした。"
            exit 1
        fi
    else
        echo "警告: server/package.json が見つかりません。サーバー機能が動作しない可能性があります。"
    fi

    mkdir -p $DIST_DIR/bin
    cp build/src/lan-play.exe $DIST_DIR/bin/
    
    echo "=== 完了 ==="
    echo "配布用ファイルは $DIST_DIR フォルダにあります。"
else
    echo "エラー: $SRC_EXE が見つかりません。"
    exit 1
fi
