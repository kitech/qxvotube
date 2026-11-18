#include "queue_store.h"
#include "common/qcjson.h"
#include "cJSON.h"

#include <qdir.h>
#include <qfile.h>
#include <qstring.h>
#include <stdio.h>
#include <string>

using namespace qxv;

namespace {

QString dirPath()
{
#ifdef QT3_BUILD
    return QDir::homeDirPath() + qFromUtf8("/.config/qlvotube");
#else
    return QDir::homePath() + qFromUtf8("/.config/qlvotube");
#endif
}

QString filePath() { return dirPath() + qFromUtf8("/queue.json"); }

bool writeAll(const QString& path, const std::string& data)
{
    FILE* f = fopen(qToUtf8(path).data(), "wb");
    if (f == 0) return false;
    const size_t n = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return n == data.size();
}

std::string readAll(const QString& path)
{
    std::string out;
    FILE* f = fopen(qToUtf8(path).data(), "rb");
    if (f == 0) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

} // namespace

namespace queue_store {

bool save(const QString& provider, const QString& chip, int current,
          const std::vector<VideoCard>& cards)
{
    if (cards.empty()) return false;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "provider", qToUtf8(provider).data());
    cJSON_AddStringToObject(root, "chip", qToUtf8(chip).data());
    cJSON_AddNumberToObject(root, "current", current);
    cJSON* arr = cJSON_AddArrayToObject(root, "items");
    for (size_t i = 0; i < cards.size(); ++i) {
        const VideoCard& c = cards[i];
        if (c.id.isEmpty()) continue;
        cJSON* it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "id", qToUtf8(c.id).data());
        cJSON_AddStringToObject(it, "title", qToUtf8(c.title).data());
        cJSON_AddStringToObject(it, "author", qToUtf8(c.author).data());
        cJSON_AddStringToObject(it, "cover", qToUtf8(c.cover).data());
        cJSON_AddNumberToObject(it, "durationMs", (double)c.durationMs);
        cJSON_AddStringToObject(it, "webUrl", qToUtf8(c.webUrl).data());
        cJSON_AddItemToArray(arr, it);
    }
    char* js = cJSON_PrintUnformatted(root);
    const std::string data = js != 0 ? std::string(js) : std::string();
    cJSON_Delete(root);
    if (js != 0) cJSON_free(js);
    if (data.empty()) return false;
    qMkdir(dirPath());
    return writeAll(filePath(), data);
}

bool load(QString* provider, QString* chip, int* current,
          std::vector<VideoCard>* cards)
{
    if (cards == 0) return false;
    const std::string raw = readAll(filePath());
    if (raw.empty()) return false;
    JRoot root(qbaFromCharptr(raw.c_str()));
    if (!root.ok()) return false;
    if (provider != 0) *provider = jstr(root.p, "provider");
    if (chip != 0) *chip = jstr(root.p, "chip");
    if (current != 0) *current = (int)jint(root.p, "current", -1);
    cards->clear();
    cJSON* arr = jarr(root.p, "items");
    for (int i = 0; i < jsize(arr); ++i) {
        const cJSON* it = jat(arr, i);
        VideoCard c;
        c.id = jstr(it, "id");
        if (c.id.isEmpty()) continue;
        c.title = jstr(it, "title");
        c.author = jstr(it, "author");
        c.cover = jstr(it, "cover");
        c.durationMs = jint(it, "durationMs", 0);
        c.webUrl = jstr(it, "webUrl");
        cards->push_back(c);
    }
    return !cards->empty();
}

void clear()
{
    QFile::remove(filePath());
}

} // namespace queue_store
