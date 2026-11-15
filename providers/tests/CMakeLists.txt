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
