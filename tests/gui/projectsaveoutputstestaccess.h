// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "project/projectsaveoutputs.h"
class ProjectSaveOutputsTestAccess {
public:
    static void checkpoint(ProjectSaveOutputs &outputs, std::function<bool(int)> fn) {
        outputs.checkpoint_ = std::move(fn);
    }
};
