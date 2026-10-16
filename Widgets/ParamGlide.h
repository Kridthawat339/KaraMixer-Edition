#ifndef PARAMGLIDE_H
#define PARAMGLIDE_H

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QHash>
#include <QString>
#include <functional>
#include <cmath>

// Glides an effect parameter to its new value in a straight line over ~40 ms instead of jumping
// there in a single step. A dial or slider that is dragged then changes the sound smoothly (no zipper /
// stepping noise) and the response follows the knob linearly.
//
//   ParamGlide::set(this, QStringLiteral("wetMix"), value, [this](float v) { fx->setWetMix(v); });
//
// owner : the dialog (used to key the glide and to drop it if the dialog is closed meanwhile)
// name  : which parameter of that dialog
// The first value ever sent for a parameter is applied straight away; later ones glide from the last one.
// Always call from the GUI thread.
namespace ParamGlide {

namespace detail {

class Engine : public QObject
{
public:
    struct Entry {
        QPointer<QObject> owner;
        std::function<void(float)> apply;
        float cur = 0.0f, target = 0.0f, step = 0.0f;
        int left = 0;
    };

    static Engine &instance()
    {
        static Engine e;
        return e;
    }

    void set(QObject *owner, const QString &name, float target, const std::function<void(float)> &apply)
    {
        const QString key = QString::number(reinterpret_cast<quintptr>(owner)) + QLatin1Char('|') + name;
        auto it = entries.find(key);
        if (it == entries.end() || it->owner.isNull()) {   // new parameter (or a new dialog on an old address)
            apply(target);                       // nothing to glide from yet
            Entry e;
            e.owner = owner;
            e.apply = apply;
            e.cur = e.target = target;
            entries.insert(key, e);
            return;
        }

        Entry &e = it.value();
        e.owner = owner;
        e.apply = apply;
        e.target = target;
        if (std::fabs(target - e.cur) < 1e-6f) {
            e.left = 0;
            return;
        }
        e.left = kSteps;
        e.step = (target - e.cur) / float(kSteps);
        if (!timer.isActive())
            timer.start();
    }

private:
    static const int kSteps = 5;                 // x 8 ms

    Engine()
    {
        timer.setInterval(8);
        connect(&timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void tick()
    {
        bool any = false;
        for (auto it = entries.begin(); it != entries.end(); ) {
            Entry &e = it.value();
            if (e.owner.isNull()) {              // dialog is gone: forget it
                it = entries.erase(it);
                continue;
            }
            if (e.left > 0) {
                e.cur = (--e.left == 0) ? e.target : e.cur + e.step;
                e.apply(e.cur);
                any = any || e.left > 0;
            }
            ++it;
        }
        if (!any)
            timer.stop();
    }

    QTimer timer;
    QHash<QString, Entry> entries;
};

} // namespace detail

inline void set(QObject *owner, const QString &name, float target, const std::function<void(float)> &apply)
{
    detail::Engine::instance().set(owner, name, target, apply);
}

} // namespace ParamGlide

#endif // PARAMGLIDE_H
