#include "AboutDialog.h"
#include "ui_AboutDialog.h"

#include "version.h"
#include "Widgets/ProLicense.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    ui->lbVersion->setText(VER_FILEVERSION_STR);

    QString version = QT_VERSION_STR;
    ui->lbQtVer->setText("Using Qt version " + version);
    // GPLv3 requires keeping the original copyright / license notice.
    ui->lbCopyright->setText(QString(VER_LEGALCOPYRIGHT_STR) +
        "\nDerived from Handy Karaoke, Copyright (C) 2017-2019 pie62 (GPLv3)");
    ui->lbCopyright->setWordWrap(true);

    // PRO licence (tied to this machine's hardware ID)
    if (QVBoxLayout *vl = qobject_cast<QVBoxLayout *>(layout())) {
        QFont mono(QString::fromLatin1("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(12);
        mono.setBold(true);

        QLabel *hw = new QLabel(QString::fromLatin1("Hardware ID: ") + ProLicense::hardwareId(), this);
        hw->setFont(mono);
        hw->setStyleSheet(QString::fromLatin1("QLabel { color: #ffc83d; }"));
        hw->setTextInteractionFlags(Qt::TextSelectableByMouse);

        QLabel *lic = new QLabel(this);
        lic->setFont(mono);

        QPushButton *btn = new QPushButton(QString::fromLatin1("License (PRO)"), this);
        btn->setMinimumHeight(32);

        auto refresh = [lic]() {
            const QString s = ProLicense::statusText();
            if (s.isEmpty()) {
                lic->setStyleSheet(QString::fromLatin1("QLabel { color: #ff8a8a; }"));
                lic->setText(QString::fromLatin1("License: FREE (not activated)"));
            } else {
                lic->setStyleSheet(QString::fromLatin1("QLabel { color: #5be36b; }"));
                lic->setText(QString::fromLatin1("License: ") + s);
            }
        };
        refresh();

        connect(btn, &QPushButton::clicked, this, [this, refresh]() {
            ProLicense::Dialog dlg(this);
            dlg.exec();
            refresh();
        });

        QHBoxLayout *row = new QHBoxLayout();
        row->addStretch(1);
        row->addWidget(btn);

        vl->addWidget(hw);
        vl->addWidget(lic);
        vl->addLayout(row);
    }
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
