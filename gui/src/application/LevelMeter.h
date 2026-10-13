#pragma once

#include <QWidget>

/*
 * A vertical peak meter in dBFS (-80 .. 0) with a peak-hold marker. Mono, or stereo as two bars.
 */
class LevelMeter : public QWidget
{
    Q_OBJECT
public:
    explicit LevelMeter(int channels = 1, QWidget* parent = nullptr);

    /// Levels and holds in dBFS, one per channel; anything at or below the floor is "no signal"
    void setLevels(const QList<double>& levels, const QList<double>& holds);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_channels;
    QList<double> m_levels;
    QList<double> m_holds;
};
