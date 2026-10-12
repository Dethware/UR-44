#pragma once

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "Json.h"

/*
 * Reads a JSON object into structs, through their serialize() functions.
 *
 * A struct provides:
 *     template<typename S> void serialize(S& fp) { fp.property("name", member); ... }
 *     QJsonObject extra;
 *
 * Supported member types: uint32_t, int32_t, bool, QString, QList<T>, std::array<T, N>, std::pair<T1, T2> and such structs.
 *
 * A missing value, or one of the wrong type, leaves the field at its current (default) value. Keys that serialize()
 * doesn't ask for are collected, so they can be written back.
 */
class JsonReader
{
public:
    explicit JsonReader(const QJsonObject& object) : m_object(object) {}

    template<typename T>
    void property(const char* name, T& v)
    {
        m_extra.remove(QLatin1String(name));
        value(m_object.value(QLatin1String(name)), v);
    }

    QJsonObject extra() const
    {
        return m_extra;
    }

    template<typename T>
    static void value(const QJsonValue& json, T& v)
    {
        if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, bool> || std::is_same_v<T, QString>)
        {
            v = Json::ensureIsType<T>(json, v);
        }
        else
        {
            // structs
            if (!json.isObject())
            {
                return;
            }
            JsonReader reader(json.toObject());
            v.serialize(reader);
            v.extra = reader.extra();
        }
    }

    template<typename T>
    static void value(const QJsonValue& json, QList<T>& v)
    {
        if (!json.isArray())
        {
            return;
        }
        QJsonArray array = json.toArray();
        v.resize(array.size());
        for (qsizetype i = 0; i < array.size(); ++i)
        {
            value(array[i], v[i]);
        }
    }

    // Fixed size native arrays are only read if the sizes match exactly
    template<typename T, size_t N>
    static void value(const QJsonValue& json, std::array<T, N>& v)
    {
        if (!json.isArray())
        {
            return;
        }
        QJsonArray array = json.toArray();
        if (size_t(array.size()) != N)
        {
            qWarning() << "Fixed size array has" << array.size() << "elements instead of" << N << ", ignoring.";
            return;
        }
        for (size_t i = 0; i < N; ++i)
        {
            value(array[qsizetype(i)], v[i]);
        }
    }

    // Pairs are serialized as {"first": ..., "second": ...}
    template<typename T1, typename T2>
    static void value(const QJsonValue& json, std::pair<T1, T2>& v)
    {
        if (!json.isObject())
        {
            return;
        }
        QJsonObject object = json.toObject();
        value(object.value("first"), v.first);
        value(object.value("second"), v.second);
    }

private:
    QJsonObject m_object;
    QJsonObject m_extra = m_object;
};

/*
 * Writes structs into a JSON object, through their serialize() functions.
 * Unknown keys collected by JsonReader are written back first.
 */
class JsonWriter
{
public:
    explicit JsonWriter(const QJsonObject& extra) : m_object(extra) {}

    template<typename T>
    void property(const char* name, T& v)
    {
        m_object.insert(QLatin1String(name), value(v));
    }

    QJsonObject object() const
    {
        return m_object;
    }

    template<typename T>
    static QJsonValue value(T& v)
    {
        if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
        {
            return qint64(v);
        }
        else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, QString>)
        {
            return v;
        }
        else
        {
            // structs
            JsonWriter writer(v.extra);
            v.serialize(writer);
            return writer.object();
        }
    }

    template<typename T>
    static QJsonValue value(QList<T>& v)
    {
        QJsonArray array;
        for (auto& item : v)
        {
            array.append(value(item));
        }
        return array;
    }

    template<typename T, size_t N>
    static QJsonValue value(std::array<T, N>& v)
    {
        QJsonArray array;
        for (auto& item : v)
        {
            array.append(value(item));
        }
        return array;
    }

    template<typename T1, typename T2>
    static QJsonValue value(std::pair<T1, T2>& v)
    {
        QJsonObject object;
        object.insert("first", value(v.first));
        object.insert("second", value(v.second));
        return object;
    }

private:
    QJsonObject m_object;
};
