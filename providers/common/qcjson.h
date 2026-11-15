#pragma once
// cJSON(来自 qlcomp,全仓唯一份,Qt3 兼容已验证)的 QString 薄封装。
// 用法:JRoot root(body); checkApi 后用 jstr/jint/jobj/jarr 取值。
// 禁用 QJson(Qt3 无此 API);所有解析一律走这里。
// Qt3 头名小写口径同 qlcomp/compatcore34.h(QT3_BUILD 宏由引用方 .pro 定义)
#ifdef QT3_BUILD
#include <qstring.h>   // 含 QByteArray(qcstring)
#else
#include <QString>
#include <QByteArray>
#endif
#include "cJSON.h"

namespace qxv {

// RAII root:cJSON_Parse 出的对象树,析构统一 cJSON_Delete
class JRoot {
public:
    JRoot() : p(nullptr) {}
    explicit JRoot(const QByteArray& body) : p(cJSON_ParseWithLength(
        body.data(), static_cast<size_t>(body.size()))) {}
    ~JRoot() { if (p) cJSON_Delete(p); }
    void parse(const QByteArray& body) {
        if (p) cJSON_Delete(p);
        p = cJSON_ParseWithLength(body.data(),
                                  static_cast<size_t>(body.size()));
    }
    bool ok() const { return p != nullptr; }
    cJSON* p;
private:
    JRoot(const JRoot&);            // 禁拷贝(独占所有权)
    JRoot& operator=(const JRoot&);
};

// 对象/数组字段访问(容错:类型不符返回 null/缺省)
inline cJSON* jobj(const cJSON* o, const char* key) {
    cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : nullptr;
    return cJSON_IsObject(it) ? it : nullptr;
}
inline cJSON* jarr(const cJSON* o, const char* key) {
    cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : nullptr;
    return cJSON_IsArray(it) ? it : nullptr;
}
inline QString jstr(const cJSON* o, const char* key) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : nullptr;
    return cJSON_IsString(it) && it->valuestring
        ? QString::fromUtf8(it->valuestring) : QString();
}
inline long long jint(const cJSON* o, const char* key, long long def = 0) {
    const cJSON* it = o ? cJSON_GetObjectItemCaseSensitive(o, key) : nullptr;
    return cJSON_IsNumber(it) ? static_cast<long long>(it->valuedouble) : def;
}
inline int jsize(const cJSON* a) {
    return a ? cJSON_GetArraySize(const_cast<cJSON*>(a)) : 0;
}
inline cJSON* jat(const cJSON* a, int i) {
    return a ? cJSON_GetArrayItem(const_cast<cJSON*>(a), i) : nullptr;
}

} // namespace qxv
