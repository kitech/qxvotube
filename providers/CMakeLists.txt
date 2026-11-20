# providers/ —— 视频源协议库(QtCore + qlappcom curl 引擎 + OpenSSL)
# 顶层构建接入:qskvotube CMakeLists 中 add_subdirectory(providers)
# 单独构建:cmake -B build -DCMAKE_PREFIX_PATH=/opt/qt/6.7.3/gcc_64 && cmake --build build
cmake_minimum_required(VERSION 3.16)
project(qxvproviders C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

add_library(qxvproviders STATIC
    ../qlcomp/cJSON.c                # 全仓唯一份(qlvotube 同源,避免双份符号)
    ../qlcomp/compatcore34.cpp       # qFromUtf8/qbaFromCharptr 等(Qt6 需 Qt6::Gui)
    ../qlappcom/eventpoller.cpp      # curl multi 传输引擎(NetClient 依赖)
    ../qlcomp/md5.c                  # xhs 签名器 MD5(纯 C,qlcomp 复用)
    ../qlcomp/limelog.cpp            # EventPoller 的日志实现(apiLogImpl)
    ../vendor/wjcryptlib/WjCryptLib_Rc4.c  # xhs 签名器 RC4(公有领域)
    common/debuglog.cpp
    common/result.cpp
    common/netclient.cpp
    common/potoken.cpp
    common/cookiejar.cpp
    common/fingerprint.cpp
    common/errortext.cpp
    factory.cpp
    bilibili/bilibiliprovider.cpp
    youtube/youtubeprovider.cpp
    hongguo/hongguoprovider.cpp
    hongguo/hg_parse.cpp
    bilibili/bili_parse.cpp
    douyin/douyinprovider.cpp
    douyin/dy_parse.cpp
    douyin/abogus.cpp                  # a_bogus 纯 C++ 移植(离线单测/主程序共用)
    douyin/acsig.cpp                   # __ac_signature 纯算 + Argus md5
    douyin/abogus_js.cpp               # 双机制对照(qjs 命令行;Qt3/Qt4+ 分支)
    douyin/acsig_js.cpp
    douyin/qjsrun.cpp
    xiaohongshu/xiaohongshuprovider.cpp
    xiaohongshu/ssr.cpp
    xiaohongshu/xhs_parse.cpp
    xiaohongshu/sign.cpp
    xiaohongshu/xhs_api.cpp
    netflix/netflixprovider.cpp
    netflix/netflix_data.cpp
    youtube/yt_parse.cpp
    common/hgcenc/spade.cpp
    common/hgcenc/mp4box.cpp
    common/hgcenc/cenc.cpp
    common/hgapp/hgapp.cpp
    common/ytdlp.cpp
    common/localstream/localstream.cpp
    common/hgstream/hgstream.cpp
)

# 注:不定义 QT_NO_KEYWORDS —— common/ytdlp.h 使用 slots 关键字,
# Qt6 AUTOMOC 会把它当显式/非关键字混用而编译失败(2026-11-18 实测)。
# target_compile_definitions(qxvproviders PRIVATE QT_NO_KEYWORDS)

target_include_directories(qxvproviders PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../qlcomp    # cJSON.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../qlappcom) # eventpoller.h

target_link_libraries(qxvproviders PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets OpenSSL::SSL OpenSSL::Crypto Threads::Threads
    ZLIB::ZLIB CURL::libcurl)

if(QXV_PROVIDERS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
