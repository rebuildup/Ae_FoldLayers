/*******************************************************************/
/*                                                                 */
/*      FoldLayers - AEGP Plugin for After Effects                 */
/*      Recreates GM FoldLayers functionality                      */
/*      Developer: 361do_plugins                                   */
/*      https://github.com/rebuildup                               */
/*                                                                 */
/*      Based on GM FoldLayers analysis:                           */
/*      - Uses shape layer (no path) as divider                    */
/*      - Divider is set to VIDEO_OFF (invisible)                  */
/*      - Uses ▸ (folded) and ▾ (unfolded) characters              */
/*      - Windows: Mouse hook for real double-click detection      */
/*      - macOS: Fallback to selection monitoring                  */
/*                                                                 */
/*******************************************************************/

#include "FoldLayers.h"

#ifdef AE_OS_WIN
#include <windows.h>
#endif

// Global variables
static AEGP_PluginID	S_my_id				= 0;
static SPBasicSuite		*sP					= NULL;

// Menu command IDs
static AEGP_Command		S_cmd_create_divider	= 0;
static AEGP_Command		S_cmd_fold_unfold		= 0;

#ifdef AE_OS_WIN
// Windows: Mouse hook for double-click detection
static HHOOK			S_mouse_hook			= NULL;
static bool				S_double_click_pending	= false;
static CRITICAL_SECTION	S_cs;
static bool				S_cs_initialized		= false;
#endif

// Idle hook state
static A_long			S_idle_counter			= 0;

//=============================================================================
// Helper Functions
//=============================================================================

static bool IsDividerLayer(const std::string& name)
{
	if (name.length() >= 3) {
		unsigned char c0 = (unsigned char)name[0];
		unsigned char c1 = (unsigned char)name[1];
		unsigned char c2 = (unsigned char)name[2];
		// ▸ = E2 96 B8, ▾ = E2 96 BE
		if (c0 == 0xE2 && c1 == 0x96 && (c2 == 0xB8 || c2 == 0xBE)) {
			return true;
		}
	}
	return false;
}

static bool IsDividerFolded(const std::string& name)
{
	if (name.length() >= 3) {
		unsigned char c0 = (unsigned char)name[0];
		unsigned char c1 = (unsigned char)name[1];
		unsigned char c2 = (unsigned char)name[2];
		// ▸ = E2 96 B8 (folded)
		if (c0 == 0xE2 && c1 == 0x96 && c2 == 0xB8) {
			return true;
		}
	}
	return false;
}

static std::string GetDividerName(const std::string& fullName)
{
	if (fullName.length() <= 4) return "Group Divider";
	return fullName.substr(4);
}

static std::string BuildDividerName(bool folded, const std::string& name)
{
	std::string result = folded ? PREFIX_FOLDED : PREFIX_UNFOLDED;
	result += name;
	return result;
}

//=============================================================================
// Layer Name Utilities
//=============================================================================

static A_Err GetLayerNameStr(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, std::string& name)
{
	A_Err err = A_Err_NONE;
	
	if (!layerH) return A_Err_STRUCT;
	
	AEGP_MemHandle nameH = NULL;
	AEGP_MemHandle sourceH = NULL;
	
	ERR(suites.LayerSuite9()->AEGP_GetLayerName(S_my_id, layerH, &nameH, &sourceH));
	
	if (!err && nameH) {
		A_UTF16Char* nameP = NULL;
		ERR(suites.MemorySuite1()->AEGP_LockMemHandle(nameH, (void**)&nameP));
		if (!err && nameP) {
			std::string result;
			while (*nameP) {
				if (*nameP < 0x80) {
					result += (char)*nameP;
				} else if (*nameP < 0x800) {
					result += (char)(0xC0 | (*nameP >> 6));
					result += (char)(0x80 | (*nameP & 0x3F));
				} else {
					result += (char)(0xE0 | (*nameP >> 12));
					result += (char)(0x80 | ((*nameP >> 6) & 0x3F));
					result += (char)(0x80 | (*nameP & 0x3F));
				}
				nameP++;
			}
			name = result;
			suites.MemorySuite1()->AEGP_UnlockMemHandle(nameH);
		}
		suites.MemorySuite1()->AEGP_FreeMemHandle(nameH);
	}
	if (sourceH) {
		suites.MemorySuite1()->AEGP_FreeMemHandle(sourceH);
	}
	
	return err;
}

static A_Err SetLayerNameStr(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& name)
{
	A_Err err = A_Err_NONE;
	
	if (!layerH) return A_Err_STRUCT;
	
	std::vector<A_UTF16Char> utf16;
	const unsigned char* p = (const unsigned char*)name.c_str();
	
	while (*p) {
		if (*p < 0x80) {
			utf16.push_back(*p++);
		} else if ((*p & 0xE0) == 0xC0) {
			A_UTF16Char c = (*p++ & 0x1F) << 6;
			if (*p) c |= (*p++ & 0x3F);
			utf16.push_back(c);
		} else if ((*p & 0xF0) == 0xE0) {
			A_UTF16Char c = (*p++ & 0x0F) << 12;
			if (*p) c |= (*p++ & 0x3F) << 6;
			if (*p) c |= (*p++ & 0x3F);
			utf16.push_back(c);
		} else {
			p++;
		}
	}
	utf16.push_back(0);
	
	ERR(suites.LayerSuite9()->AEGP_SetLayerName(layerH, utf16.data()));
	
	return err;
}

