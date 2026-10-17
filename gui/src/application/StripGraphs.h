#pragma once

#include <QColor>
#include <QWidget>

#include "ChannelStripCurves.h"

namespace UR44
{
class Device;
}

/*
 * The Channel Strip editor's graphs, like dspMixFx's: the compressor's transfer curve and the EQ's response. They
 * follow the strip's parameters, and grey out while the section is bypassed.
 */
class CompressorGraph : public QWidget
{
    Q_OBJECT
public:
    CompressorGraph(UR44::Device* device, uint16_t strip, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void refresh();
    QPointF thresholdPosition() const;

    UR44::Device* m_device;
    uint16_t m_strip;
    UR44::ChannelStripCurves::Compressor m_compressor{{}};
    bool m_bypassed = false;
};

/*
 * The EQ graph is also a control, like dspMixFx's: drag a band's point to set its frequency and gain (Shift: fine,
 * Ctrl: only along the direction first moved). The mid band's width handles, on its curve either side of the point,
 * and the wheel over its point set its Q. The other points ignore the wheel: they'd move away from the mouse.
 */
class EqGraph : public QWidget
{
    Q_OBJECT
public:
    EqGraph(UR44::Device* device, uint16_t strip, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// The band colours, for the band boxes' captions
    static QColor bandColor(UR44::ChannelStripCurves::Eq::Band band);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class Handle
    {
        None,
        Low,
        Mid,
        High,
        MidWidthBelow, // the mid band's width handles, below and above its frequency
        MidWidthAbove,
    };

    void refresh();
    QRectF plotArea() const;
    double xForFrequency(double hz) const;
    double frequencyForX(double x) const;
    double yForGain(double db) const;
    double gainForY(double y) const;
    /// Where a handle is drawn
    QPointF handlePosition(Handle handle) const;
    Handle handleAt(const QPointF& pos) const;
    /// Moves a band's point to `pos` (frequency and/or gain), or a width handle (Q)
    void moveHandle(Handle handle, const QPointF& pos, bool frequency, bool gain);
    void stepWidth(int steps);
    void showValues(Handle handle, const QPoint& globalPos);
    void set(const char* parameter, int32_t value);

    UR44::Device* m_device;
    uint16_t m_strip;
    UR44::ChannelStripCurves::Eq m_eq{{}};
    bool m_bypassed = false;

    Handle m_hover = Handle::None;
    int m_wheelRemainder = 0; // eighths of a degree, short of a notch
    struct Drag
    {
        Handle handle = Handle::None;
        QPointF press;  // where the drag (re)started
        QPointF origin; // the handle's position then
        bool fine = false;
        // Ctrl: 0 undecided, 1 frequency only, 2 gain only; -1 free
        int axis = -1;
    } m_drag;
};
