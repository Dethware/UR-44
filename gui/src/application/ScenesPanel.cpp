#include "ScenesPanel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QToolButton>
#include <QVBoxLayout>

#include "Application.h"
#include "Device.h"
#include "Scene.h"
#include "Widgets.h"

using UR44::Device;
namespace Scenes = UR44::Scenes;

ScenesPanel::ScenesPanel(Device* device, QWidget* parent) : QWidget(parent), m_device(device)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(Widgets::caption(tr("SCENES"), this));

    m_list = new QListWidget(this);
    m_list->setToolTip(tr("Prepared scenes: the mixer and effect settings. Double-click to recall."));
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { recall(m_list->row(item)); });
    connect(m_list, &QListWidget::currentRowChanged, this, &ScenesPanel::updateButtons);
    layout->addWidget(m_list, 1);

    auto* buttons = new QGridLayout;
    buttons->setSpacing(4);
    m_recall = new QPushButton(tr("Recall"), this);
    m_recall->setToolTip(tr("Send the selected scene to the device"));
    connect(m_recall, &QPushButton::clicked, this, [this]() { recall(selected()); });
    m_store = new QPushButton(tr("Store"), this);
    m_store->setToolTip(tr("Replace the selected scene with the current settings"));
    connect(m_store, &QPushButton::clicked, this, &ScenesPanel::store);
    m_storeAs = new QPushButton(tr("Store As..."), this);
    m_storeAs->setToolTip(tr("Keep the current settings as a new scene"));
    connect(m_storeAs, &QPushButton::clicked, this, &ScenesPanel::storeAs);

    auto* more = new QToolButton(this);
    more->setText(tr("More"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more);
    m_rename = menu->addAction(tr("Rename..."), this, &ScenesPanel::rename);
    m_remove = menu->addAction(tr("Delete"), this, &ScenesPanel::remove);
    m_clear = menu->addAction(tr("Delete All..."), this, &ScenesPanel::clear);
    menu->addSeparator();
    m_default = menu->addAction(tr("Default State..."), this, &ScenesPanel::recallDefault);
    m_default->setToolTip(tr("Reset the mixer and effects to the factory settings"));
    menu->setToolTipsVisible(true);
    menu->addSeparator();
    menu->addAction(tr("Import dspMixFx Scenes..."), this, &ScenesPanel::importFile);
    m_export = menu->addAction(tr("Export dspMixFx Scenes..."), this, &ScenesPanel::exportFile);
    more->setMenu(menu);
    more->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    buttons->addWidget(m_recall, 0, 0);
    buttons->addWidget(m_store, 0, 1);
    buttons->addWidget(m_storeAs, 1, 0);
    buttons->addWidget(more, 1, 1);
    layout->addLayout(buttons);

    connect(device, &Device::stateChanged, this, &ScenesPanel::updateButtons);
    reload();
}

void ScenesPanel::reload()
{
    const int row = m_list->currentRow();
    m_list->clear();
    const auto& scenes = APPLICATION->settings.scenes;
    for (int i = 0; i < scenes.size(); ++i)
    {
        auto* item = new QListWidgetItem(scenes[i].name, m_list);
        QFont font = item->font();
        font.setBold(i == m_active);
        item->setFont(font);
    }
    m_list->setCurrentRow(std::min(row, int(m_list->count()) - 1));
    updateButtons();
}

void ScenesPanel::updateButtons()
{
    const bool ready = m_device->isReady();
    const bool hasSelection = selected() >= 0;
    m_recall->setEnabled(ready && hasSelection);
    m_store->setEnabled(ready && hasSelection);
    m_storeAs->setEnabled(ready);
    m_default->setEnabled(ready);
    m_rename->setEnabled(hasSelection);
    m_remove->setEnabled(hasSelection);
    m_clear->setEnabled(m_list->count() > 0);
    m_export->setEnabled(m_list->count() > 0);
}

int ScenesPanel::selected() const
{
    int row = m_list->currentRow();
    return row >= 0 && row < APPLICATION->settings.scenes.size() ? row : -1;
}

void ScenesPanel::save()
{
    APPLICATION->settings.scheduleSave();
    reload();
}

void ScenesPanel::recall(int index)
{
    if (index < 0 || index >= APPLICATION->settings.scenes.size())
    {
        return;
    }
    const StoredScene& scene = APPLICATION->settings.scenes[index];
    QString error;
    if (!m_device->recallScene(QByteArray::fromBase64(scene.data.toLatin1()), &error))
    {
        QMessageBox::warning(this, tr("Recall Scene"), tr("Couldn't recall \"%1\": %2").arg(scene.name, error));
        return;
    }
    m_active = index;
    reload();
}

void ScenesPanel::store()
{
    int index = selected();
    QByteArray data = m_device->currentScene();
    if (index < 0 || data.isEmpty())
    {
        return;
    }
    StoredScene& scene = APPLICATION->settings.scenes[index];
    if (QMessageBox::question(this, tr("Store Scene"),
                              tr("Replace \"%1\" with the current settings?").arg(scene.name)) != QMessageBox::Yes)
    {
        return;
    }
    scene.data = QString::fromLatin1(data.toBase64());
    m_active = index;
    save();
}

