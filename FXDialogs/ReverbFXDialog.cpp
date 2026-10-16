#include "ReverbFXDialog.h"
#include "Widgets/ParamGlide.h"
#include "ui_ReverbFXDialog.h"

ReverbFXDialog::ReverbFXDialog(QWidget *parent, ReverbFX *reverbFX) :
    QDialog(parent),
    ui(new Ui::ReverbFXDialog)
{
    ui->setupUi(this);

    rv = reverbFX;

    if (rv != nullptr)
    {
        ui->dialInGain->setValue(rv->inGain());
        ui->spinInGain->setValue(rv->inGain());

        ui->dialRvMix->setValue(rv->reverbMix());
        ui->spinRvMix->setValue(rv->reverbMix());

        ui->dialRvTime->setValue(rv->reverbTime());
        ui->spinRvTime->setValue(rv->reverbTime());

        ui->dialHF->setValue(rv->highFreqRTRatio() * 1000);
        ui->spinHF->setValue(rv->highFreqRTRatio());
    }

    connectAll();
}

ReverbFXDialog::~ReverbFXDialog()
{
    delete ui;
}

void ReverbFXDialog::connectAll()
{
    connect(ui->dialInGain, SIGNAL(valueChanged(int)), this, SLOT(setInGain(int)));
    connect(ui->spinInGain, SIGNAL(valueChanged(int)), this, SLOT(setInGain(int)));

    connect(ui->dialRvMix, SIGNAL(valueChanged(int)), this, SLOT(setReverbMix(int)));
    connect(ui->spinRvMix, SIGNAL(valueChanged(int)), this, SLOT(setReverbMix(int)));

    connect(ui->dialRvTime, SIGNAL(valueChanged(int)), this, SLOT(setReverbTime(int)));
    connect(ui->spinRvTime, SIGNAL(valueChanged(int)), this, SLOT(setReverbTime(int)));

    connect(ui->dialHF, SIGNAL(valueChanged(int)), this, SLOT(onDialHFValueChanged(int)));
    connect(ui->spinHF, SIGNAL(valueChanged(double)), this, SLOT(onSpinHFValueChanged(double)));
}

void ReverbFXDialog::disconnectAll()
{
    disconnect(ui->dialInGain, SIGNAL(valueChanged(int)), this, SLOT(setInGain(int)));
    disconnect(ui->spinInGain, SIGNAL(valueChanged(int)), this, SLOT(setInGain(int)));

    disconnect(ui->dialRvMix, SIGNAL(valueChanged(int)), this, SLOT(setReverbMix(int)));
    disconnect(ui->spinRvMix, SIGNAL(valueChanged(int)), this, SLOT(setReverbMix(int)));

    disconnect(ui->dialRvTime, SIGNAL(valueChanged(int)), this, SLOT(setReverbTime(int)));
    disconnect(ui->spinRvTime, SIGNAL(valueChanged(int)), this, SLOT(setReverbTime(int)));

    disconnect(ui->dialHF, SIGNAL(valueChanged(int)), this, SLOT(onDialHFValueChanged(int)));
    disconnect(ui->spinHF, SIGNAL(valueChanged(double)), this, SLOT(onSpinHFValueChanged(double)));
}

void ReverbFXDialog::onDialHFValueChanged(int value)
{
    setHighFreqRTRatio(value / 1000.0f);
}

void ReverbFXDialog::onSpinHFValueChanged(double value)
{
    setHighFreqRTRatio((float)value);
}

void ReverbFXDialog::setInGain(int ing)
{
    disconnectAll();

    ui->dialInGain->setValue(ing);
    ui->spinInGain->setValue(ing);

    if (rv != nullptr)
        ParamGlide::set(this, QStringLiteral("setInGain"), float((float)ing), [this](float v) { if (rv) rv->setInGain(v); });

    connectAll();
}

void ReverbFXDialog::setReverbMix(int m)
{
    disconnectAll();

    ui->dialRvMix->setValue(m);
    ui->spinRvMix->setValue(m);

    if (rv != nullptr)
        ParamGlide::set(this, QStringLiteral("setReverbMix"), float((float)m), [this](float v) { if (rv) rv->setReverbMix(v); });

    connectAll();
}

void ReverbFXDialog::setReverbTime(int t)
{
    disconnectAll();

    ui->dialRvTime->setValue(t);
    ui->spinRvTime->setValue(t);

    if (rv != nullptr)
        ParamGlide::set(this, QStringLiteral("setReverbTime"), float((float)t), [this](float v) { if (rv) rv->setReverbTime(v); });

    connectAll();
}

void ReverbFXDialog::setHighFreqRTRatio(float hf)
{
    disconnectAll();

    ui->dialHF->setValue(hf*1000);
    ui->spinHF->setValue(hf);

    if (rv != nullptr)
        ParamGlide::set(this, QStringLiteral("setHighFreqRTRatio"), float(hf), [this](float v) { if (rv) rv->setHighFreqRTRatio(v); });

    connectAll();
}

void ReverbFXDialog::on_btnReset_clicked()
{
    setInGain(0);
    setReverbMix(0);
    setReverbTime(1000);
    setHighFreqRTRatio(0.001);
}
