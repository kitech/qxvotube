#!/bin/sh
# qskvotube 占位骨架编译（Qt6 + QSkinny，顺序编译不并行）
# 依赖：
#   - Qt6:   /opt/qt/6.8.1/gcc_64（aqt 装的官方版；CI 同源）
#   - QSkinny: /opt/qt/qskinny.6.7（预编译;Qt 6.7.3 配对,不动 master 安装）
# 说明：QSkinny 的导出目标引用旧命名 Qt::Core/Qt::CorePrivate，
#       CMakeLists 里已建 INTERFACE 垫片转发到 Qt6::*。
set -e
set -x
cd "$(dirname "$0")"

# 皮肤插件路径由 app 内 qskSkinManager->setPluginPaths 前置(anystik 同法);
# 不再依赖 QT_PLUGIN_PATH(x64-nosys 插件与本库 ABI 配对存疑)
# libqskinny(master) 的 Qt 依赖须解析到 6.7.3(与 anystik 同法)
export LD_LIBRARY_PATH="/opt/qt/6.7.3/gcc_64/lib:/opt/qt/qskinny/lib/qskinny${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

QTSRC=${QTSRC:-/opt/qt/6.7.3/gcc_64/lib/cmake}
QSKINNY_CMAKE=${QSKINNY_CMAKE:-/opt/qt/qskinny/lib/cmake}

mkdir -p build && cd build
/opt/qt/6.7.3/gcc_64/bin/qt-cmake .. \
    -DQt6_DIR=/opt/qt/6.7.3/gcc_64/lib/cmake/Qt6 \
    -DCMAKE_PREFIX_PATH="$QTSRC;$QSKINNY_CMAKE" \
    -DCMAKE_BUILD_TYPE=Release
# anystik 同款 hotfix:QSkinny 导入目标带入的未版本化 -lQt6* 会被系统
# /usr/lib 的更新版 Qt6 抢先解析,直接抹掉(link.txt 由 cmake 生成后改)
sed -i 's/-lQt6Qml -lQt6Quick -lQt6OpenGL//g' CMakeFiles/qskvotube.dir/link.txt 2>/dev/null || true
if [ x"$1" = x"c" ]; then
    make clean
fi
make
ret=$?
if [ x"$ret" = x"0" ] && [ -f "qskvotube" ]; then
    cp -v qskvotube q6vtube
fi
ls -lh
