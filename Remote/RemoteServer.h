#ifndef REMOTESERVER_H
#define REMOTESERVER_H

// Small HTTP server that mirrors the program window to a phone browser and
// injects touch input back as mouse events (open by scanning a QR code).

#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

class RemoteServer : public QObject
{
    Q_OBJECT

public:
    // baseWindow: the window that is mirrored by default (the mixer);
    // ignoreWindow: never mirrored (the hidden lyrics main window)
    RemoteServer(QWidget *baseWindow, QWidget *ignoreWindow, QObject *parent = nullptr);
    ~RemoteServer();

    bool start(const QString &ip);      // tries port 8765.. (10 ports)
    void stop();
    bool isRunning() const { return server.isListening(); }

    QString url() const;                // http://ip:port/?t=token
    quint16 port() const { return server.serverPort(); }
    int     clientCount() const;        // phones seen in the last few seconds

    // IPv4 addresses of this computer, private LAN ranges first
    static QStringList localAddresses();

signals:
    void clientsChanged();

private slots:
    void onNewConnection();

private:
    void onReadyRead(QTcpSocket *s);
    void handle(QTcpSocket *s, const QByteArray &method, const QString &path,
                const QByteArray &query, const QByteArray &body);
    void reply(QTcpSocket *s, int code, const QByteArray &contentType,
               const QByteArray &body, const QByteArray &extraHeaders = QByteArray());

    QWidget *targetWindow() const;
    bool grabFrame();                   // updates lastJpeg / lastHash
    void inject(const QMap<QString, QString> &p);
    void touchClient(const QString &ip);

    QWidget     *mainWin;
    QWidget     *ignoreWin;
    QTcpServer   server;
    QString      ipText;
    QString      token;

    QByteArray     lastJpeg;
    QString        lastHash;
    QElapsedTimer  lastGrab;

    QPointer<QWidget> dragWindow;
    QPointer<QWidget> pressWidget;

    QMap<QString, qint64> clients;      // ip -> msecs (QElapsedTimer based)
    QElapsedTimer         clock;
    QTimer                idleTimer;
};

#endif // REMOTESERVER_H
