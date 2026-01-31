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
#include "AE_GeneralPlug.h"

// Initialize Windows mouse hook for double-click detection
void InitWindowsHook();

// Shutdown Windows mouse hook
void ShutdownWindowsHook();

#endif // AE_OS_WIN

#endif // WINDOWS_HOOK_H
