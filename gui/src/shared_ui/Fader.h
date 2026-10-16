#pragma once

#include <QList>
#include <QSlider>
#include <QString>

/*
 * A vertical fader with notches beside its track at given values, like a mixing console's scale: short marks on
 * both sides of the track, longer ones for the major marks, and an optional label at the left of each.
 */
class Fader : public QSlider
{
    Q_OBJECT
public:
    struct Mark
    {
        int value;
        QString label;
        bool major = false;
    };

    explicit Fader(QWidget* parent = nullptr);

    void setMarks(const QList<Mark>& marks);
    const QList<Mark>& marks() const { return m_marks; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// The y coordinate of the handle's centre at `value`
    int yForValue(int value) const;
    int labelWidth() const;

    QList<Mark> m_marks;
};
