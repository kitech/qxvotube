#!/bin/bash
# qskvotube Android 构建(移植自 ../qxmusic/qskmusic/build-android.sh):
# 同 env/同 qt-cmake+androiddeployqt aux-mode 打包流程;差异:
#   - 无 libncm/go extras/i18n lrelease/version_config 段(qskvotube 不用)
#   - ffmpeg 改为 arm64 预编译库 fail-fast(eng_ffmpeg 链接依赖;
#     libmpv 为 dlopen 运行期加载,缺失只影响 mpv 引擎可用性)
#
# ── ffmpeg arm64 预编译库(vendor/android-ffmpeg/arm64-v8a)──
# 来源:husen-hn/ffmpeg-android-binary 的 release tag **v2.0.0**,
#      该 tag 内含的是 **FFmpeg 7.1**(libavformat 61.7.100,非仓库版本号);
#      NDK r26b 构建,min API 21,aarch64,SO 无版本后缀(Android 友好)。
#      头文件取自 ffmpeg.org 的 ffmpeg-7.1.tar.bz2(与 so 同版本)。
# 重取方法(慢/失败时加 https://gh-proxy.com/ 前缀):
#   curl -L "https://gh-proxy.com/https://github.com/husen-hn/ffmpeg-android-binary/releases/download/v2.0.0/arm64-v8a.zip"
#   解包 zip 内 arm64-v8a/lib/*.so → vendor/android-ffmpeg/arm64-v8a/lib/
#   tar -xjf ffmpeg-7.1.tar.bz2 后拷 libav*/libsw*/*.h → include/<lib名>/
# 详见 vendor/vendorinfos.md。
#
# ── libmpv arm64(vendor/android-libmpv/libmpv.so)──
# 来源:media-kit/libmpv-android-video-build release tag v1.1.11
# (asset default-arm64-v8a.jar 解出);自包含(NEDED 仅系统库,ffmpeg 内嵌),
# eng_mpv dlopen 即用。重取命令见 vendor/vendorinfos.md。
set -e
export JAVA_HOME=/opt/jdk-17.0.13+11
export ANDROID_SDK_ROOT=/opt/android-sdk
export ANDROID_NDK_ROOT=/opt/android-ndk-r26b

APP_VERSION_CODE=$(git rev-list --count HEAD)
if [ -z "$APP_VERSION_CODE" ]; then APP_VERSION_CODE=1; fi
echo "[build-android.sh] versionCode: ${APP_VERSION_CODE}"

QT_ANDROID=/opt/qt/6.7.3/android_arm64_v8a
QSK_ANDROID=/opt/qt/qskinny-arm64
QT_HOST=/opt/qt/6.7.3/gcc_64

BUILD_DIR=build-android
# rm -rf "$BUILD_DIR"  # 保留 cmake 缓存以支持增量编译

# ── ffmpeg arm64 预编译库(eng_ffmpeg 链接依赖)──
# 目录布局:<dir>/lib/libav*.so + <dir>/include/libav*/*.h
# 缺省 ../vendor/android-ffmpeg/arm64-v8a,可用 CXV_FFMPEG_ARM64_DIR 覆盖;
# CMake 端经 env 读同一路径建 IMPORTED 目标。
FFDIR=${CXV_FFMPEG_ARM64_DIR:-$(cd .. && pwd)/vendor/android-ffmpeg/arm64-v8a}
for L in libavformat libavcodec libavutil libswscale libswresample; do
    # L 已含 lib 前缀(如 libavformat),勿再拼 lib,否则查成 liblibavformat.so
    if ! ls "$FFDIR/lib/${L}.so" >/dev/null 2>&1; then
        echo "FATAL: ffmpeg arm64 预编译缺 $L: $FFDIR/lib/" >&2
        ls -la "$FFDIR/lib/" 2>&1 | head -10 >&2   # 失败时打出实况,便于定位
        echo "  备好后重跑,或 CXV_FFMPEG_ARM64_DIR=<dir> 指向它" >&2
        exit 1
    fi
done
export CXV_FFMPEG_ARM64_DIR="$FFDIR"

mkdir -p "$BUILD_DIR"

