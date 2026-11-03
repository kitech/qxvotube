#include "config.h"
#include "compat34.h"
#include "hjson_wrap.h"
#ifdef QT3_BUILD
#include <qdir.h>
#else
#include <QDir>
#endif
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <map>

static std::string qStrToStd(const QString& s) {
    return std::string(qToUtf8(s).data());
}

static QString configDir() {
#ifdef QT3_BUILD
    return QDir::homeDirPath() + "/.config/qlvotube";
#else
    return QDir::homePath() + "/.config/qlvotube";
#endif
}

static std::string configFilePath() {
    return qStrToStd(configDir()) + "/config.json";
}

static cJSON* loadConfig() {
    std::string path = configFilePath();
    std::ifstream ifs(path);
    if (!ifs.is_open()) { return nullptr; }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return cJSON_Parse(ss.str().c_str());
}

static bool saveConfig(cJSON* root) {
    std::string path = configFilePath();
    std::string dir = path.substr(0, path.rfind('/'));
    qMkdir(qFromUtf8(dir));
    char* jsonStr = cJSON_Print(root);
    std::ofstream ofs(path);
    bool ok = ofs.is_open();
    if (ok) { ofs << jsonStr; }
    free(jsonStr);
    return ok;
}

cJSON* Config::loadRoot() { return loadConfig(); }
bool Config::saveRoot(cJSON* root) { return saveConfig(root); }

static const std::map<std::string, std::string>& defaultValues() {
    static const std::map<std::string, std::string> m = {
        {"uilang", "zh-CN"}
    };
    return m;
}

QString Config::value(const QString& key) {
    auto res = hjsonLoad(configFilePath());
    std::string k = qToUtf8(key).data();
    if (res.isOk()) {
        Hjson::Value item = res.unwrap()[k];
        if (item.defined()) return qFromUtf8(item.to_string());
    }
    auto it = defaultValues().find(k);
    return it != defaultValues().end() ? qFromUtf8(it->second) : QString();
}

bool Config::setValue(const QString& key, const QString& value) {
    auto res = hjsonLoad(configFilePath());
    Hjson::Value root = res.isOk() ? std::move(res.unwrap()) : Hjson::Value(Hjson::Type::Map);
    root[qToUtf8(key).data()] = qToUtf8(value).data();
    return hjsonSave(root, configFilePath()).isOk();
}
