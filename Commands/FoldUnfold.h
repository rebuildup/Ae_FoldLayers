/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Fold/Unfold Commands                          */
/*      Handles fold/unfold operations for dividers                */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef FOLDUNFOLD_H
#define FOLDUNFOLD_H

#include "AEConfig.h"
#include "AEGP_SuiteHandler.h"
#include <string>
#include <vector>

// Ensure shy mode is enabled in composition
void EnsureShyModeEnabled(AEGP_SuiteHandler& suites);

// Fold/unfold command handler
A_Err DoFoldUnfold(AEGP_SuiteHandler& suites);

#endif // FOLDUNFOLD_H
