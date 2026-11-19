#ifndef SUBTITLEDIALOG_H
#define SUBTITLEDIALOG_H

#include "compat34.h"
#include "providers/common/result.h"
#include <qdialog.h>
#include <qstring.h>
class QTextEdit;
class QLabel;

// 字幕列表窗口:只读文本区,逐行 "[mm:ss] 文本"。
// 工作线程结果由 MainWindow postEvent 回填。QTextEdit 跨 Qt3/4/6 通用。
class SubtitleDialog : public QDialog {
    Q_OBJECT
public:
    SubtitleDialog(QWidget* parent = 0);
    void setTitle(const QString& t);
    void clear();
    void setCues(const QList36<qxv::SubtitleCue>& cues, int currentIdx = -1);
    void setError(const QString& msg);
private:
    QTextEdit* m_list;
    QLabel* m_hint;
    QString m_title;
};

#endif // SUBTITLEDIALOG_H
