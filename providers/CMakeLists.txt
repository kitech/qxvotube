# providers/ —— 视频源协议库(QtCore + cpp-httplib vendored + OpenSSL)
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
find_package(Qt6 COMPONENTS Core REQUIRED)

# vendored cpp-httplib(gh-proxy 前缀下载;已存在则跳过)
set(HTTPLIB_FILE "${CMAKE_CURRENT_SOURCE_DIR}/common/httplib.h")
if(NOT EXISTS "${HTTPLIB_FILE}")
    file(DOWNLOAD
        "https://gh-proxy.com/https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.20.0/httplib.h"
        "${HTTPLIB_FILE}"
        STATUS _dl)
    if(NOT _dl STREQUAL "0;OK" AND NOT _dl STREQUAL "0;No error")
        message(FATAL_ERROR "download httplib.h failed: ${_dl}")
    endif()
endif()

add_library(qxvproviders STATIC
    ../qlcomp/cJSON.c                # 全仓唯一份(qlvotube 同源,避免双份符号)
    common/debuglog.cpp
    common/result.cpp
    common/netclient.cpp
    common/cookiejar.cpp
    common/fingerprint.cpp
    common/errortext.cpp
    factory.cpp
    bilibili/bilibiliprovider.cpp
    youtube/youtubeprovider.cpp
)

target_compile_definitions(qxvproviders PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
    CPPHTTPLIB_ZLIB_SUPPORT
    QT_NO_KEYWORDS)

target_include_directories(qxvproviders PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../qlcomp)   # cJSON.h

target_link_libraries(qxvproviders PUBLIC
    Qt6::Core OpenSSL::SSL OpenSSL::Crypto Threads::Threads ZLIB::ZLIB)

if(QXV_PROVIDERS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
