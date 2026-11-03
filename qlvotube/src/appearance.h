#ifndef APPEARANCE_H
#define APPEARANCE_H

#include "compat34.h"
#include <vector>

class QObject;
class QWidget;
class QPushButton;

#ifdef QT3_BUILD
class QPopupMenu;
typedef QPopupMenu MenuWidget34;
#else
class QMenu;
typedef QMenu MenuWidget34;
#endif

struct AppearanceMenuItemRef {
    const char* key;
    void* owner;
    int id;
    void* action;
    int group;
    int index;
    AppearanceMenuItemRef() : key(0), owner(0), id(-1), action(0), group(-1), index(-1) {}
};

QPushButton* makeAppearanceMenuButton(QWidget* parent, const QString& glyph);
void attachAppearanceMenu(QPushButton* btn, MenuWidget34* menu);
const char* const* appearanceLangLabelKeys();
int appearanceLangIndex(const QString& langCode);
void addAppearanceMenuItem(std::vector<AppearanceMenuItemRef>* out, MenuWidget34* menu,
                           const QObject* receiver, const char* slot, const char* key,
                           int group, int index);

#endif // APPEARANCE_H
