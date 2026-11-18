#!/bin/sh
# Qt3 验证:用 Qt 3.3.8sh 构建并运行
#   qt3sign    签名器 KAT + 1000 组差分 + cookie 工具
#   qt3xhsapi  web API 请求构造/响应解析(合成 fixture)
# 用法: ./build.sh [clean]
set -e
cd "$(dirname "$0")"

QTDIR=${QTDIR:-/opt/qt338sh}
export QTDIR
QMAKE="$QTDIR/bin/qmake"

if [ "$1" = "clean" ]; then
    rm -rf build build-api build-parse
fi

run() {  # $1=pro $2=builddir $3=bin $4=fixture
    mkdir -p "$2"
    ( cd "$2" && "$QMAKE" -makefile "../$1" && make -j1 )
    echo "--- run $3 ---"
    "./$2/$3" "$4"
}

run qt3sign.pro    build     qt3sign    ../fixtures
run qt3xhsapi.pro  build-api qt3xhsapi ../fixtures/xhs_api
run qt3hgc.pro     build-hgc qt3hgc     ../fixtures/hgc
run qt3parse.pro   build-parse qt3parse ../fixtures
