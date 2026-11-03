#!/bin/sh
set -x
mkdir -p build-qt6 && cd build-qt6
QMAKE_EXTRA=""
if [ x"$1" = x"asan" ]; then
    QMAKE_EXTRA="CONFIG+=asan"
fi
/opt/qt/6.7.3/gcc_64/bin/qmake $QMAKE_EXTRA ../qlvotube.pro
sed -i 's/\-O2/\-O1/g' Makefile
sed -i 's/\-std=c++11/\-std=c++17/g' Makefile
sed -i 's/\-std=gnu++11/\-std=c++17/g' Makefile
if [ x"$1" = x"c" ]; then
    make clean
fi
make
ret=$?
if [ x"$ret" = x"0" ] && [ -f "qlvotube" ]; then
    cp -v qlvotube q6votube
fi
ls -lh | grep votube || true
