// qskvotube 单测:cxMsToText 时间格式化(qlappcom doctest 方式)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "src/cxmsutil.h"

TEST_CASE("cxMsToText - 基础格式") {
    SUBCASE("零值") {
        CHECK(cxMsToText(0) == QString("0:00"));
    }
    SUBCASE("秒内补零") {
        CHECK(cxMsToText(500) == QString("0:00"));   // 截断到秒
        CHECK(cxMsToText(1000) == QString("0:01"));
        CHECK(cxMsToText(9000) == QString("0:09"));
    }
    SUBCASE("十位秒") {
        CHECK(cxMsToText(30000) == QString("0:30"));
        CHECK(cxMsToText(59000) == QString("0:59"));
    }
    SUBCASE("分进位") {
        CHECK(cxMsToText(60000) == QString("1:00"));
        CHECK(cxMsToText(61000) == QString("1:01"));
        CHECK(cxMsToText(125000) == QString("2:05"));
    }
    SUBCASE("长时长(函数按总分钟,无小时进位)") {
        CHECK(cxMsToText(3600000L * 11 + 66000) == QString("661:06"));
    }
    SUBCASE("负值(不该出现,只断言不崩)") {
        QString v = cxMsToText(-1);
        CHECK(!v.isEmpty());
    }
}
