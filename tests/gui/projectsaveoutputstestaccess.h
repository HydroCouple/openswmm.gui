// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      projectsaveoutputstestaccess.h
 * @brief     Tests for projectsaveoutputstestaccess.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "project/projectsaveoutputs.h"
class ProjectSaveOutputsTestAccess
{
public:
    /**
     *
     * @brief Replace the project-output publication checkpoint hook for failure-injection tests.
     * @param outputs Borrowed transaction, not concurrently publishing while this setter runs.
     * @param fn Owned callback moved into the transaction, replacing/destroying the prior
     * hook. Receives zero before publication, i+1 after each zero-based record i, and
     * record count plus one after the committed marker. True continues; false triggers
     * rollback before commit. The final post-commit return is ignored. Empty disables
     * the hook. Captured references must outlive later synchronous publication calls;
     * exceptions are not caught by this setter. Does not itself publish or write files.
     *
     */
    static void checkpoint(ProjectSaveOutputs &outputs, std::function<bool(int)> fn)
    {
        outputs.checkpoint_ = std::move(fn);
    }
};
