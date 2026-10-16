#ifndef HNKFILE_H
#define HNKFILE_H

#include <QByteArray>
#include <QString>

// Stub: the original HNK reader is not part of the public source tree.
// The mixer-only edition does not play song files, so every reader
// returns empty data (bpm() == 0 means "invalid file").
class HNKFile
{
public:
    static int bpm(const QString &) { return 0; }
    static QByteArray midData(const QString &) { return QByteArray(); }
    static QByteArray lyrData(const QString &) { return QByteArray(); }
    static QByteArray curData(const QString &) { return QByteArray(); }
};

#endif // HNKFILE_H
