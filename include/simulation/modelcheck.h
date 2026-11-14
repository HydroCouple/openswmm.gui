// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelcheck.h
 * @brief     Check Model (review G-09): open and initialize a model file the way a run does and
 *            list every error and warning, linked to the objects the messages name.
 * @details   checkModelFile() owns its engine and may run on a worker thread. The other functions
 *            are pure or read the live model engine on the GUI thread.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "selection/selectionmanager.h"

#include <QList>
#include <QPair>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::simulation
{

/*! One problem the engine reported. */
struct ModelCheckIssue {
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    int code = 0;    ///< SWMM number from "ERROR 138:" / "WARNING 04:"; 0 when the text has none
    QString message; ///< the engine's text, trimmed
    SWMMObjectRef::ObjectType objectType = SWMMObjectRef::Unknown; ///< set by resolveModelCheckObjects
    QString objectName; ///< empty when the message names no object of the model
};

/*! What checking one model file found. */
struct ModelCheckResult {
    bool opened = false;      ///< the strict open succeeded
    bool initialized = false; ///< initialize succeeded: a run would start
    QList<ModelCheckIssue> issues; ///< errors first, then warnings, each in the engine's order
};

/**
 * @brief Open a model file strictly, apply the run's 2D backend policy and initialize it in a new
 *   engine, then collect every error and warning; the engine is closed and destroyed on return.
 * @details Report and output files go next to @p inpPath; initialize opens no `[FILES]` save
 *   targets (those open when a run starts). Thread-safe: the engine is private to the call.
 * @param[in] inpPath Model file to check.
 * @return What the engine reported; objects are not resolved yet.
 */
ModelCheckResult checkModelFile(const QString &inpPath);

/**
 * @brief Split one engine message into severity and code.
 * @param[in] message Engine text such as "  ERROR 138: Node J1 has ...".
 * @param[in] fallback Severity when the text has no ERROR/WARNING prefix (the list it came from).
 * @return The issue with message, severity and code set.
 */
ModelCheckIssue parseModelCheckMessage(const QString &message, ModelCheckIssue::Severity fallback);

/**
 * @brief The objects a message may name, in reading order: each "<Type> <name>" pair whose type is
 *   a SWMM object word (Node, Junction, Outfall, Conduit, Pump, Subcatchment, Rain Gage, Aquifer,
 *   Time Series, Curve, Transect, ...), case-insensitive, the name optionally quoted.
 * @param[in] message Engine text.
 * @return Candidate (type, name) pairs; names keep their characters except surrounding quotes.
 */
QList<QPair<SWMMObjectRef::ObjectType, QString>> objectsNamedIn(const QString &message);

/**
 * @brief For each issue, link the first candidate from objectsNamedIn() that is an object of the
 *   model; a trailing '.', ',', ';', ':' or ')' is dropped from a name that does not match as read.
 * @param[in] engine Borrowed live model engine; GUI thread. Null leaves every issue unlinked.
 * @param[in,out] issues Issues whose objectType and objectName are set when a candidate matches.
 */
void resolveModelCheckObjects(SWMM_Engine engine, QList<ModelCheckIssue> &issues);

} // namespace openswmmvis::simulation
