#include "AboutDialog.h"
#include "ui_AboutDialog.h"

#include "version.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QTextBrowser>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QApplication>
#include <QSettings>
#include <QClipboard>
#include <QDialog>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    ui->lbVersion->setText(VER_FILEVERSION_STR);

    QString version = QT_VERSION_STR;
    ui->lbQtVer->setText("Using Qt version " + version);
    // GPLv3 requires keeping the original copyright / license notice.
    ui->lbCopyright->setTextFormat(Qt::RichText);
    ui->lbCopyright->setText(QString::fromUtf8(
        "<div align='center'>%1<br>"
        "<span style='font-size:8pt; color:gray;'>Based on Handy Karaoke %2 2017%3 2019 pie62 (GPLv3)</span></div>")
        .arg(QString::fromUtf8(VER_LEGALCOPYRIGHT_STR), QString(QChar(0x00A9)), QString(QChar(0x2013))));
    ui->lbCopyright->setWordWrap(true);

    // source code / licence notice
    if (QVBoxLayout *vl = qobject_cast<QVBoxLayout *>(layout())) {
        QPushButton *btnLic = new QPushButton(QString::fromUtf8("ลิขสิทธิ์ / ซอร์สโค้ด (GPLv3)"), this);
        btnLic->setMinimumHeight(32);
        connect(btnLic, &QPushButton::clicked, this, [this]() {
            QDialog d(this);
            d.setWindowTitle(QString::fromUtf8("ลิขสิทธิ์ และ ซอร์สโค้ด"));
            d.resize(640, 520);
            QVBoxLayout *v = new QVBoxLayout(&d);
            QTextBrowser *tb = new QTextBrowser(&d);
            tb->setOpenExternalLinks(true);
            QString text;
            QFile f(QApplication::applicationDirPath() + QStringLiteral("/NOTICE.txt"));
            if (f.open(QIODevice::ReadOnly))
                text = QString::fromUtf8(f.readAll());
            else
                text = QString::fromUtf8("KaraMixer Edition เป็นซอฟต์แวร์เสรีภายใต้ GNU GPL v3\n"
                                         "Based on Handy Karaoke (C) 2017-2019 pie62 (GPLv3)\n"
                                         "ไม่พบไฟล์ NOTICE.txt ในโฟลเดอร์โปรแกรม");
            tb->setPlainText(text);
            v->addWidget(tb);
            QHBoxLayout *h = new QHBoxLayout();
            QPushButton *bg = new QPushButton(QString::fromUtf8("เปิดข้อความ GPLv3 ฉบับเต็ม"), &d);
            QPushButton *bc = new QPushButton(QString::fromUtf8("ปิด"), &d);
            connect(bg, &QPushButton::clicked, &d, []() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QApplication::applicationDirPath() + QStringLiteral("/gpl-3.0.txt")));
            });
            connect(bc, &QPushButton::clicked, &d, &QDialog::accept);
            h->addWidget(bg);
            h->addStretch(1);
            h->addWidget(bc);
            v->addLayout(h);
            d.exec();
        });

        QHBoxLayout *row = new QHBoxLayout();
        row->addStretch(1);
        row->addWidget(btnLic);

        vl->addLayout(row);
    }
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
