#pragma once

#include <QWidget>

/*
 * A vertical peak meter in dBFS (-80 .. 0) with a peak-hold marker. Mono, or stereo as two bars.
 * With the GainReduction scale it shows a compressor's gain reduction instead: 0 .. 20 dB, hanging from the top.
 */
class LevelMeter : public QWidget
{
    Q_OBJECT
public:
    enum Scale
    {
        Level,
        GainReduction,
    };

    explicit LevelMeter(int channels = 1, QWidget* parent = nullptr, Scale scale = Level);

    /// Gain reduction in dB (positive), one per channel; for the GainReduction scale
    void setGainReduction(const QList<double>& reductions);

    /// Levels and holds in dBFS, one per channel; anything at or below the floor is "no signal"
    void setLevels(const QList<double>& levels, const QList<double>& holds);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintGainReduction(class QPainter& p, const QRect& area);

    int m_channels;
    Scale m_scale;
    QList<double> m_levels;
    QList<double> m_holds;
};
