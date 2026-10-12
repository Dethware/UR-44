#include "Settings.h"

#include <QDebug>
#include <QJsonDocument>

#include "FileSystem.h"
#include "Json.h"
#include "JsonSerialization.h"

Settings::Settings()
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(SAVE_DELAY_MS);
    QObject::connect(&m_saveTimer, &QTimer::timeout, &m_saveTimer, [this]() { save(); });
}

Settings::~Settings()
{
    flush();
}

void Settings::setPath(const QString& path)
{
    m_path = path;
}

bool Settings::load()
{
    try
    {
        QJsonDocument doc = Json::requireDocument(FS::read(m_path), "Settings");
        JsonReader::value(Json::requireObject(doc), *this);
    }
    catch (Exception& e)
    {
        qWarning() << "Couldn't load settings:" << e.cause();
        return false;
    }
    return true;
}

bool Settings::save()
{
    m_saveTimer.stop();
    QJsonObject object = JsonWriter::value(*this).toObject();
    try
    {
        FS::write(m_path, QJsonDocument(object).toJson(QJsonDocument::Indented));
    }
    catch (Exception& e)
    {
        qCritical() << "Couldn't save settings:" << e.cause();
        return false;
    }
    return true;
}

void Settings::scheduleSave()
{
    // Don't restart a running timer, or constant changes would postpone the save forever
    if (!m_saveTimer.isActive())
    {
        m_saveTimer.start();
    }
}

void Settings::flush()
{
    if (m_saveTimer.isActive())
    {
        save();
    }
}
