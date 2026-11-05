#!/bin/sh
set -x
if [ x"$QTDIR" = x"" ]; then
    QTDIR=/opt/qt338sh
    export QTDIR
fi
HERE=`cd $(dirname "$0") && pwd`
cd "$HERE"

mkdir -p build-qt3 && cd build-qt3
/opt/qt338sh/bin/qmake -makefile ../tools/cxvideo_demo/cxvideo_demo.pro
if [ x"$1" = x"c" ]; then
    make clean
fi
make
ret=$?
if [ x"$ret" = x"0" ]; then
    cp -v cxvideo_demo q3cxv 2>/dev/null
fi

cd "$HERE"
mkdir -p build-qt3-selftest && cd build-qt3-selftest
/opt/qt338sh/bin/qmake -makefile ../tools/selftest/cxvideo_selftest.pro
make
ret2=$?
echo "demo make ret=$ret selftest make ret=$ret2"
if [ x"$ret2" = x"0" ]; then
    ./cxvideo_selftest
fi
test $ret -eq 0 -a $ret2 -eq 0
