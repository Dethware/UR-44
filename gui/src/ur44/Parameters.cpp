#include "Parameters.h"

#include <cstring>

namespace UR44
{

QString Parameter::text(int32_t value) const
{
    QString t = labels && isValid(value) ? QString::fromLatin1(labels[value - min]) : QString::number(value);
    return *unit ? t + ' ' + QLatin1String(unit) : t;
}

const Parameters& Parameters::instance()
{
    static const Parameters parameters;
    return parameters;
}

const Parameter* Parameters::byId(uint16_t id) const
{
    return id < Generated::PARAMETER_COUNT ? &Generated::PARAMETERS[id] : nullptr;
}

const Parameter* Parameters::byName(const char* name) const
{
    const auto* first = Generated::NAME_INDEX;
    const auto* last = Generated::NAME_INDEX + Generated::NAME_INDEX_COUNT;
    const auto* it = std::lower_bound(first, last, name, [](const Generated::NameIndexEntry& e, const char* n) {
        return std::strcmp(e.name, n) < 0;
    });
    return it != last && std::strcmp(it->name, name) == 0 ? byId(it->id) : nullptr;
}

const Parameter* Parameters::byName(const QString& name) const
{
    return byName(name.toLatin1().constData());
}

const Parameter& Parameters::operator[](const char* name) const
{
    const Parameter* p = byName(name);
    Q_ASSERT_X(p, "Parameters", name);
    // Unknown names get a harmless placeholder in release builds
    static const Parameter missing{0xFFFF, "", 0, 0, 0, 0, "", nullptr};
    return p ? *p : missing;
}

const Parameter& Parameters::operator[](const QString& name) const
{
    return (*this)[name.toLatin1().constData()];
}

}
