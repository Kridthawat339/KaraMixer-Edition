#include "AutoWahFXDialog.h"
#include "Widgets/ParamGlide.h"
#include "ui_AutoWahFXDialog.h"


AutoWahFXDialog::AutoWahFXDialog(QWidget *parent, AutoWahFX *fx) :
    QDialog(parent),
    ui(new Ui::AutoWahFXDialog)
{
    ui->setupUi(this);

    this->fx = fx;

    ui->spinDryMix->setValue(fx->dryMix());
    ui->spinWetMix->setValue(fx->wetMix());
    ui->spinFeedback->setValue(fx->feedback());
    ui->spinRate->setValue(fx->rate());
    ui->spinRange->setValue(fx->range());
    ui->spinFrequency->setValue(fx->freq());

    ui->dialDryMix->setValue(fx->dryMix() * 100);
    ui->dialWetMix->setValue(fx->wetMix() * 100);
    ui->dialFeedback->setValue(fx->feedback() * 100);
    ui->dialRate->setValue(fx->rate() * 10);
    ui->dialRange->setValue(fx->range() * 10);
    ui->dialFrequency->setValue(fx->freq());

    this->connectAll();
}

AutoWahFXDialog::~AutoWahFXDialog()
{
    delete ui;
}

void AutoWahFXDialog::connectAll()
{
    connect(ui->dialDryMix, SIGNAL(valueChanged(int)), this, SLOT(onDialDryMixValueChanged(int)));
    connect(ui->dialWetMix, SIGNAL(valueChanged(int)), this, SLOT(onDialWetMixValueChanged(int)));
    connect(ui->dialFeedback, SIGNAL(valueChanged(int)), this, SLOT(onDialFeedbackValueChanged(int)));
    connect(ui->dialRate, SIGNAL(valueChanged(int)), this, SLOT(onDialRateValueChanged(int)));
    connect(ui->dialRange, SIGNAL(valueChanged(int)), this, SLOT(onDialRangeValueChanged(int)));
    connect(ui->dialFrequency, SIGNAL(valueChanged(int)), this, SLOT(onDialFrequencyValueChanged(int)));

    connect(ui->spinDryMix, SIGNAL(valueChanged(double)), this, SLOT(onSpinDryMixValueChanged(double)));
    connect(ui->spinWetMix, SIGNAL(valueChanged(double)), this, SLOT(onSpinWetMixValueChanged(double)));
    connect(ui->spinFeedback, SIGNAL(valueChanged(double)), this, SLOT(onSpinFeedbackValueChanged(double)));
    connect(ui->spinRate, SIGNAL(valueChanged(double)), this, SLOT(onSpinRateValueChanged(double)));
    connect(ui->spinRange, SIGNAL(valueChanged(double)),  this, SLOT(onSpinRangeValueChanged(double)));
    connect(ui->spinFrequency, SIGNAL(valueChanged(int)), this, SLOT(onSpinFrequencyValueChanged(int)));
}

void AutoWahFXDialog::disconnectAll()
{
    disconnect(ui->dialDryMix, SIGNAL(valueChanged(int)), this, SLOT(onDialDryMixValueChanged(int)));
    disconnect(ui->dialWetMix, SIGNAL(valueChanged(int)), this, SLOT(onDialWetMixValueChanged(int)));
    disconnect(ui->dialFeedback, SIGNAL(valueChanged(int)), this, SLOT(onDialFeedbackValueChanged(int)));
    disconnect(ui->dialRate, SIGNAL(valueChanged(int)), this, SLOT(onDialRateValueChanged(int)));
    disconnect(ui->dialRange, SIGNAL(valueChanged(int)), this, SLOT(onDialRangeValueChanged(int)));
    disconnect(ui->dialFrequency, SIGNAL(valueChanged(int)), this, SLOT(onDialFrequencyValueChanged(int)));

    disconnect(ui->spinDryMix, SIGNAL(valueChanged(double)), this, SLOT(onSpinDryMixValueChanged(double)));
    disconnect(ui->spinWetMix, SIGNAL(valueChanged(double)), this, SLOT(onSpinWetMixValueChanged(double)));
    disconnect(ui->spinFeedback, SIGNAL(valueChanged(double)), this, SLOT(onSpinFeedbackValueChanged(double)));
    disconnect(ui->spinRate, SIGNAL(valueChanged(double)), this, SLOT(onSpinRateValueChanged(double)));
    disconnect(ui->spinRange, SIGNAL(valueChanged(double)),  this, SLOT(onSpinRangeValueChanged(double)));
    disconnect(ui->spinFrequency, SIGNAL(valueChanged(int)), this, SLOT(onSpinFrequencyValueChanged(int)));
}

void AutoWahFXDialog::onDialDryMixValueChanged(int value)
{
    ui->spinDryMix->setValue(value / 100.0);
}

void AutoWahFXDialog::onDialWetMixValueChanged(int value)
{
    ui->spinWetMix->setValue(value / 100.0);
}

void AutoWahFXDialog::onDialFeedbackValueChanged(int value)
{
    ui->spinFeedback->setValue(value / 100.0);
}

void AutoWahFXDialog::onDialRateValueChanged(int value)
{
    ui->spinRate->setValue(value / 10.0);
}

void AutoWahFXDialog::onDialRangeValueChanged(int value)
{
    ui->spinRange->setValue(value / 10.0);
}

void AutoWahFXDialog::onDialFrequencyValueChanged(int value)
{
    ui->spinFrequency->setValue(value);
}

void AutoWahFXDialog::onSpinDryMixValueChanged(double arg1)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setDryMix"), float(arg1), [this](float v) { if (fx) fx->setDryMix(v); });

    ui->dialDryMix->setValue(arg1 * 100);

    connectAll();
}

void AutoWahFXDialog::onSpinWetMixValueChanged(double arg1)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setWetMix"), float(arg1), [this](float v) { if (fx) fx->setWetMix(v); });

    ui->dialWetMix->setValue(arg1 * 100);

    connectAll();
}

void AutoWahFXDialog::onSpinFeedbackValueChanged(double arg1)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setFeedback"), float(arg1), [this](float v) { if (fx) fx->setFeedback(v); });

    ui->dialFeedback->setValue(arg1 * 100);

    connectAll();
}

void AutoWahFXDialog::onSpinRateValueChanged(double arg1)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setRate"), float(arg1), [this](float v) { if (fx) fx->setRate(v); });

    ui->dialRate->setValue(arg1 * 10);

    connectAll();
}

void AutoWahFXDialog::onSpinRangeValueChanged(double arg1)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setRange"), float(arg1), [this](float v) { if (fx) fx->setRange(v); });

    ui->dialRange->setValue(arg1 * 10);

    connectAll();
}

void AutoWahFXDialog::onSpinFrequencyValueChanged(int arg1)
{
    disconnectAll();

    ParamGlide::set(this, QStringLiteral("setFreq"), float((float)arg1), [this](float v) { if (fx) fx->setFreq(v); });

    ui->dialFrequency->setValue(arg1);

    connectAll();
}

void AutoWahFXDialog::on_btnReset_clicked()
{
    ui->spinDryMix->setValue(0.500);
    ui->spinWetMix->setValue(1.500);
    ui->spinFeedback->setValue(0.5);
    ui->spinRate->setValue(2.0);
    ui->spinRange->setValue(4.3);
    ui->spinFrequency->setValue(50);
}
