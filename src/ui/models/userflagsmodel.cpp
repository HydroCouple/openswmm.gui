/*!
 * \file   userflagsmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  UserFlagsModel implementation — see userflagsmodel.h.
 */
#include "ui/models/userflagsmodel.h"

#include <openswmm/engine/openswmm_model.h>

namespace openswmmvis::ui {

namespace {
constexpr int kNameBufLen  = 256;
constexpr int kDescBufLen  = 1024;
constexpr int kValueBufLen = 1024;
} // namespace

UserFlagsModel::UserFlagsModel(SWMM_Engine engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

const QVector<UserFlagsModel::Def> &UserFlagsModel::defs() const
{
    if (m_defsCacheValid)
        return m_defsCache;

    m_defsCache.clear();
    if (m_engine) {
        int count = 0;
        if (swmm_userflag_def_count(m_engine, &count) == SWMM_OK) {
            m_defsCache.reserve(count);
            char name[kNameBufLen];
            char desc[kDescBufLen];
            int  type = 0;
            for (int i = 0; i < count; ++i) {
                if (swmm_userflag_def_get(m_engine, i, name, kNameBufLen,
                                          &type, desc, kDescBufLen) != SWMM_OK)
                    continue;
                Def d;
                d.name        = QString::fromUtf8(name);
                d.type        = static_cast<FlagType>(type);
                d.description = QString::fromUtf8(desc);
                m_defsCache.append(d);
            }
        }
    }
    m_defsCacheValid = true;
    return m_defsCache;
}

bool UserFlagsModel::isDefined(const QString &name) const
{
    const QString upper = name.toUpper();
    for (const Def &d : defs())
        if (d.name == upper)
            return true;
    return false;
}

bool UserFlagsModel::define(const QString &name, FlagType type,
                            const QString &description, QString *outError)
{
    if (!m_engine) {
        if (outError) *outError = tr("Engine is not open.");
        return false;
    }
    const QByteArray nameUtf8 = name.toUtf8();
    const QByteArray descUtf8 = description.toUtf8();
    if (swmm_userflag_define(m_engine, nameUtf8.constData(),
                             static_cast<int>(type),
                             descUtf8.constData()) != SWMM_OK) {
        if (outError)
            *outError = tr("The engine rejected the flag definition "
                           "\"%1\".").arg(name);
        return false;
    }
    m_defsCacheValid = false;
    emit defsChanged();
    return true;
}

bool UserFlagsModel::undefine(const QString &name, QString *outError)
{
    if (!m_engine) {
        if (outError) *outError = tr("Engine is not open.");
        return false;
    }
    const QByteArray nameUtf8 = name.toUtf8();
    if (swmm_userflag_undefine(m_engine, nameUtf8.constData()) != SWMM_OK) {
        if (outError)
            *outError = tr("No user flag named \"%1\" is defined.").arg(name);
        return false;
    }
    m_defsCacheValid = false;
    emit defsChanged();
    return true;
}

QString UserFlagsModel::value(const QString &objType, const QString &objName,
                              const QString &flagName, bool *found) const
{
    if (found) *found = false;
    if (!m_engine) return QString();

    const QByteArray typeUtf8 = objType.toUtf8();
    const QByteArray nameUtf8 = objName.toUtf8();
    const QByteArray flagUtf8 = flagName.toUtf8();
    char buf[kValueBufLen];
    int  isFound = 0;
    if (swmm_userflag_value_get(m_engine, typeUtf8.constData(),
                                nameUtf8.constData(), flagUtf8.constData(),
                                buf, kValueBufLen, &isFound) != SWMM_OK)
        return QString();
    if (found) *found = (isFound != 0);
    return isFound ? QString::fromUtf8(buf) : QString();
}

bool UserFlagsModel::setValue(const QString &objType, const QString &objName,
                              const QString &flagName, const QString &value,
                              QString *outError)
{
    if (!m_engine) {
        if (outError) *outError = tr("Engine is not open.");
        return false;
    }
    const QByteArray typeUtf8  = objType.toUtf8();
    const QByteArray nameUtf8  = objName.toUtf8();
    const QByteArray flagUtf8  = flagName.toUtf8();
    const QByteArray valueUtf8 = value.toUtf8();
    if (swmm_userflag_value_set(m_engine, typeUtf8.constData(),
                                nameUtf8.constData(), flagUtf8.constData(),
                                valueUtf8.constData()) != SWMM_OK) {
        if (outError)
            *outError = tr("The engine rejected the value \"%1\" for flag "
                           "\"%2\" — check that the flag is defined and the "
                           "value matches its type.").arg(value, flagName);
        return false;
    }
    emit valueChanged(objType, objName, flagName);
    return true;
}

bool UserFlagsModel::clearValue(const QString &objType, const QString &objName,
                                const QString &flagName)
{
    if (!m_engine) return false;
    const QByteArray typeUtf8 = objType.toUtf8();
    const QByteArray nameUtf8 = objName.toUtf8();
    const QByteArray flagUtf8 = flagName.toUtf8();
    if (swmm_userflag_value_clear(m_engine, typeUtf8.constData(),
                                  nameUtf8.constData(),
                                  flagUtf8.constData()) != SWMM_OK)
        return false;
    emit valueChanged(objType, objName, flagName);
    return true;
}

QString UserFlagsModel::typeLabel(FlagType type)
{
    switch (type) {
        case FlagType::Boolean: return tr("Boolean");
        case FlagType::Integer: return tr("Integer");
        case FlagType::Real:    return tr("Real");
        case FlagType::String:  return tr("String");
    }
    return tr("String");
}

} // namespace openswmmvis::ui
