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
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void refresh();

    UR44::Device* m_device;
    uint16_t m_strip;
    UR44::ChannelStripCurves::Compressor m_compressor{{}};
    bool m_bypassed = false;
};

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
    void paintEvent(QPaintEvent* event) override;

private:
    void refresh();

    UR44::Device* m_device;
    uint16_t m_strip;
    UR44::ChannelStripCurves::Eq m_eq{{}};
    bool m_bypassed = false;
};
