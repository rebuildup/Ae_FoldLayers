/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Windows Mouse Hook                            */
/*      Handles double-click detection on Windows                  */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef WINDOWS_HOOK_H
#define WINDOWS_HOOK_H

#include "AEConfig.h"

#ifdef AE_OS_WIN

#include <windows.h>

// Initialize Windows mouse hook for double-click detection
void InitWindowsHook();

// Shutdown Windows mouse hook
void ShutdownWindowsHook();

// Process pending double-click
A_Err ProcessDoubleClick();

#endif // AE_OS_WIN

#endif // WINDOWS_HOOK_H
