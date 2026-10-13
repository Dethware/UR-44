#include "Parameters.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace UR44
{

QString Parameter::text(int32_t value) const
{
    QString t;
    if (!labels.isEmpty() && isValid(value))
    {
        t = labels.at(value - min);
    }
    else
    {
        t = QString::number(value);
    }
    return unit.isEmpty() ? t : t + ' ' + unit;
}

const Parameters& Parameters::instance()
{
    static const Parameters table = []() {
        QFile file(":/ur44/parameters.json");
        if (!file.open(QIODevice::ReadOnly))
        {
            qCritical() << "Parameter table resource is missing";
            return Parameters();
        }
        QString error;
        Parameters p = fromJson(file.readAll(), &error);
        if (p.isEmpty())
        {
            qCritical() << "Parameter table is invalid:" << error;
        }
        return p;
    }();
    return table;
}

Parameters Parameters::fromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull())
    {
        if (error)
        {
            *error = parseError.errorString();
        }
        return {};
    }

    Parameters p;
    const QJsonArray list = doc.object().value("parameters").toArray();
    for (const QJsonValue& v : list)
    {
        QJsonObject o = v.toObject();
        Parameter param;
        param.id = uint16_t(o.value("id").toInt());
        param.name = o.value("name").toString();
        param.min = o.value("min").toInt();
        param.max = o.value("max").toInt();
        param.defaultValue = o.value("default").toInt();
        param.instances = o.value("instances").toInt(1);
        param.unit = o.value("unit").toString();
        for (const QJsonValue& l : o.value("labels").toArray())
        {
            param.labels.append(l.toString());
        }
        if (param.name.isEmpty() || param.max < param.min ||
            (!param.labels.isEmpty() && param.labels.size() != param.max - param.min + 1))
        {
            if (error)
            {
                *error = QString("invalid entry for id %1").arg(param.id);
            }
            return {};
        }
        p.m_byId.insert(param.id, p.m_parameters.size());
        p.m_byName.insert(param.name, p.m_parameters.size());
        p.m_parameters.append(param);
    }

    const QJsonObject aliases = doc.object().value("aliases").toObject();
    for (auto it = aliases.begin(); it != aliases.end(); ++it)
    {
        auto target = p.m_byName.constFind(it.value().toString());
        if (target != p.m_byName.constEnd())
        {
            p.m_byName.insert(it.key(), *target);
        }
    }
    return p;
}

const Parameter* Parameters::byId(uint16_t id) const
{
    auto it = m_byId.constFind(id);
    return it == m_byId.constEnd() ? nullptr : &m_parameters.at(*it);
}

const Parameter* Parameters::byName(const QString& name) const
{
    auto it = m_byName.constFind(name);
    return it == m_byName.constEnd() ? nullptr : &m_parameters.at(*it);
}

const Parameter& Parameters::operator[](const QString& name) const
{
    const Parameter* p = byName(name);
    Q_ASSERT_X(p, "Parameters", qPrintable(name));
    static const Parameter missing;
    return p ? *p : missing;
}

}
