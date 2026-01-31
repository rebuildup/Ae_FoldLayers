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

#include "entry.h"

#ifndef DllExport
	#define DllExport
#endif
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

// Unicode prefix characters (UTF-8) for visual fold state in layer names
// Fold state is stored in FD-0/FD-1 hidden stream groups AND shown visually in layer name
// ▸ (U+25B8) = folded, ▾ (U+25BE) = unfolded
#define PREFIX_FOLDED			"\xE2\x96\xB8 "	// ▸ (small right-pointing triangle) - folded state
#define PREFIX_UNFOLDED			"\xE2\x96\xBE "	// ▾ (small down-pointing triangle) - unfolded state

// UTF-8 encoding constants - kept for backward compatibility
#define UTF8_PREFIX_BYTES		4				// Length of PREFIX_FOLDED/PREFIX_UNFOLDED in bytes
#define MIN_GROUP_NAME_LENGTH	3				// Minimum length for a valid group name (prefix + 1 char)

// Divider identity stream name prefixes
#define DIVIDER_ID_PREFIX			"FD-"		// Fold state marker prefix
#define DIVIDER_ID_PREFIX_LEN		3			// Length of "FD-"
#define DIVIDER_HIERARCHY_PREFIX	"FD-H:"		// Hierarchy storage prefix
#define DIVIDER_HIERARCHY_PREFIX_LEN	5		// Length of "FD-H:"

// Buffer sizes
#define ERROR_BUFFER_SIZE		128			// Size of error message buffer
#define CFSTRING_BUFFER_SIZE	2048		// Size of CFString conversion buffer

// UTF-8 byte markers for prefix validation
#define UTF8_BYTE_0			0xE2			// First byte of ▸/▾
#define UTF8_BYTE_1			0x96			// Second byte of ▸/▾
#define UTF8_BYTE_2_FOLDED	0xB8			// Third byte of ▸ (folded)
#define UTF8_BYTE_2_UNFOLDED	0xBE		// Third byte of ▾ (unfolded)

// Structure for group info
typedef struct {
	std::string		hierarchy;		// e.g., "1", "1/A", "1/A/i"
	std::string		name;			// User-defined group name
	bool			isFolded;		// Current fold state
	AEGP_LayerH		layerH;			// Handle to group layer
	A_long			layerIndex;		// Index in comp
} FoldGroupInfo;

// Define missing constant if needed
#ifndef AEGP_LayerStream_ROOT_VECTORS_GROUP
	#define AEGP_LayerStream_ROOT_VECTORS_GROUP	((AEGP_LayerStream)0x0D)
#endif

//=============================================================================
// Global variables (extern for access across modules)
//=============================================================================

extern AEGP_PluginID	S_my_id;
extern SPBasicSuite		*sP;
extern A_long			S_idle_counter;

// Menu command IDs
extern AEGP_Command		S_cmd_create_divider;
extern AEGP_Command		S_cmd_fold_unfold;

//=============================================================================
// Utils - StringConv
//=============================================================================

#include "Utils/StringConv.h"

//=============================================================================
// Hierarchy - GroupParser & GroupBuilder
//=============================================================================

#include "Hierarchy/GroupParser.h"
#include "Hierarchy/GroupBuilder.h"

//=============================================================================
// Divider Identity & State Management
//=============================================================================

// Helper to find child by match name (safe for all group types)
A_Err FindStreamByMatchName(AEGP_SuiteHandler& suites, AEGP_StreamRefH parentH, const char* matchName, AEGP_StreamRefH* outStreamH);

// Get hierarchy from hidden FD-H: group for rename recovery
std::string GetHierarchyFromHiddenGroup(AEGP_SuiteHandler& suites, AEGP_LayerH layerH);

// Check if layer has divider identity (FD- prefix in stream)
bool HasDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH);

// Add identification group to layer with optional hierarchy
A_Err AddDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& hierarchy);

// Check if layer is a divider layer
bool IsDividerLayer(AEGP_SuiteHandler& suites, AEGP_LayerH layerH);

