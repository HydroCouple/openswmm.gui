/*!
 * \file   meshpage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → Mesh.
 *
 * File management for 2D mesh configurations: which `.2dm` the engine reads
 * via `[2D_MESH_FILE]`, or the inline mesh embedded in the project `.inp`.
 *
 * Deliberately never gated on the 2D module: creating or selecting a mesh is
 * what turns the module on, so gating this page would be circular.
 */
#ifndef MESHPAGE_H
#define MESHPAGE_H

#include <functional>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QLabel;
class QListWidget;

namespace openswmmvis::ui
{

class MeshPage : public SimOptionsPage
{
    Q_OBJECT

public:
    explicit MeshPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;

    /*!
     * \brief How this page turns the 2D module on or off.
     *
     * Setting a mesh active implies the module is wanted, and clearing the
     * last one implies it is not. The check box lives on Models / Processes,
     * so the dialog supplies the setter rather than letting this page reach
     * across into another page's widget.
     */
    void set2DModuleSetter(std::function<void(bool)> setter);

private slots:
    void refreshMeshList();
    void onMeshSetActive();
    void onMeshRemove();
    void onMeshImport();

private:
    void buildUi();

    QListWidget *m_meshList        = nullptr;
    QLabel      *m_meshDirLabel    = nullptr;
    QLabel      *m_meshActiveLabel = nullptr;
    std::function<void(bool)> m_set2DModuleEnabled;
};

} // namespace openswmmvis::ui

#endif // MESHPAGE_H
