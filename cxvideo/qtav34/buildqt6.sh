#!/bin/sh
set -x
HERE=`cd $(dirname "$0") && pwd`
cd "$HERE"

mkdir -p build-qt6 && cd build-qt6
QMAKE=${QMAKE:-/opt/qt/6.7.3/gcc_64/bin/qmake}
$QMAKE ../tools/cxvideo_demo/cxvideo_demo.pro
sed -i 's/\-O2/\-O1/g' Makefile
sed -i 's/\-std=c++11/\-std=c++17/g' Makefile
sed -i 's/\-std=gnu++11/\-std=c++17/g' Makefile
if [ x"$1" = x"c" ]; then
    make clean
fi
make
ret=$?

cd "$HERE"
mkdir -p build-qt6-selftest && cd build-qt6-selftest
$QMAKE ../tools/selftest/cxvideo_selftest.pro
sed -i 's/\-O2/\-O1/g' Makefile
sed -i 's/\-std=c++11/\-std=c++17/g' Makefile
sed -i 's/\-std=gnu++11/\-std=c++17/g' Makefile
make
ret2=$?
echo "demo make ret=$ret selftest make ret=$ret2"
if [ x"$ret2" = x"0" ]; then
    ./cxvideo_selftest
fi
test $ret -eq 0 -a $ret2 -eq 0
