#ifndef REMOTESERVER_H
#define REMOTESERVER_H

// Small HTTP server that mirrors the program window to a phone browser and
// injects touch input back as mouse events (open by scanning a QR code).
// It also serves the phone MIXER page (/m): faders, mute and solo of every strip and the master, working through the
// mixer window's own controls (see setMixerApi) - nothing is opened on the computer's screen, so it works with the
// mixer window behind the karaoke program or hidden.
// The port and the access code are kept between runs, and the server can start with the program (autoStartEnabled), so
// the link in the phone keeps working without anyone touching the computer on the stage.

#include <functional>
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

    bool start(const QString &ip);      // the saved port first, then 8765.. (10 ports); the saved access code
    void stop();
    bool isRunning() const { return server.isListening(); }

    // start with the program (settings in KaraMixer.conf)
    static bool autoStartEnabled();
    static void setAutoStartEnabled(bool on);
    static QString savedIp();           // the IP the link was made with last time (empty: none)
    bool startAuto();                   // start with the saved IP (or the first one found); false: no network yet
    void newToken();                    // a new access code: the old link / QR stops working at once

    // The phone mixer page: `state` gives the mixer as JSON (asked a few times a second), `change` applies one change
    // (k, i, v ...), `meters` gives only the meter levels as "n,n,n,..." (asked ~30 times a second, so it must be tiny and
    // fast). All three are called in the GUI thread. Set by the main window.
    void setMixerApi(std::function<QByteArray()> state, std::function<void(const QMap<QString, QString> &)> change,
                     std::function<QByteArray()> meters);

    QString url() const;                // http://ip:port/m?t=token (the phone mixer page)
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

    static QString makeToken();
    static bool validToken(const QString &t);

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
    int                   lastClientCount = 0;      // what clientsChanged() last told

    std::function<QByteArray()> mixState;
    std::function<void(const QMap<QString, QString> &)> mixChange;
    std::function<QByteArray()> mixMeters;
};

#endif // REMOTESERVER_H
