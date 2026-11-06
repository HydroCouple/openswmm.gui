#pragma once
#include "selection/selectionmanager.h"
class SWMMModelLayer;
class QUndoStack;
class QDialog;
class QWidget;
QDialog *openInheritedPropertiesDialog(SWMMModelLayer *layer, const QList<SWMMObjectRef> &targets,
                                       bool initialQuality, QWidget *parent = nullptr,
                                       QUndoStack *undo = nullptr);
