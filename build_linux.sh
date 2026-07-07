#!/bin/bash
set -e
echo "=== クリーンビルド開始 (Linux) ==="
rm -rf build
mkdir build
cd build
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
echo "=== ビルド成功 ==="
echo "GUIアプリ: build/gui/lan-play-gui"
