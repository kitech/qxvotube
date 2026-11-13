#!/bin/bash
# qskvotube 桌面构建(Qt6 + QSkinny,顺序编译不并行)
# 写法对齐 ../qxmusic/qskmusic/build-x64.sh:
#   QTDIR 探测(Linux gcc_64 → macOS macos)、qt-cmake 直接调、平台分支 hotfix、
#   末尾打印运行命令。差异:
#   - QSkinny 依赖:CMAKE_PREFIX_PATH 需带 /opt/qt/qskinny(可 QSKINNY_CMAKE 覆盖)
#   - 运行时库路径同时前置 qskinny 插件所在目录
#   - 视频播放走 Release(qskmusic 用 Debug+O1;此处保 O2 以免 QA 时间线受性能影响)
set -e
set -x

cd "$(dirname "$0")"

QTDIR=${QTDIR:-/opt/qt/6.7.3/gcc_64}
if [ ! -d "$QTDIR" ]; then
    # macos
    QTDIR=/opt/qt/6.7.3/macos
fi
QSKINNY=${QSKINNY:-/opt/qt/qskinny}

# 皮肤插件/libqskinny 的 Qt 依赖须解析到 QTDIR(与 anystik/qskmusic 同法);
# macOS 只认 DYLD_LIBRARY_PATH(LD_LIBRARY_PATH 无效)
if [ "$(uname)" = "Darwin" ]; then
    export DYLD_LIBRARY_PATH="$QSKINNY/lib/qskinny:$QTDIR/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
else
    export LD_LIBRARY_PATH="$QTDIR/lib:$QSKINNY/lib/qskinny${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

QTSRC=${QTSRC:-$QTDIR/lib/cmake}
QSKINNY_CMAKE=${QSKINNY_CMAKE:-$QSKINNY/lib/cmake}

mkdir -p build
cd build
"$QTDIR/bin/qt-cmake" .. \
    -DQt6_DIR="$QTDIR/lib/cmake/Qt6" \
    -DCMAKE_PREFIX_PATH="$QTSRC;$QSKINNY_CMAKE" \
    -DCMAKE_BUILD_TYPE=Release

# hotfix (Linux only:macOS 无这些未版本化 Qt 库,且 BSD sed -i 语法不同)
if [ "$(uname)" != "Darwin" ]; then
    sed -i 's/-lQt6Qml -lQt6Quick -lQt6OpenGL//g' CMakeFiles/qskvotube.dir/link.txt 2>/dev/null || true
fi

if [ x"$1" = x"c" ]; then
    make clean
fi
make -j1

ret=$?
if [ x"$ret" = "x0" ] && [ -f "qskvotube" ]; then
    cp -v qskvotube q6vtube
fi
ls -lh
if [ "$(uname)" = "Darwin" ]; then
    echo "=== done: DYLD_LIBRARY_PATH=$PWD:$QSKINNY/lib/qskinny:$QTDIR/lib ./build/q6vtube ==="
else
    echo "=== done: LD_LIBRARY_PATH=$QTDIR/lib:$QSKINNY/lib/qskinny ./build/q6vtube ==="
fi
