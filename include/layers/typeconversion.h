// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "selection/selectionmanager.h"
#include <QStringList>

struct TypeConversionRequest {
    SWMMObjectRef::ObjectType family = SWMMObjectRef::Unknown;
    QList<SWMMObjectRef> objects;
    int target = -1;
    QString design;
    QString capture;
    int placement = 0;
    bool topologyOnly = false;
};

struct TypeConversionOutcome {
    SWMMObjectRef object;
    int source = -1;
    enum State { Eligible, Converted, AlreadyTarget, Skipped } state = Skipped;
    QString reason;
    QStringList cleared;
    QStringList warnings;
};