// Check if layer is a divider with known name (optimized version)
bool IsDividerLayerWithKnownName(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& name);

// Get fold state data stream from layer
A_Err GetFoldGroupDataStream(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, AEGP_StreamRefH* outStreamH, bool* outIsFolded = NULL);

// Set group fold state
A_Err SetGroupState(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, bool setFolded);

// Sync layer name with current fold state
A_Err SyncLayerName(AEGP_SuiteHandler& suites, AEGP_LayerH layerH);

// Check if divider is folded
bool IsDividerFolded(AEGP_SuiteHandler& suites, AEGP_LayerH layerH);

//=============================================================================
// Core Functionality
//=============================================================================

// Get active composition
A_Err GetActiveComp(AEGP_SuiteHandler& suites, AEGP_CompH* compH);

// Get layers that belong to this divider's group
A_Err GetGroupLayers(AEGP_SuiteHandler& suites, AEGP_CompH compH,
					A_long dividerIndex, const std::string& dividerHierarchy,
					std::vector<AEGP_LayerH>& groupLayers);

// Fold/unfold a divider
A_Err FoldDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH,
				 AEGP_LayerH dividerLayer, A_long dividerIndex, bool fold);

// Toggle a single divider
A_Err ToggleDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH,
				   AEGP_LayerH layerH, A_long layerIndex);

// Get all dividers in the composition
A_Err GetAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH,
					std::vector<std::pair<AEGP_LayerH, A_long> >& dividers);

// Check if any divider is selected
A_Err IsDividerSelected(AEGP_SuiteHandler& suites, AEGP_CompH compH, bool* result);

// Toggle selected dividers only
A_Err ToggleSelectedDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH);

// Toggle all dividers - unfold priority, fold if all unfolded
A_Err ToggleAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH);

//=============================================================================
// Commands
//=============================================================================

// Create divider command handler
A_Err DoCreateDivider(AEGP_SuiteHandler& suites);

// Fold/unfold command handler
A_Err DoFoldUnfold(AEGP_SuiteHandler& suites);

// Ensure shy mode is enabled in composition
// Returns A_Err_NONE on success, error code otherwise
A_Err EnsureShyModeEnabled(AEGP_SuiteHandler& suites);

//=============================================================================
// Platform-specific hooks
//=============================================================================

#ifdef AE_OS_WIN
	// Windows: Mouse hook for double-click detection
	extern HHOOK			S_mouse_hook;
	extern bool				S_double_click_pending;
	extern bool				S_suppress_next_action;
	extern CRITICAL_SECTION	S_cs;
	extern bool				S_cs_initialized;
	extern bool				S_is_divider_selected;

	// Process pending double-click
	A_Err ProcessDoubleClick();
#endif

#ifdef AE_OS_MAC
	// macOS: Event tap for double-click detection
	extern bool				S_pending_fold_action;
	extern double			S_last_left_down_event_ts;
	extern double			S_last_click_event_ts;
	extern CFMachPortRef	S_event_tap;
	extern CFRunLoopSourceRef S_event_tap_source;
	extern bool				S_event_tap_active;
	extern pthread_mutex_t	S_mac_state_mutex;
	extern bool				S_mac_divider_selected_for_input;
	extern std::string		S_mac_selected_divider_full_name;
	extern bool				S_mac_selected_divider_valid;
	extern double			S_mac_selected_divider_cached_at;
	extern bool				S_mac_should_warn_ax;
	extern bool				S_mac_warned_ax;
	extern bool				S_mac_ax_hit_test_usable;

	// Install macOS event tap
	void InstallMacEventTap();

	// Poll mouse state (fallback)
	void PollMouseState();

	// Check Accessibility trust
	bool MacAXTrusted();

	// Hit test for selected divider
	bool MacHitTestLooksLikeSelectedDivider(CGPoint globalPos);
#endif

//=============================================================================
// Entry Point
//=============================================================================

extern "C" DllExport AEGP_PluginInitFuncPrototype EntryPointFunc;

#endif // FOLDLAYERS_H
