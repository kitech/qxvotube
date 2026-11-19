#!/bin/sh
set -e
set -x

export PKG_CONFIG_PATH=/opt/vcpkg/installed/x64-osx/lib/pkgconfig
QMAKE_EXTRA=""
if [ x"$1" = x"asan" ]; then
    QMAKE_EXTRA="CONFIG+=asan"
fi

# script must copy as buildosx{3,4,5,6}.sh
exename=$(basename $0)
qtnum=${exename: -4: 1}

if [ x"$qtnum" == x"4" ]; then
	# macports binary qt4 installation
	QMAKE=${QMAKE:-/opt/local/libexec/qt4/bin/qmake}
elif [ x"$qtnum" == x"6" ]; then
	QMAKE=${QMAKE:-/opt/qt/6.7.3/macos/bin/qmake}
else
	QMAKE=${QMAKE:-/opt/qt/5.15.2/clang_64/bin/qmake}
	qtnum=5
fi
builddir="build-osx${qtnum}"
mkdir -p "$builddir" && cd "$builddir"
"$QMAKE" -r $QMAKE_EXTRA ../qlvotube.pro
make

# package
mkdir -p qlvotube.app/Contents/Resources/lang
QT_TRANS=$("$QMAKE" -query QT_INSTALL_TRANSLATIONS)
cp -f "$QT_TRANS/qt_zh_CN.qm" "$QT_TRANS/qt_zh_TW.qm" qlvotube.app/Contents/Resources/ 2>/dev/null || true
cp -f ../lang/*.json qlvotube.app/Contents/Resources/lang/ 2>/dev/null || true
# imageformats 运行时插件（Qt4 jpeg/gif/tiff 不内置；缺 libqjpeg → 所有 jpg 封面解码失败）
# ★ 这就是 mac 上 qlmusic 正常 / qlvotube 封面全挂的打包层根因
QT_PLUGDIR=$("$QMAKE" -query QT_INSTALL_PLUGINS)
if [ -d "$QT_PLUGDIR/imageformats" ]; then
    mkdir -p qlvotube.app/Contents/PlugIns/imageformats
    cp -f "$QT_PLUGDIR/imageformats/"* qlvotube.app/Contents/PlugIns/imageformats/ 2>/dev/null || true
    # 插件自身的 Qt 依赖改指 bundle 内 Frameworks（Qt4 插件常带 @rpath 前缀）
    for p in qlvotube.app/Contents/PlugIns/imageformats/*; do
        install_name_tool -change @rpath/QtCore.framework/Versions/4/QtCore \
            @executable_path/../Frameworks/QtCore.framework/Versions/4/QtCore "$p" 2>/dev/null || true
        install_name_tool -change @rpath/QtGui.framework/Versions/4/QtGui \
            @executable_path/../Frameworks/QtGui.framework/Versions/4/QtGui "$p" 2>/dev/null || true
    done
fi
tar zcf ../qlvotube-qt4-osx-x64.tar.gz qlvotube.app/
ls -lh ../qlvotube-*.gz
ls -lh qlvotube.app/Contents/MacOS/
