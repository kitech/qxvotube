#!/bin/bash
# qskvotube 单元测试构建(qlappcom doctest 方式:直编直链直跑)。
# 串行执行,勿与其他 build 并行。
set -e
cd "$(dirname "$0")"

QTDIR=/opt/qt/6.7.3/gcc_64
# libqskinny 的 Qt 依赖解析到 6.7.3(与 buildqt6.sh 同法)
export LD_LIBRARY_PATH="$QTDIR/lib:/opt/qt/qskinny/lib/qskinny${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM=offscreen # skinname 测试不需真实窗口

B=build-tests
mkdir -p "$B"

CXX="g++ -std=c++17 -g -O0 -w"
QTINC="$QTDIR/include"

# ── 1. cxMsToText(纯逻辑,仅 QtCore)──
echo "=== doctest: cxmsutil ==="
$CXX -c test_cxmsutil.cpp -I. -Isrc -I"$QTINC" -I"$QTINC/QtCore" \
    -fPIC -o "$B/t_cxmsutil.o"
$CXX "$B/t_cxmsutil.o" -L"$QTDIR/lib" -lQt6Core -Wl,-rpath,"$QTDIR/lib" \
    -o "$B/t_cxmsutil"
./"$B/t_cxmsutil"

# ── 2. 皮肤名回归守卫(offscreen + qskinny)──
echo "=== doctest: skinname ==="
$CXX -c test_skinname.cpp -I. -I"$QTINC" -I"$QTINC/QtCore" -I"$QTINC/QtGui" \
    -I/opt/qt/qskinny/include/qskinny -fPIC -o "$B/t_skinname.o"
$CXX "$B/t_skinname.o" -L"$QTDIR/lib" -L/opt/qt/qskinny/lib/qskinny \
    -lQt6Gui -lQt6Core -lqskinny \
    -Wl,-rpath,"$QTDIR/lib" -Wl,-rpath,/opt/qt/qskinny/lib/qskinny \
    -o "$B/t_skinname"
./"$B/t_skinname"

echo "=== qskvotube 单测全部通过 ==="
