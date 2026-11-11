#!/bin/sh
# qskvotube 占位骨架编译（Qt6 + QSkinny，顺序编译不并行）
# 依赖：
#   - Qt6:   /opt/qt/6.8.1/gcc_64（aqt 装的官方版；CI 同源）
#   - QSkinny: /opt/qt/qskinny（预编译；本地）
# 说明：QSkinny 的导出目标引用旧命名 Qt::Core/Qt::CorePrivate，
#       CMakeLists 里已建 INTERFACE 垫片转发到 Qt6::*。
set -e
set -x
cd "$(dirname "$0")"

# QSkinny skin 插件路径(预编译装;setSkin 需要)
export QT_PLUGIN_PATH="${QT_PLUGIN_PATH:+$QT_PLUGIN_PATH:}/mnt/sda5/osopt/qt/qskinny-x64-nosys/lib/qskinny/plugins"
# libqskinny(master) 的 Qt 依赖须解析到 6.7.3(与 qskmusic 同法)
export LD_LIBRARY_PATH="/opt/qt/6.7.3/gcc_64/lib:/mnt/sda5/osopt/qt/qskinny-x64-nosys/lib/qskinny${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

QTSRC=${QTSRC:-/opt/qt/6.7.3/gcc_64/lib/cmake}
QSKINNY_CMAKE=${QSKINNY_CMAKE:-/opt/qt/qskinny/lib/cmake}

mkdir -p build && cd build
cmake -B . -S .. \
    -DCMAKE_PREFIX_PATH="$QTSRC;$QSKINNY_CMAKE" \
    -DCMAKE_BUILD_TYPE=Release
if [ x"$1" = x"c" ]; then
    make clean
fi
make
ret=$?
if [ x"$ret" = x"0" ] && [ -f "qskvotube" ]; then
    cp -v qskvotube q6vtube
fi
ls -lh
