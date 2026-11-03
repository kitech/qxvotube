#ifndef GLOBALUIUTIL_H
#define GLOBALUIUTIL_H

class QString;

enum SticonIcon {
    SticonInfo     = 1,
    SticonWarning  = 2,
    SticonCritical = 3
};

extern void stbarShowStatusMessage(const QString &msg, int timeout = 0);
extern void stbarShowStatusMessage(const QString &msg, SticonIcon type, int timeout = 0);
extern void sticonShowStatusMessage(const QString &msg, SticonIcon iconType, int timeout);

#endif // GLOBALUIUTIL_H
