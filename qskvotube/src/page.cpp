#include "page.h"

Page::Page(QQuickItem* parent)
    : QskControl(parent)
{
    // qskmusic 版有、qsktox 版漏:页面可聚焦 → 点亮焦点链头
    // (StackBox → Page → 文本控件),否则键盘输入不可达
    setFocusPolicy(Qt::StrongFocus);
}

Page::~Page()
{
}

void Page::onCreate(const QVariantMap&, const QVariantMap&) {}
void Page::onStart() {}
void Page::onResume() {}
void Page::onPause() {}
void Page::onStop() {}
void Page::onDestroy() {}
void Page::onRestart() {}
void Page::onNewIntent(const QVariantMap&) {}
void Page::onSaveInstanceState(QVariantMap&) {}
void Page::onRestoreInstanceState(const QVariantMap&) {}

void Page::finish()
{
    emit finishRequested();
}
