#include "CompressorFXDialog.h"
#include "Widgets/ParamGlide.h"
#include "ui_CompressorFXDialog.h"

CompressorFXDialog::CompressorFXDialog(QWidget *parent, CompressorFX *fx) :
    QDialog(parent),
    ui(new Ui::CompressorFXDialog)
{
    ui->setupUi(this);

    this->fx = fx;

    ui->dialGain->setValue(fx->gain());
    ui->dialThreshold->setValue(fx->threshold());
    ui->dialRatio->setValue(fx->ratio());
    ui->dialAttack->setValue(fx->attack());
    ui->dialRelease->setValue(fx->release());

    ui->spinGain->setValue(fx->gain());
    ui->spinThreshold->setValue(fx->threshold());
    ui->spinRatio->setValue(fx->ratio());
    ui->spinAttack->setValue(fx->attack());
    ui->spinRelease->setValue(fx->release());

    this->connectAll();
}

CompressorFXDialog::~CompressorFXDialog()
{
    delete ui;
}

void CompressorFXDialog::connectAll()
{
    connect(ui->dialGain, SIGNAL(valueChanged(int)), this, SLOT(dialGainValueChanged(int)));
    connect(ui->dialThreshold, SIGNAL(valueChanged(int)), this, SLOT(dialThresholdValueChanged(int)));
    connect(ui->dialRatio, SIGNAL(valueChanged(int)), this, SLOT(dialRatioValueChanged(int)));
    connect(ui->dialAttack, SIGNAL(valueChanged(int)), this, SLOT(dialAttackValueChanged(int)));
    connect(ui->dialRelease, SIGNAL(valueChanged(int)), this, SLOT(dialReleaseValueChanged(int)));

    connect(ui->spinGain, SIGNAL(valueChanged(int)), this, SLOT(setGain(int)));
    connect(ui->spinThreshold, SIGNAL(valueChanged(int)), this, SLOT(setThreshold(int)));
    connect(ui->spinRatio, SIGNAL(valueChanged(int)), this, SLOT(setRatio(int)));
    connect(ui->spinAttack, SIGNAL(valueChanged(int)), this, SLOT(setAttack(int)));
    connect(ui->spinRelease, SIGNAL(valueChanged(int)), this, SLOT(setRelease(int)));
}

void CompressorFXDialog::disconnectAll()
{
    disconnect(ui->dialGain, SIGNAL(valueChanged(int)), this, SLOT(dialGainValueChanged(int)));
    disconnect(ui->dialThreshold, SIGNAL(valueChanged(int)), this, SLOT(dialThresholdValueChanged(int)));
    disconnect(ui->dialRatio, SIGNAL(valueChanged(int)), this, SLOT(dialRatioValueChanged(int)));
    disconnect(ui->dialAttack, SIGNAL(valueChanged(int)), this, SLOT(dialAttackValueChanged(int)));
    disconnect(ui->dialRelease, SIGNAL(valueChanged(int)), this, SLOT(dialReleaseValueChanged(int)));

    disconnect(ui->spinGain, SIGNAL(valueChanged(int)), this, SLOT(setGain(int)));
    disconnect(ui->spinThreshold, SIGNAL(valueChanged(int)), this, SLOT(setThreshold(int)));
    disconnect(ui->spinRatio, SIGNAL(valueChanged(int)), this, SLOT(setRatio(int)));
    disconnect(ui->spinAttack, SIGNAL(valueChanged(int)), this, SLOT(setAttack(int)));
    disconnect(ui->spinRelease, SIGNAL(valueChanged(int)), this, SLOT(setRelease(int)));
}

void CompressorFXDialog::dialGainValueChanged(int value)
{
    ui->spinGain->setValue(value);
}

void CompressorFXDialog::dialThresholdValueChanged(int value)
{
    ui->spinThreshold->setValue(value);
}

void CompressorFXDialog::dialRatioValueChanged(int value)
{
    ui->spinRatio->setValue(value);
}

void CompressorFXDialog::dialAttackValueChanged(int value)
{
    ui->spinAttack->setValue(value);
}

void CompressorFXDialog::dialReleaseValueChanged(int value)
{
    ui->spinRelease->setValue(value);
}

void CompressorFXDialog::setGain(int gain)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setGain"), float(gain), [this](float v) { if (fx) fx->setGain(v); });

    ui->dialGain->setValue(gain);

    connectAll();
}

void CompressorFXDialog::setThreshold(int threshold)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setThreshold"), float(threshold), [this](float v) { if (fx) fx->setThreshold(v); });

    ui->dialThreshold->setValue(threshold);

    connectAll();
}

void CompressorFXDialog::setRatio(int ratio)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setRatio"), float(ratio), [this](float v) { if (fx) fx->setRatio(v); });

    ui->dialRatio->setValue(ratio);

    connectAll();
}

void CompressorFXDialog::setAttack(int attack)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setAttack"), float(attack), [this](float v) { if (fx) fx->setAttack(v); });

    ui->dialAttack->setValue(attack);

    connectAll();
}

void CompressorFXDialog::setRelease(int release)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setRelease"), float(release), [this](float v) { if (fx) fx->setRelease(v); });

    ui->dialRelease->setValue(release);

    connectAll();
}

void CompressorFXDialog::on_btnReset_clicked()
{
    ui->spinGain->setValue(0);
    ui->spinThreshold->setValue(-15);
    ui->spinRatio->setValue(3);
    ui->spinAttack->setValue(10);
    ui->spinRelease->setValue(200);
}