void ScenesPanel::storeAs()
{
    QByteArray data = m_device->currentScene();
    if (data.isEmpty())
    {
        return;
    }
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Store Scene As"), tr("Scene name:"), QLineEdit::Normal,
                                         tr("Scene %1").arg(APPLICATION->settings.scenes.size() + 1), &ok)
                       .trimmed();
    if (!ok || name.isEmpty())
    {
        return;
    }
    APPLICATION->settings.scenes.append({name, QString::fromLatin1(data.toBase64()), {}});
    m_active = int(APPLICATION->settings.scenes.size()) - 1;
    save();
    m_list->setCurrentRow(m_active);
}

void ScenesPanel::rename()
{
    int index = selected();
    if (index < 0)
    {
        return;
    }
    StoredScene& scene = APPLICATION->settings.scenes[index];
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename Scene"), tr("Scene name:"), QLineEdit::Normal, scene.name, &ok)
                       .trimmed();
    if (ok && !name.isEmpty())
    {
        scene.name = name;
        save();
    }
}

void ScenesPanel::remove()
{
    int index = selected();
    if (index < 0)
    {
        return;
    }
    APPLICATION->settings.scenes.removeAt(index);
    m_active = m_active == index ? -1 : (m_active > index ? m_active - 1 : m_active);
    save();
}

void ScenesPanel::clear()
{
    if (QMessageBox::question(this, tr("Delete All Scenes"),
                              APPLICATION->settings.scenes.size() == 1
                                  ? tr("Delete the prepared scene? This can't be undone.")
                                  : tr("Delete all %1 prepared scenes? This can't be undone.")
                                        .arg(APPLICATION->settings.scenes.size())) != QMessageBox::Yes)
    {
        return;
    }
    APPLICATION->settings.scenes.clear();
    m_active = -1;
    save();
}

void ScenesPanel::recallDefault()
{
    QMessageBox box(QMessageBox::Warning, tr("Default State"),
                    tr("Reset the mixer and effects to the factory settings?"), QMessageBox::Cancel, this);
    box.setInformativeText(tr("Every input will be on in both mixes at 0 dB, centred. Mind microphones near "
                              "speakers. Effects are removed and the Sweet Spot Data goes back to \"01 Basic\".\n\n"
                              "The sample rate, PHONES 2 source, loopback and +48V stay as they are."));
    box.addButton(tr("Reset"), QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() == QMessageBox::Cancel)
    {
        return;
    }
    QString error;
    if (!m_device->recallScene(m_device->defaultScene(), &error))
    {
        QMessageBox::warning(this, tr("Default State"), tr("Couldn't reset the device: %1").arg(error));
        return;
    }
    m_active = -1;
    reload();
}

void ScenesPanel::importFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Import dspMixFx Scenes"), QString(),
                                                tr("dspMixFx scene files (*.UR44 *.ur44);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }
    QFile file(path);
    QString error;
    std::optional<Scenes::File> scenes;
    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
    }
    else
    {
        scenes = Scenes::readFile(file.readAll(), &error);
    }
    if (!scenes)
    {
        QMessageBox::warning(this, tr("Import dspMixFx Scenes"), tr("Couldn't read %1: %2").arg(path, error));
        return;
    }
    // a file without stored scenes still has the state it was saved with
    if (scenes->scenes.isEmpty() && scenes->current)
    {
        scenes->scenes.append({QFileInfo(path).completeBaseName(), *scenes->current});
    }
    for (const UR44::Scene& scene : std::as_const(scenes->scenes))
    {
        QString name = scene.name.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : scene.name;
        APPLICATION->settings.scenes.append({name, QString::fromLatin1(scene.data.toBase64()), {}});
    }
    save();
    QMessageBox::information(this, tr("Import dspMixFx Scenes"),
                             scenes->scenes.size() == 1 ? tr("Imported 1 scene.")
                                                        : tr("Imported %1 scenes.").arg(scenes->scenes.size()));
}

void ScenesPanel::exportFile()
{
    const auto& stored = APPLICATION->settings.scenes;
    QString path = QFileDialog::getSaveFileName(this, tr("Export dspMixFx Scenes"), QString(),
                                                tr("dspMixFx scene files (*.UR44)"));
    if (path.isEmpty())
    {
        return;
    }
    if (!path.endsWith(".UR44", Qt::CaseInsensitive))
    {
        path += ".UR44";
    }
    QList<UR44::Scene> scenes;
    for (const StoredScene& s : stored)
    {
        scenes.append({s.name, QByteArray::fromBase64(s.data.toLatin1())});
    }
    // the file's current state: the device's, or the first scene if it isn't connected
    QByteArray current = m_device->currentScene();
    if (current.isEmpty())
    {
        current = scenes.first().data;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(Scenes::writeFile(scenes, current)) < 0 || !file.commit())
    {
        QMessageBox::warning(this, tr("Export dspMixFx Scenes"), tr("Couldn't write %1: %2").arg(path, file.errorString()));
        return;
    }
    if (scenes.size() > Scenes::FILE_SLOTS)
    {
        QMessageBox::information(this, tr("Export dspMixFx Scenes"),
                                 tr("dspMixFx has %1 scene slots: only the first %1 scenes were exported.")
                                     .arg(Scenes::FILE_SLOTS));
    }
}
