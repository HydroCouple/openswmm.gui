/**
 * @file      heatrasterassigndialog.h
 * @brief     Raster-based assignment of thermal parameters and forcing.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QDialog>
class SWMMModelLayer;
class MapCanvas;
namespace OpenSWMMVis
{
class HeatRasterAssignDialog final : public QDialog
{
public:
    HeatRasterAssignDialog(SWMMModelLayer *, MapCanvas *, QWidget *parent = nullptr);
};
}
