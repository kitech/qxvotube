#!/bin/sh
# qskvotube 桌面编译（Qt6 + QSkinny，顺序编译不并行）
# 依赖：
#   - Qt6:   Linux /opt/qt/6.7.3/gcc_64；macOS /opt/qt/6.7.3/macos
#            （可用 QTDIR 覆盖；探测顺序同 ../anystik/anystik/build-x64.sh）
#   - QSkinny: /opt/qt/qskinny（预编译;Qt 6.7.3 配对,不动 master 安装;
#            可用 QSKINNY_CMAKE 覆盖）
# 说明：QSkinny 的导出目标引用旧命名 Qt::Core/Qt::CorePrivate，
#       CMakeLists 里已建 INTERFACE 垫片转发到 Qt6::*。
set -e
set -x
cd "$(dirname "$0")"

# Qt 定位(anystik/build-x64.sh 同法):Linux gcc_64 → macOS macos
QTDIR=${QTDIR:-/opt/qt/6.7.3/gcc_64}
[ -d "$QTDIR" ] || QTDIR=/opt/qt/6.7.3/macos

# 皮肤插件路径由 app 内 qskSkinManager->setPluginPaths 前置(anystik 同法);
# 不再依赖 QT_PLUGIN_PATH(x64-nosys 插件与本库 ABI 配对存疑)
# libqskinny(master) 的 Qt 依赖须解析到 6.7.3(与 anystik 同法);
# macOS 只认 DYLD_LIBRARY_PATH(LD_LIBRARY_PATH 无效)
if [ "$(uname)" = "Darwin" ]; then
    export DYLD_LIBRARY_PATH="$QTDIR/lib:/opt/qt/qskinny/lib/qskinny${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
else
    export LD_LIBRARY_PATH="$QTDIR/lib:/opt/qt/qskinny/lib/qskinny${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

QTSRC=${QTSRC:-$QTDIR/lib/cmake}
QSKINNY_CMAKE=${QSKINNY_CMAKE:-/opt/qt/qskinny/lib/cmake}

mkdir -p build && cd build
"$QTDIR/bin/qt-cmake" .. \
    -DQt6_DIR="$QTDIR/lib/cmake/Qt6" \
    -DCMAKE_PREFIX_PATH="$QTSRC;$QSKINNY_CMAKE" \
    -DCMAKE_BUILD_TYPE=Release
# anystik 同款 hotfix:QSkinny 导入目标带入的未版本化 -lQt6* 会被系统
# /usr/lib 的更新版 Qt6 抢先解析,直接抹掉(link.txt 由 cmake 生成后改)
# 仅 Linux:BSD sed -i 需后缀参数,macOS 也无这些未版本化库
if [ "$(uname)" != "Darwin" ]; then
    sed -i 's/-lQt6Qml -lQt6Quick -lQt6OpenGL//g' CMakeFiles/qskvotube.dir/link.txt 2>/dev/null || true
fi
if [ x"$1" = x"c" ]; then
    make clean
fi
make
ret=$?
if [ x"$ret" = "x0" ] && [ -f "qskvotube" ]; then
    cp -v qskvotube q6vtube
fi
ls -lh
if [ "$(uname)" = "Darwin" ]; then
    echo "=== 运行: DYLD_LIBRARY_PATH=$PWD:/opt/qt/qskinny/lib/qskinny:$QTDIR/lib ./build/q6vtube ==="
else
    echo "=== 运行: LD_LIBRARY_PATH=$QTDIR/lib:/opt/qt/qskinny/lib/qskinny ./build/q6vtube ==="
fi
