#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace UR44
{
class Device;
}

/*
 * Prepared scenes: named snapshots of the mixer and effect state to switch between, kept in the settings.
 *
 * Recall sends a scene to the device (double-click or Recall). Store replaces the selected scene with the current
 * state, Store As adds a new one. The More menu has the default state (factory mixer settings), and scenes can be
 * imported from and exported to dspMixFx scene files (*.UR44).
 *
 * A scene doesn't include the sample rate, PHONES 2 source, loopback, meter sources, +48V or input gains.
 */
class ScenesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ScenesPanel(UR44::Device* device, QWidget* parent = nullptr);

private:
    void reload();
    void updateButtons();
    int selected() const;
    void save();

    void recall(int index);
    void store();
    void storeAs();
    void rename();
    void remove();
    void clear();
    void recallDefault();
    void importFile();
    void exportFile();

    UR44::Device* m_device;
    QListWidget* m_list = nullptr;
    QPushButton* m_recall = nullptr;
    QPushButton* m_store = nullptr;
    QPushButton* m_storeAs = nullptr;
    class QAction* m_default = nullptr;
    class QAction* m_rename = nullptr;
    class QAction* m_remove = nullptr;
    class QAction* m_clear = nullptr;
    class QAction* m_export = nullptr;
    /// The scene last recalled or stored (shown in bold); -1 if none
    int m_active = -1;
};
