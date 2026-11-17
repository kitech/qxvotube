#!/bin/sh
# qmake 接入验证(Qt3/Qt4=stub,Qt>=5=真实实现):编译并运行 qt3stub,
# 输出 createProviders() 的 provider 数量。
# 用法: ./build.sh [qt6|qt4|qt3]   (默认 qt6)
set -e
cd "$(dirname "$0")"

case "${1:-qt6}" in
qt6) QMAKE=${QMAKE:-/opt/qt/6.7.3/gcc_64/bin/qmake}; DIR=build-qt6 ;;
qt4) QMAKE=${QMAKE:-/opt/qt/4.8.7/gcc_64/bin/qmake}; DIR=build-qt4 ;;
qt3) QMAKE=${QMAKE:-/opt/qt338sh/bin/qmake}; QTDIR=/opt/qt338sh; export QTDIR; DIR=build-qt3 ;;
*) echo "用法: $0 [qt6|qt4|qt3]"; exit 2 ;;
esac

mkdir -p "$DIR"
cd "$DIR"
"$QMAKE" -makefile ../qt3stub.pro
make -j1
./qt3stub
