#include "appearance.h"
#include "translator.h"
#include "compat34.h"
#ifdef QT3_BUILD
#include <qpushbutton.h>
#else
#include <QPushButton>
#include <QAction>
#endif

QPushButton* makeAppearanceMenuButton(QWidget* parent, const QString& glyph)
{
    QPushButton* b = new QPushButton(glyph, parent);
    b->setFixedSize(30, 30);
    return b;
}

void attachAppearanceMenu(QPushButton* btn, MenuWidget34* menu)
{
#ifdef QT3_BUILD
    menu->setCheckable(true);
    btn->setPopup(menu);
#else
    btn->setMenu(menu);
#endif
}

const char* const* appearanceLangLabelKeys()
{
    static const char* const kKeys[3] = { "简体中文", "繁體中文", "English" };
    return kKeys;
}

int appearanceLangIndex(const QString& langCode)
{
    if (langCode == "zh-TW") return 1;
    if (langCode == "en-US") return 2;
    return 0;
}

void addAppearanceMenuItem(std::vector<AppearanceMenuItemRef>* out, MenuWidget34* menu,
                           const QObject* receiver, const char* slot, const char* key,
                           int group, int index)
{
    QString text = _(qFromUtf8(key));
    AppearanceMenuItemRef ref;
    ref.key = key;
    ref.owner = menu;
    ref.group = group;
    ref.index = index;
#ifdef QT3_BUILD
    ref.id = menu->insertItem(text, receiver, slot);
#else
    QAction* a = menu->addAction(text, receiver, slot);
    if (group >= 0) a->setCheckable(true);
    ref.action = a;
#endif
    if (out) out->push_back(ref);
}
