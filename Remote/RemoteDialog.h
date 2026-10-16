#ifndef REMOTEDIALOG_H
#define REMOTEDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QWidget>
#include <vector>

class QLabel;
class QComboBox;
class QPushButton;
class RemoteServer;

// Widget that paints a QR code (black on white, with quiet zone)
class QrWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QrWidget(QWidget *parent = nullptr);
    void setText(const QString &text);
    QSize sizeHint() const override { return QSize(240, 240); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    std::vector<std::vector<bool> > modules;
};

class RemoteDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RemoteDialog(RemoteServer *server, QWidget *parent = nullptr);

private slots:
    void toggle();
    void refresh();

private:
    RemoteServer *server;
    QrWidget     *qrw;
    QLabel       *lbUrl;
    QLabel       *lbStatus;
    QLabel       *lbHint;
    QComboBox    *cbIp;
    QPushButton  *btn;
};

#endif // REMOTEDIALOG_H
