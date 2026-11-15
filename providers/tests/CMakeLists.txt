add_executable(pvsmoke smoke.cpp)
target_link_libraries(pvsmoke PRIVATE qxvproviders)
add_test(NAME providers_smoke COMMAND pvsmoke)

add_executable(ettest errortext.test.cpp)
target_link_libraries(ettest PRIVATE qxvproviders)
add_test(NAME providers_errortext
         COMMAND ettest ${CMAKE_CURRENT_SOURCE_DIR}/../../qlvotube/lang)
