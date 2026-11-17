add_executable(pvsmoke smoke.cpp)
target_link_libraries(pvsmoke PRIVATE qxvproviders)
add_test(NAME providers_smoke COMMAND pvsmoke)

add_executable(ettest errortext.test.cpp)
target_link_libraries(ettest PRIVATE qxvproviders)
add_test(NAME providers_errortext
         COMMAND ettest ${CMAKE_CURRENT_SOURCE_DIR}/../../qlvotube/lang)

add_executable(hgcenc hgcenc.test.cpp)
target_link_libraries(hgcenc PRIVATE qxvproviders)
add_test(NAME providers_hgcenc COMMAND hgcenc)

# H3a E2E 需要外网(App 取流),不进 ctest 默认集;手动运行 ./hgapp
add_executable(hgapp hgapp.test.cpp)
target_link_libraries(hgapp PRIVATE qxvproviders)

# H2b 流式代理自检(需外网;含 Range/回收/空闲关/ffprobe)
add_executable(hgstream hgstream.test.cpp)
target_link_libraries(hgstream PRIVATE qxvproviders)
# 测试 TU 直接 include httplib.h:必须与库内同一份编译定义(否则 ODR 冲突 → SSL 断言)
target_compile_definitions(hgstream PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT)

# 搜索分页验收(需外网;手动运行 ./paging)
add_executable(paging paging.test.cpp)
target_link_libraries(paging PRIVATE qxvproviders)

# 清晰度选择验收(需外网;手动运行 ./quality)
add_executable(quality quality.test.cpp)
target_link_libraries(quality PRIVATE qxvproviders)

# homeFeed 验收(需外网;手动运行 ./homefeed)
add_executable(homefeed homefeed.test.cpp)
target_link_libraries(homefeed PRIVATE qxvproviders)

# detail 补全验收(需外网;手动运行 ./detail)
add_executable(detail detail.test.cpp)
target_link_libraries(detail PRIVATE qxvproviders)

# 可靠性三件(重试/cancel/缓存;纯本地,可进 ctest)
add_executable(reliability reliability.test.cpp)
target_link_libraries(reliability PRIVATE qxvproviders)
target_compile_definitions(reliability PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT)
add_test(NAME providers_reliability COMMAND reliability)

# 离线解析单测(fixtures/ 真实响应回放,不联网,进 ctest)
add_executable(parsetest parse.test.cpp)
target_link_libraries(parsetest PRIVATE qxvproviders)
target_compile_definitions(parsetest PRIVATE
    FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME providers_parse COMMAND parsetest)

# localstream 通用性(内存源;离线,进 ctest)
add_executable(localstream localstream.test.cpp)
target_link_libraries(localstream PRIVATE qxvproviders)
target_compile_definitions(localstream PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT)
add_test(NAME providers_localstream COMMAND localstream)

# YouTube 高清晰度(HLS 合流;需外网+代理+yt-dlp+ffmpeg,手动运行)
add_executable(ytdash ytdash.test.cpp)
target_link_libraries(ytdash PRIVATE qxvproviders)

# 小红书免登录链路(需外网;节流均值 6s,手动运行)
add_executable(xhs xhs.test.cpp)
target_link_libraries(xhs PRIVATE qxvproviders)
# 直接 include httplib.h 的测试 TU 必须与库内同一份编译定义(否则 ODR → SSL 断言/异常)
target_compile_definitions(xhs PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT)

# 认证错误模型(离线,进 ctest)
add_executable(autherr autherror.test.cpp)
target_link_libraries(autherr PRIVATE qxvproviders)
add_test(NAME providers_autherror COMMAND autherr)

# xhs 签名器(离线,进 ctest;fixture 目录传 argv[1])
add_executable(xhssign xhssign.test.cpp)
target_link_libraries(xhssign PRIVATE qxvproviders)
add_test(NAME providers_xhssign COMMAND xhssign ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# xhs web API 解析(离线,合成 fixture)
add_executable(xhsapi xhsapi.test.cpp)
target_link_libraries(xhsapi PRIVATE qxvproviders)
add_test(NAME providers_xhsapi COMMAND xhsapi
         ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xhs_api)
