/**
 * @file      profilesectionstore.h
 * @brief     Project-owned persistence of saved profile section definitions.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_PROFILESECTIONSTORE_H
#define OPENSWMMVIS_PROFILESECTIONSTORE_H
#include "plot/profilesectionseries.h"
#include <QJsonArray>
#include <QObject>

// Project-owned saved definitions. Dialog drafts remain outside this store
// until Save Section; failed restoration retains its payload and blocks loss.
class ProfileSectionStore : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty saved-section store named savedProfileSections.
     * @param[in] parent Optional QObject owner.
     */
    explicit ProfileSectionStore(QObject *parent = nullptr);
    /**
     * @brief Access validated saved definitions in stored order.
     * @return Store-owned reference invalidated by subsequent mutations; drafts are not included.
     */
    const QVector<ProfileSection::Definition> &definitions() const { return definitions_; }
    /**
     * @brief Inspect the restoration error that blocks edits and serialization.
     * @return Empty on a usable store, otherwise the retained diagnostic.
     */
    QString loadError() const { return loadError_; }
    /**
     * @brief Validate and upsert one saved section by exact ID.
     * @param[in] definition Complete definition accepted by ProfileSection::validateDefinition().
     * @param[out] error Optional failure diagnostic.
     * @return True for insertion, replacement or identical JSON; false preserves definitions for validation/load errors or the 1,000-entry limit.
     * @note Changed data emits definitionsChanged() then edited(); identical content emits neither.
     */
    bool saveDefinition(const ProfileSection::Definition &definition, QString *error = nullptr);
    /**
     * @brief Remove the first saved section with an exact matching ID.
     * @param[in] id Section identity.
     * @return True emits definitionsChanged() then edited(); false means absent or blocked by a restoration error.
     */
    bool removeDefinition(const QString &id);
    /**
     * @brief Validate and replace up to 1,000 uniquely identified section definitions.
     * @param[in] payload Array of definition objects.
     * @param[in] baseDirectory Directory for resolving definition file references.
     * @param[out] error Optional failure diagnostic.
     * @return True replaces definitions, clears rejected payload/error and emits definitionsChanged() only. False retains existing definitions and the rejected payload, blocking later edits/saves.
     */
    bool restore(const QJsonValue &payload, const QString &baseDirectory, QString *error = nullptr);
    /**
     * @brief Validate definitions and encode their references for a project save.
     * @param[in] baseDirectory Destination directory used for relative references.
     * @param[out] array Replaced only after every definition validates.
     * @param[out] error Optional validation or retained-load-error diagnostic.
     * @return True on full encoding; false leaves array unchanged.
     */
    bool serialize(const QString &baseDirectory, QJsonArray &array, QString *error = nullptr) const;
    /**
     * @brief Find or create the owner's direct savedProfileSections child.
     * @param[in] owner Optional QObject owner; null returns null.
     * @param[in] create Permit allocating a parent-owned child when absent.
     * @return Borrowed child pointer or null.
     */
    static ProfileSectionStore *forOwner(QObject *owner, bool create = true);
signals:
    /**
     * @brief Announce a changed saved definition or successful removal; restoration does not mark edits.
     */
    void edited();
    /**
     * @brief Announce changed definitions, including successful restoration.
     */
    void definitionsChanged();

private:
    QVector<ProfileSection::Definition> definitions_;
    QString loadError_;
    QJsonValue rejectedPayload_;
};
#endif
