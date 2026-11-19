#ifndef COMMENTDIALOG_H
#define COMMENTDIALOG_H

#include "qlcompat34.h"
#include "providers/common/result.h"
#include <qdialog.h>
#include <qstring.h>
class QTextEdit;
class QLabel;
class QPushButton;

// 评论窗口:只读文本区 + 加载更多(工作线程结果由 MainWindow postEvent 回填)
// 用 QTextEdit 而非 QListWidget:Qt3/4/6 都有 qtextedit.h,跨版本通用。
class CommentDialog : public QDialog {
    Q_OBJECT
public:
    CommentDialog(QWidget* parent = 0);
    void setTitle(const QString& t);
    void clear();
    void appendComments(const QList36<qxv::CommentItem>& items, bool hasMore);
    void setError(const QString& msg);
signals:
    void loadMoreRequested();       // 翻页请求
private slots:
    void loadMore();
private:
    QTextEdit* m_list;
    QLabel* m_hint;
    QPushButton* m_more;
    QString m_title;
};

#endif // COMMENTDIALOG_H
