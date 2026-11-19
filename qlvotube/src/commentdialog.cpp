#include "commentdialog.h"
#include <qtextedit.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qdatetime.h>

/* Unix 秒 → yyyy-MM-dd HH:mm(0=未知返回空);Qt3/4/5.8+ 兼容 */
static QString cmtTime(qint64 sec) {
    if (sec <= 0) return QString();
#ifdef QT3_BUILD
    QDateTime dt;
    dt.setTime_t((uint)sec);
#elif QT_VERSION < 0x050800
    QDateTime dt = QDateTime::fromTime_t((uint)sec);
#else
    QDateTime dt = QDateTime::fromSecsSinceEpoch(sec);
#endif
    return dt.toString(QString::fromLatin1("yyyy-MM-dd HH:mm"));
}

// 评论文本含 < > & 时需转义,否则 QTextEdit 当富文本解析导致内容丢失/错乱
static QString cmtEscape(const QString& s) {
    QString out;
    for (int i = 0; i < s.length(); ++i) {
        const QChar c = s.at(i);
        if (c == QChar('<')) out += qFromUtf8("&lt;");
        else if (c == QChar('>')) out += qFromUtf8("&gt;");
        else if (c == QChar('&')) out += qFromUtf8("&amp;");
        else out += c;
    }
    return out;
}

CommentDialog::CommentDialog(QWidget* parent) : QDialog(parent),
    m_list(0), m_hint(0), m_more(0)
{
#ifdef QT3_BUILD
    setCaption(qFromUtf8("评论"));
#else
    setWindowTitle(qFromUtf8("评论"));
#endif
    QBoxLayout* lay = qNewBoxLayout(this, QBoxLayout::TopToBottom, 8, 6);
    m_hint = new QLabel(this);
    m_list = new QTextEdit(this);
    m_list->setReadOnly(true);
    m_more = new QPushButton(qFromUtf8("加载更多"), this);
    lay->addWidget(m_hint, 0);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_more, 0);
    QObject::connect(m_more, SIGNAL(clicked()), this, SLOT(loadMore()));
    resize(560, 630);   /* 默认高度 +50%(420→630),评论列表一屏看更多 */
}

void CommentDialog::setTitle(const QString& t) {
    m_title = t;
    m_hint->setText(t.isEmpty() ? qFromUtf8("评论")
                                : t + qFromUtf8(" · 评论"));
}

void CommentDialog::clear() {
    m_list->clear();
    m_more->setEnabled(false);
    m_hint->setText(m_title.isEmpty() ? qFromUtf8("评论")
                                      : m_title + qFromUtf8(" · 评论"));
}

void CommentDialog::appendComments(const QList36<qxv::CommentItem>& items,
                                   bool hasMore) {
    for (int i = 0; i < items.size(); ++i) {
        const qxv::CommentItem& c = items[i];
        /* 抬头:谁发的 + 时间 + 回复数(该条 rcount,不递归) */
        QString head = c.author.isEmpty() ? qFromUtf8("匿名")
                                          : cmtEscape(c.author);
        const QString ts = cmtTime(c.time);
        if (!ts.isEmpty()) head += qFromUtf8("  ") + ts;
        if (c.replyCount > 0)
            head += qFromUtf8("  回复 ") + QString::number(c.replyCount);
        m_list->append(head);
        if (!c.text.isEmpty())
            m_list->append(qFromUtf8("  ") + cmtEscape(c.text));  /* 正文缩进 */
    }
    m_more->setEnabled(hasMore);
}

void CommentDialog::setError(const QString& msg) {
    m_hint->setText(qFromUtf8("评论获取失败：") + msg);
    m_more->setEnabled(false);
}

void CommentDialog::loadMore() {
    emit loadMoreRequested();
}
