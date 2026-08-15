/*!
 * \file   ilayerstylesubject.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One styleable subject for the unified LayerStyleDialog.
 *
 *         Slice U-2. A layer can present one or more ILayerStyleSubjects to
 *         the dialog, each becoming a tab (or tab-group) of property
 *         editors. The Q_PROPERTYs on subject->propertyObject() drive the
 *         editor UI through the existing QPropertyModel framework, so
 *         adding a new styleable thing is just a matter of writing a
 *         QObject with Q_PROPERTY declarations and exposing it via a
 *         subject.
 *
 *         Cancel rollback uses a JSON snapshot. The default helper
 *         JsonPropertySnapshot walks every Q_PROPERTY of the propertyObject
 *         (or, when the object is a SublayerStyle subclass, defers to its
 *         toJson()/fromJson() overrides which already exist for .oswp
 *         persistence).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_ILAYERSTYLESUBJECT_H
#define OPENSWMMVIS_UI_DIALOGS_ILAYERSTYLESUBJECT_H

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace openswmmvis::ui {

/*!
 * \brief One styleable thing the dialog can edit.
 *
 *        Subjects are owned by the dialog (unique_ptr) and outlive a
 *        single dialog session. The propertyObject() must outlive the
 *        subject; typically it's owned by the parent layer/sublayer.
 */
class ILayerStyleSubject
{
public:
    virtual ~ILayerStyleSubject() = default;

    /*! Tab title displayed in the dialog. */
    [[nodiscard]] virtual QString title() const = 0;

    /*! Optional sub-section. When non-empty, subjects sharing a section
     *  get grouped into a single outer tab with sub-tabs inside. */
    [[nodiscard]] virtual QString section() const { return {}; }

    /*! The QObject whose Q_PROPERTYs drive the editor. */
    [[nodiscard]] virtual QObject *propertyObject() const = 0;

    /*! Stable id for routing (right-click → "focus this tab"). For
     *  sublayer subjects this should match the sublayer id; for layer-
     *  level subjects, use a fixed slug like "general" / "rendering". */
    [[nodiscard]] virtual QString routingId() const { return {}; }

    /*! Capture all writable Q_PROPERTYs into a JSON object. The default
     *  implementation reads every Q_PROPERTY via QObject's metaobject;
     *  subjects whose propertyObject already has toJson()/fromJson()
     *  (i.e. SublayerStyle subclasses) can override to defer to that. */
    [[nodiscard]] virtual QJsonObject snapshot() const;

    /*! Restore from a snapshot captured by \ref snapshot. */
    virtual void restore(const QJsonObject &snapshot);
};

/*! Trivial concrete subject — title + propertyObject + optional routingId.
 *  Most layers can use this directly without writing a derived class. */
class LayerStyleSubject : public ILayerStyleSubject
{
public:
    LayerStyleSubject(QString title,
                      QObject *propertyObject,
                      QString routingId = {},
                      QString section   = {})
        : m_title(std::move(title))
        , m_section(std::move(section))
        , m_routingId(std::move(routingId))
        , m_propertyObject(propertyObject)
    {}

    [[nodiscard]] QString  title()          const override { return m_title; }
    [[nodiscard]] QString  section()        const override { return m_section; }
    [[nodiscard]] QString  routingId()      const override { return m_routingId; }
    [[nodiscard]] QObject *propertyObject() const override { return m_propertyObject; }

private:
    QString  m_title;
    QString  m_section;
    QString  m_routingId;
    QObject *m_propertyObject = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_ILAYERSTYLESUBJECT_H
