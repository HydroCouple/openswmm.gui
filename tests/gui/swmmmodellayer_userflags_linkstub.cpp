// Link stub: SWMMModelLayer::ensureUserFlagsModel().
//
// test_subcatchpropertyadapter exercises only the subcatchment `tag`
// Q_PROPERTY round-trip. The linked swmmsubcatchpropertyadapter.cpp references
// SWMMModelLayer::ensureUserFlagsModel() in its row-building path, which the
// test never drives (the adapter under test has no layer attached). Defining
// it here resolves that one symbol without dragging in the ~5000-line
// swmmmodellayer.cpp and its transitive layer/render/engine graph — keeping
// this a lean, self-contained leaf test. The real implementation lives in
// src/layers/swmmmodellayer.cpp and is covered by the app build.
#include "layers/swmmmodellayer.h"

openswmmvis::ui::UserFlagsModel *SWMMModelLayer::ensureUserFlagsModel()
{
    return nullptr;
}

// Same story for swmmlinkpropertyadapter.cpp's inletUsageRef() row, which
// asks the layer for the conduit's inlet-usage row: no layer here, so no row.
bool SWMMModelLayer::inletUsageFor(int, int, SWMM_InletUsage *) const
{
    return false;
}
