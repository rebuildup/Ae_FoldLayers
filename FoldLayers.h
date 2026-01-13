/*******************************************************************/
/*                                                                 */
/*      FoldLayers - AEGP Plugin for After Effects                 */
/*      Recreates GM FoldLayers functionality                      */
/*      Developer: 361do_plugins                                   */
/*      https://github.com/rebuildup                               */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef FOLDLAYERS_H
#define FOLDLAYERS_H

#include "AEConfig.h"

#ifdef AE_OS_WIN
	#define VC_EXTRALEAN
	#include <io.h>
	#include <fcntl.h>
	#include <windows.h>
	#include <stddef.h>
	#include <windowsx.h>
	#include <commctrl.h>
	#include <sys/types.h>
	#include <sys/stat.h>
#endif

#ifndef DllExport
	#define DllExport
#endif

#include "entry.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include "AE_Macros.h"
#include "FoldLayers_Strings.h"
#include "String_Utils.h"

#include <string>
#include <vector>
#include <regex>

// Version info
#define FOLDLAYERS_MAJOR_VERSION	1
#define FOLDLAYERS_MINOR_VERSION	0
#define FOLDLAYERS_BUG_VERSION		0

// Group hierarchy constants
#define MAX_HIERARCHY_DEPTH		26	// a-z for sub-levels
#define GROUP_MARKER_START		"("
#define GROUP_MARKER_END		")"
#define GROUP_HIERARCHY_SEP		"/"

// Unicode prefix characters (UTF-8) - same as GM FoldLayers
// ▸ (U+25B8) = folded, ▾ (U+25BE) = unfolded
#define PREFIX_FOLDED			"\xE2\x96\xB8 "	// ▸ (small right-pointing triangle)
#define PREFIX_UNFOLDED			"\xE2\x96\xBE "	// ▾ (small down-pointing triangle) 

// Structure for group info
typedef struct {
	std::string		hierarchy;		// e.g., "1", "1/A", "1/A/i"
	std::string		name;			// User-defined group name
	bool			isFolded;		// Current fold state
	AEGP_LayerH		layerH;			// Handle to group layer
	A_long			layerIndex;		// Index in comp
} FoldGroupInfo;

// This entry point is exported through the PiPL (.r file)
extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;

#endif // FOLDLAYERS_H