# 1. cmake configure via qt-cmake(qskmusic 同参)
"$QT_ANDROID/bin/qt-cmake" -S . -B "$BUILD_DIR" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_PREFIX_PATH="$QSK_ANDROID" \
    -DQSkinny_DIR="$QSK_ANDROID/lib/cmake/QSkinny" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 2. build native .so only (not apk target)
cmake --build "$BUILD_DIR" -j1 --target qskvotube

# 3. prepare android-build dir (copy .so + Qt deps, no Gradle)
APK_DIR="$BUILD_DIR/android-build"
LIB_DIR="$APK_DIR/libs/arm64-v8a"
mkdir -p "$LIB_DIR"
cp "$BUILD_DIR/libqskvotube_arm64-v8a.so" "$LIB_DIR/"

# ffmpeg arm64 库平铺进 JNI lib 根目录(Android 无子目录)
cp "$FFDIR"/lib/*.so "$LIB_DIR/"

# libmpv arm64 可选:eng_mpv dlopen "libmpv.so";缺失仅 mpv 引擎不可用
MPV_SO=${CXV_MPV_ARM64_SO:-$(cd .. && pwd)/vendor/android-libmpv/libmpv.so}
if [ -f "$MPV_SO" ]; then
    cp "$MPV_SO" "$LIB_DIR/"
else
    echo "[build-android.sh] 提示: 无 $MPV_SO,APK 内 mpv 引擎不可用(ffmpeg 引擎可用)"
fi

# 平铺复制 qskinny 皮肤插件到 JNI lib 根目录(qskmusic 同)
cp "$QSK_ANDROID/lib/qskinny/plugins/skins/"*.so "$LIB_DIR/"
cp "$QSK_ANDROID/lib/qskinny/plugins/platforminputcontexts/"*.so "$LIB_DIR/"

"$QT_HOST/bin/androiddeployqt" \
    --input "$BUILD_DIR/android-qskvotube-deployment-settings.json" \
    --output "$APK_DIR" \
    --deployment bundled \
    --builddir "$BUILD_DIR" \
    --aux-mode

# 复制应用图标到 Android res
mkdir -p "$APK_DIR/res/drawable"
if [ -f app_icon.png ]; then
    cp app_icon.png "$APK_DIR/res/drawable/ic_launcher.png"
else
    cp ../../qxmusic/qskmusic/app_icon.png "$APK_DIR/res/drawable/ic_launcher.png"
fi

# 覆盖 manifest(qskmusic 同:Qt 模板缺 lib_name/icon)
cp android/AndroidManifest.xml "$APK_DIR/AndroidManifest.xml"

# 移除 renderscript.srcDirs 配置(build-tools 34+ 不再支持)
sed -i '/renderscript\.srcDirs/d' "$APK_DIR/build.gradle"

# 注入版本号(defaultConfig 覆盖 Manifest 默认值;先删再追加保证幂等)
sed -i '/^        versionCode /d; /^        versionName /d' "$APK_DIR/build.gradle"
sed -i "/defaultConfig {/a\\        versionCode ${APP_VERSION_CODE}\n        versionName \"0.1\"" "$APK_DIR/build.gradle"

# Qt 模板默认 -Xmx386m 过小;Properties 后值覆盖前值(qskmusic 同)
cat >> "$APK_DIR/gradle.properties" <<PROPS
org.gradle.jvmargs=-Xmx4096m -XX:MaxMetaspaceSize=1024m -Dfile.encoding=UTF-8
androidBuildToolsVersion=34.0.0
androidCompileSdkVersion=android-33
androidNdkVersion=26.1.10909125
androidPackageName=io.fedlet.qskvotube
buildDir=build
qtAndroidDir=${QT_ANDROID}/src/android/java
qtMinSdkVersion=23
qtTargetAbiList=arm64-v8a
qtTargetSdkVersion=34
PROPS

# 4. build debug APK (auto-signed with Android debug key)
GRADLE_OPTS="-Xmx2048m" "$APK_DIR/gradlew" --no-daemon --max-workers=2 \
    -p "$APK_DIR" assembleDebug

# 5. verify output
APK=$(find "$APK_DIR/build/outputs" -name "*.apk" 2>/dev/null | head -1)
if [ -n "$APK" ]; then
    echo "=== APK built: $APK ==="
    ls -lh "$APK"
fi

find build-android -name "*qskvotube*.so" | xargs ls -lh
