// Leaf InitialQualityDialog tests exercise the standalone editor. Owned editors
// and their project history are linked with the real application in test_inheritedpropertybulk.
#include "ui/dialogs/inheritedpropertiesdialog.h"
#include "layers/swmmmodellayer.h"
SWMM_Engine SWMMModelLayer::engine() const
{
    return nullptr;
}
QDialog *openInheritedPropertiesDialog(SWMMModelLayer *, const QList<SWMMObjectRef> &, bool,
                                       QWidget *, QUndoStack *)
{
    return nullptr;
}