//=============================================================================
// Core Functionality
//=============================================================================

static A_Err GetActiveComp(AEGP_SuiteHandler& suites, AEGP_CompH* compH)
{
	A_Err err = A_Err_NONE;
	AEGP_ItemH itemH = NULL;
	AEGP_ItemType itemType = AEGP_ItemType_NONE;
	
	*compH = NULL;
	
	ERR(suites.ItemSuite9()->AEGP_GetActiveItem(&itemH));
	
	if (!err && itemH) {
		ERR(suites.ItemSuite9()->AEGP_GetItemType(itemH, &itemType));
		if (!err && itemType == AEGP_ItemType_COMP) {
			ERR(suites.CompSuite11()->AEGP_GetCompFromItem(itemH, compH));
		}
	}
	
	return err;
}

static A_Err GetGroupLayers(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                            A_long dividerIndex, std::vector<AEGP_LayerH>& groupLayers)
{
	A_Err err = A_Err_NONE;
	A_long numLayers = 0;
	
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	for (A_long i = dividerIndex + 1; i < numLayers && !err; i++) {
		AEGP_LayerH layer = NULL;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer) {
			std::string name;
			ERR(GetLayerNameStr(suites, layer, name));
			
			if (!err && IsDividerLayer(name)) {
				break;
			}
			
			groupLayers.push_back(layer);
		}
	}
	
	return err;
}

static A_Err FoldDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                         AEGP_LayerH dividerLayer, A_long dividerIndex, bool fold)
{
	A_Err err = A_Err_NONE;
	
	if (!dividerLayer || !compH) return A_Err_STRUCT;
	
	std::string dividerName;
	ERR(GetLayerNameStr(suites, dividerLayer, dividerName));
	if (err) return err;
	
	std::string baseName = GetDividerName(dividerName);
	std::string newName = BuildDividerName(fold, baseName);
	ERR(SetLayerNameStr(suites, dividerLayer, newName));
	
	std::vector<AEGP_LayerH> groupLayers;
	ERR(GetGroupLayers(suites, compH, dividerIndex, groupLayers));
	
	for (size_t i = 0; i < groupLayers.size() && !err; i++) {
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(groupLayers[i], AEGP_LayerFlag_SHY, fold ? TRUE : FALSE));
	}
	
	return err;
}

static A_Err ToggleSelectedDividers(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_NONE;
	
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
		
		if (!err && numSelected > 0) {
			ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Fold/Unfold"));
			
			for (A_u_long i = 0; i < numSelected && !err; i++) {
				AEGP_CollectionItemV2 item;
				ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, i, &item));
				if (!err && item.type == AEGP_CollectionItemType_LAYER) {
					std::string name;
					ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
					if (!err && IsDividerLayer(name)) {
						bool isFolded = IsDividerFolded(name);
						A_long idx = 0;
						ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &idx));
						if (!err) {
							ERR(FoldDivider(suites, compH, item.u.layer.layerH, idx, !isFolded));
						}
					}
				}
			}
			
			ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	return err;
}

//=============================================================================
// Command Handlers
//=============================================================================

static A_Err DoCreateDivider(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "Please open a composition first.");
		return A_Err_NONE;
	}
	
	A_long insertIndex = 0;
	
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
		
		if (!err && numSelected >= 1) {
			AEGP_CollectionItemV2 item;
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
			if (!err && item.type == AEGP_CollectionItemType_LAYER) {
				ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &insertIndex));
			}
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		collectionH = NULL;
	}
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Create Group Divider"));
	
	// Create SHAPE layer (vector layer)
	AEGP_LayerH newLayer = NULL;
	ERR(suites.CompSuite11()->AEGP_CreateVectorLayerInComp(compH, &newLayer));
	
	if (!err && newLayer) {
		// Set layer name with expanded prefix (▾ = unfolded state)
		std::string dividerName = BuildDividerName(false, "Group Divider");
		ERR(SetLayerNameStr(suites, newLayer, dividerName));
		
		// Move to insert position
		if (insertIndex > 0) {
			ERR(suites.LayerSuite9()->AEGP_ReorderLayer(newLayer, insertIndex));
		}
		
		// Set VIDEO OFF (invisible)
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(newLayer, AEGP_LayerFlag_VIDEO_ACTIVE, FALSE));
		
		// Set label to 0 (None)
		ERR(suites.LayerSuite9()->AEGP_SetLayerLabel(newLayer, 0));
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	return err;
}

static A_Err DoFoldUnfold(AEGP_SuiteHandler& suites)
{
	return ToggleSelectedDividers(suites);
}

