#ifndef CONFIG_H
#define CONFIG_H

#include <qstring.h>
#include "cJSON.h"

class Config {
public:
    static QString value(const QString& key);
    static bool setValue(const QString& key, const QString& value);
    static cJSON* loadRoot();
    static bool saveRoot(cJSON* root);
};

#endif
