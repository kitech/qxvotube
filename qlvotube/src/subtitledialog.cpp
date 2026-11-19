#include "subtitledialog.h"
#include <qtextedit.h>
#include <qlabel.h>

/* 毫秒 → mm:ss(超 1 小时用 h:mm:ss) */
static QString subTimeLabel(qint64 ms)
{
    const int total = (int)(ms / 1000);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    QString ss = QString::number(s);
    if (s < 10) ss = qFromUtf8("0") + ss;
    if (h > 0) {
        QString mm = QString::number(m);
        if (m < 10) mm = qFromUtf8("0") + mm;
        return QString::number(h) + qFromUtf8(":") + mm + qFromUtf8(":") + ss;
    }
    QString mm = QString::number(m);
    if (m < 10) mm = qFromUtf8("0") + mm;
    return mm + qFromUtf8(":") + ss;
}

/* 字幕文本含 < > & 时需转义,否则 QTextEdit 当富文本解析导致内容丢失 */
static QString subEscape(const QString& s)
{
    QString out;
    for (int i = 0; i < s.length(); ++i) {
        const QChar c = s.at(i);
        if (c == QChar('<')) out += qFromUtf8("&lt;");
        else if (c == QChar('>')) out += qFromUtf8("&gt;");
        else if (c == QChar('&')) out += qFromUtf8("&amp;");
        else if (c == QChar('\n')) out += qFromUtf8("<br>");  /* json3 多行文本 */
        else out += c;
    }
    return out;
}

SubtitleDialog::SubtitleDialog(QWidget* parent) : QDialog(parent),
    m_list(0), m_hint(0)
{
#ifdef QT3_BUILD
    setCaption(qFromUtf8("字幕"));
#else
    setWindowTitle(qFromUtf8("字幕"));
#endif
    QBoxLayout* lay = qNewBoxLayout(this, QBoxLayout::TopToBottom, 8, 6);
    m_hint = new QLabel(this);
    m_list = new QTextEdit(this);
    m_list->setReadOnly(true);
    lay->addWidget(m_hint, 0);
    lay->addWidget(m_list, 1);
    resize(560, 520);
}

void SubtitleDialog::setTitle(const QString& t)
{
    m_title = t;
    m_hint->setText(t.isEmpty() ? qFromUtf8("字幕列表")
                                : t + qFromUtf8(" · 字幕列表"));
}

void SubtitleDialog::clear()
{
    m_list->clear();
    m_hint->setText(m_title.isEmpty() ? qFromUtf8("字幕列表")
                                      : m_title + qFromUtf8(" · 字幕列表"));
}

void SubtitleDialog::setCues(const QList36<qxv::SubtitleCue>& cues, int currentIdx)
{
    m_list->clear();
    for (int i = 0; i < cues.size(); ++i) {
        const qxv::SubtitleCue& c = cues[i];
        const QString mark = (i == currentIdx) ? qFromUtf8("▶ ") : QString();
        m_list->append(mark + qFromUtf8("[") + subTimeLabel(c.startMs)
                       + qFromUtf8("] ") + subEscape(c.text));
    }
}

void SubtitleDialog::setError(const QString& msg)
{
    m_hint->setText(qFromUtf8("字幕获取失败：") + msg);
}