//=============================================================================
// Windows: Mouse hook for double-click detection
//=============================================================================

#ifdef AE_OS_WIN

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0 && wParam == WM_LBUTTONDBLCLK) {
		// Double-click detected!
		EnterCriticalSection(&S_cs);
		S_double_click_pending = true;
		LeaveCriticalSection(&S_cs);
	}
	return CallNextHookEx(S_mouse_hook, nCode, wParam, lParam);
}

#endif

//=============================================================================
// Idle Hook - Process pending double-clicks
//=============================================================================

static A_Err IdleHook(
	AEGP_GlobalRefcon	plugin_refconPV,
	AEGP_IdleRefcon		refconPV,
	A_long				*max_sleepPL)
{
	A_Err err = A_Err_NONE;
	
	S_idle_counter++;
	
#ifdef AE_OS_WIN
	// Check if a double-click is pending
	bool dblclick = false;
	EnterCriticalSection(&S_cs);
	if (S_double_click_pending) {
		dblclick = true;
		S_double_click_pending = false;
	}
	LeaveCriticalSection(&S_cs);
	
	if (dblclick) {
		// A double-click just happened - check if a divider is selected
		AEGP_SuiteHandler suites(sP);
		
		AEGP_CompH compH = NULL;
		ERR(GetActiveComp(suites, &compH));
		
		if (!err && compH) {
			AEGP_Collection2H collectionH = NULL;
			A_u_long numSelected = 0;
			
			ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
			if (!err && collectionH) {
				ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
				
				if (!err && numSelected == 1) {
					AEGP_CollectionItemV2 item;
					ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
					
					if (!err && item.type == AEGP_CollectionItemType_LAYER) {
						std::string name;
						ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
						
						if (!err && IsDividerLayer(name)) {
							// Divider is selected and double-clicked - toggle!
							bool isFolded = IsDividerFolded(name);
							A_long idx = 0;
							ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &idx));
							if (!err) {
								ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Fold/Unfold"));
								ERR(FoldDivider(suites, compH, item.u.layer.layerH, idx, !isFolded));
								ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
							}
						}
					}
				}
				
				suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
			}
		}
	}
#endif
	
	*max_sleepPL = 50;
	return A_Err_NONE;
}

//=============================================================================
// Menu Hooks
//=============================================================================

static A_Err UpdateMenuHook(
	AEGP_GlobalRefcon		plugin_refconPV,
	AEGP_UpdateMenuRefcon	refconPV,
	AEGP_WindowType			active_window)
{
	A_Err err = A_Err_NONE;
	AEGP_SuiteHandler suites(sP);
	
	ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_create_divider));
	ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_fold_unfold));
	
	return err;
}

static A_Err CommandHook(
	AEGP_GlobalRefcon	plugin_refconPV,
	AEGP_CommandRefcon	refconPV,
	AEGP_Command		command,
	AEGP_HookPriority	hook_priority,
	A_Boolean			already_handledB,
	A_Boolean			*handledPB)
{
	A_Err err = A_Err_NONE;
	AEGP_SuiteHandler suites(sP);
	
	try {
		if (command == S_cmd_create_divider) {
			err = DoCreateDivider(suites);
			*handledPB = TRUE;
		}
		else if (command == S_cmd_fold_unfold) {
			err = DoFoldUnfold(suites);
			*handledPB = TRUE;
		}
	}
	catch (...) {
		err = A_Err_GENERIC;
	}
	
	return err;
}

//=============================================================================
// Entry Point
//=============================================================================

A_Err EntryPointFunc(
	struct SPBasicSuite		*pica_basicP,
	A_long					major_versionL,
	A_long					minor_versionL,
	AEGP_PluginID			aegp_plugin_id,
	AEGP_GlobalRefcon		*global_refconP)
{
	A_Err err = A_Err_NONE;
	
	sP = pica_basicP;
	S_my_id = aegp_plugin_id;
	
	S_idle_counter = 0;
	
#ifdef AE_OS_WIN
	// Initialize critical section and mouse hook
	InitializeCriticalSection(&S_cs);
	S_cs_initialized = true;
	S_double_click_pending = false;
	
	// Install mouse hook to detect double-clicks
	S_mouse_hook = SetWindowsHookEx(WH_MOUSE, MouseProc, NULL, GetCurrentThreadId());
#endif
	
	AEGP_SuiteHandler suites(sP);
	
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_create_divider));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_fold_unfold));
	
	if (!err) {
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_create_divider,
			"Create Group Divider",
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_fold_unfold,
			"Fold/Unfold",
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.RegisterSuite5()->AEGP_RegisterCommandHook(
			S_my_id,
			AEGP_HP_BeforeAE,
			AEGP_Command_ALL,
			CommandHook,
			NULL));
		
		ERR(suites.RegisterSuite5()->AEGP_RegisterUpdateMenuHook(
			S_my_id,
			UpdateMenuHook,
			NULL));
		
		ERR(suites.RegisterSuite5()->AEGP_RegisterIdleHook(
			S_my_id,
			IdleHook,
			NULL));
	}
	
	return err;
}
