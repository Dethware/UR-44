#include "Widgets.h"

#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "Bindings.h"
#include "Knob.h"

namespace Widgets
{

QToolButton* squareButton(const QString& text, const QColor& color, const QString& tooltip, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tooltip);
    b->setCheckable(true);
    b->setFixedSize(26, 22);
    b->setStyleSheet(QString("QToolButton { background: #3a3a3a; color: #d0d0d0; border: 1px solid #1e1e1e;"
                             " border-radius: 3px; font-weight: bold; }"
                             "QToolButton:checked { background: %1; color: black; }"
                             "QToolButton:disabled { background: #2e2e2e; color: #6a6a6a; }"
                             // read-only indicators (PHONES 1) still show their state
                             "QToolButton:checked:disabled { background: %2; color: #202020; }")
                         .arg(color.name(), color.darker(150).name()));
    return b;
}

QToolButton* textButton(const QString& text, const QColor& color, const QString& tooltip, QWidget* parent)
{
    auto* b = squareButton(text, color, tooltip, parent);
    b->setFixedSize(QSize(64, 20));
    return b;
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignCenter);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    l->setFont(f);
    return l;
}

QWidget* knobControl(const QString& title, UR44::Device* device, const QString& parameter, uint16_t index,
                     const QColor& color, QWidget* parent, Knob** knobOut, QLabel** valueOut)
{
    auto* box = new QWidget(parent);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(caption(title, box));
    auto* knob = new Knob(color, box);
    layout->addWidget(knob, 0, Qt::AlignHCenter);
    auto* value = caption(QString(), box);
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance("-00.00 msec"));
    layout->addWidget(value);
    if (!parameter.isEmpty())
    {
        Bindings::slider(knob, device, parameter, index, value, {}, true);
        Bindings::describe(knob, title, QString());
    }
    if (knobOut)
    {
        *knobOut = knob;
    }
    if (valueOut)
    {
        *valueOut = value;
    }
    return box;
}

QWidget* knobControl(const QString& title, const QString& name, const QString& description, UR44::Device* device,
                     const QString& parameter, uint16_t index, const QColor& color, QWidget* parent)
{
    Knob* knob = nullptr;
    QWidget* box = knobControl(title, device, parameter, index, color, parent, &knob);
    Bindings::describe(knob, name, description);
    return box;
}

}
