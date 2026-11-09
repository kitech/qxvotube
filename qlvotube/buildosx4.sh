#!/bin/sh
set -e
set -x

export PKG_CONFIG_PATH=/opt/vcpkg/installed/x64-osx/lib/pkgconfig
QMAKE_EXTRA=""
if [ x"$1" = x"asan" ]; then
    QMAKE_EXTRA="CONFIG+=asan"
fi

# macports binary qt4 installation
QMAKE=${QMAKE:-/opt/local/libexec/qt4/bin/qmake}
mkdir -p build-osx4 && cd build-osx4
"$QMAKE" -r $QMAKE_EXTRA ../qlvotube.pro
make

# package
mkdir -p qlvotube.app/Contents/Resources/lang
QT_TRANS=$("$QMAKE" -query QT_INSTALL_TRANSLATIONS)
cp -f "$QT_TRANS/qt_zh_CN.qm" "$QT_TRANS/qt_zh_TW.qm" qlvotube.app/Contents/Resources/ 2>/dev/null || true
cp -f ../lang/*.json qlvotube.app/Contents/Resources/lang/ 2>/dev/null || true
tar zcf ../qlvotube-qt4-osx-x64.tar.gz qlvotube.app/
ls -lh ../qlvotube-*.gz
ls -lh qlvotube.app/Contents/MacOS/
