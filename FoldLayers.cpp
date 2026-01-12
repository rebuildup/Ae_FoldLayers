/*******************************************************************/
/*                                                                 */
/*      FoldLayers - AEGP Plugin for After Effects                 */
/*      Recreates GM FoldLayers functionality                      */
/*      Developer: 361do_plugins                                   */
/*      https://github.com/rebuildup                               */
/*                                                                 */
/*      How it works:                                              */
/*      - "Create Group Divider" adds a divider layer              */
/*      - Layers below the divider (until next divider) are a group*/
/*      - Double-click on divider layer toggles fold/unfold        */
/*      - Folding = set shy flag + hideShyLayers                   */
/*                                                                 */
/*******************************************************************/

#include "FoldLayers.h"
#include <chrono>

// Global variables
static AEGP_PluginID	S_my_id				= 0;
static SPBasicSuite		*sP					= NULL;

// Menu command IDs
static AEGP_Command		S_cmd_create_divider	= 0;
static AEGP_Command		S_cmd_toggle_all		= 0;

// Double-click detection state
static AEGP_LayerH		S_last_selected_layer	= NULL;
static std::chrono::steady_clock::time_point S_last_selection_time;
static const int		DOUBLE_CLICK_MS			= 500;  // Double-click threshold

//=============================================================================
// Helper Functions
//=============================================================================

// Check if layer name starts with fold group prefix (▶ or ▼)
static bool IsDividerLayer(const std::string& name)
{
	if (name.length() >= 3) {
		unsigned char c0 = (unsigned char)name[0];
		unsigned char c1 = (unsigned char)name[1];
		unsigned char c2 = (unsigned char)name[2];
		// ▶ = E2 96 B6, ▼ = E2 96 BC
		if (c0 == 0xE2 && c1 == 0x96 && (c2 == 0xB6 || c2 == 0xBC)) {
			return true;
		}
	}
	return false;
}

// Check if divider is in folded state (▶)
static bool IsDividerFolded(const std::string& name)
{
	if (name.length() >= 3) {
		unsigned char c0 = (unsigned char)name[0];
		unsigned char c1 = (unsigned char)name[1];
		unsigned char c2 = (unsigned char)name[2];
		// ▶ = E2 96 B6 (folded)
		if (c0 == 0xE2 && c1 == 0x96 && c2 == 0xB6) {
			return true;
		}
	}
	return false;
}

// Get name without prefix
static std::string GetDividerName(const std::string& fullName)
{
	// Skip prefix (4 bytes: 3 for triangle + 1 for space)
	if (fullName.length() <= 4) return "Group Divider";
	return fullName.substr(4);
}

// Build divider layer name
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
	AEGP_MemHandle nameH = NULL;
	AEGP_MemHandle sourceH = NULL;
	A_UTF16Char* nameP = NULL;
	
	ERR(suites.LayerSuite9()->AEGP_GetLayerName(S_my_id, layerH, &nameH, &sourceH));
	
	if (!err && nameH) {
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
	AEGP_ItemType itemType;
	
	ERR(suites.ItemSuite9()->AEGP_GetActiveItem(&itemH));
	
	if (itemH) {
		ERR(suites.ItemSuite9()->AEGP_GetItemType(itemH, &itemType));
		if (!err && itemType == AEGP_ItemType_COMP) {
			ERR(suites.CompSuite11()->AEGP_GetCompFromItem(itemH, compH));
		}
	}
	
	return err;
}

// Get layers that belong to a divider (from divider+1 to next divider or end)
static A_Err GetGroupLayers(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                            A_long dividerIndex, std::vector<AEGP_LayerH>& groupLayers)
{
	A_Err err = A_Err_NONE;
	A_long numLayers = 0;
	
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	// Start from layer after divider
	for (A_long i = dividerIndex + 1; i < numLayers && !err; i++) {
		AEGP_LayerH layer;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer) {
			std::string name;
			ERR(GetLayerNameStr(suites, layer, name));
			
			// Stop at next divider
			if (!err && IsDividerLayer(name)) {
				break;
			}
			
			groupLayers.push_back(layer);
		}
	}
	
	return err;
}

// Toggle fold state for a divider
static A_Err ToggleDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                           AEGP_LayerH dividerLayer, A_long dividerIndex)
{
	A_Err err = A_Err_NONE;
	
	std::string dividerName;
	ERR(GetLayerNameStr(suites, dividerLayer, dividerName));
	if (err) return err;
	
	bool isFolded = IsDividerFolded(dividerName);
	std::string baseName = GetDividerName(dividerName);
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup(isFolded ? "Unfold Group" : "Fold Group"));
	
	// Toggle divider name prefix
	std::string newName = BuildDividerName(!isFolded, baseName);
	ERR(SetLayerNameStr(suites, dividerLayer, newName));
	
	// Get layers in this group
	std::vector<AEGP_LayerH> groupLayers;
	ERR(GetGroupLayers(suites, compH, dividerIndex, groupLayers));
	
	// Toggle shy flag on group layers
	for (AEGP_LayerH layer : groupLayers) {
		if (!err) {
			// If folding (was unfolded), set shy
			// If unfolding (was folded), clear shy
			ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(layer, AEGP_LayerFlag_SHY, !isFolded ? TRUE : FALSE));
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
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
	
	// Get currently selected layers to determine insert position
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	A_long insertIndex = 0;  // Default to top
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
		
		// If a layer is selected, insert above it
		if (numSelected >= 1) {
			AEGP_CollectionItemV2 item;
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
			if (!err && item.type == AEGP_CollectionItemType_LAYER) {
				ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &insertIndex));
			}
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Create Group Divider"));
	
	// Create null layer as divider
	AEGP_LayerH newLayer = NULL;
	
	ERR(suites.CompSuite11()->AEGP_CreateNullInComp(
		NULL,      // name (set later)
		compH,     // comp
		NULL,      // duration (comp duration)
		&newLayer
	));
	
	if (!err && newLayer) {
		// Set layer name with expanded prefix (unfold state initially)
		std::string dividerName = BuildDividerName(false, "Group Divider");
		ERR(SetLayerNameStr(suites, newLayer, dividerName));
		
		// Move to insert position if needed
		if (insertIndex > 0) {
			ERR(suites.LayerSuite9()->AEGP_ReorderLayer(newLayer, insertIndex));
		}
		
		// Set as guide layer (optional - for visual distinction)
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(newLayer, AEGP_LayerFlag_GUIDE_LAYER, TRUE));
		
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "Group Divider created. Double-click to fold/unfold.");
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	return err;
}

static A_Err DoToggleAll(AEGP_SuiteHandler& suites, bool fold)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_NONE;
	
	// Check if any divider is selected
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	std::vector<std::pair<AEGP_LayerH, A_long>> selectedDividers;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
		
		for (A_u_long i = 0; i < numSelected && !err; i++) {
			AEGP_CollectionItemV2 item;
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, i, &item));
			if (!err && item.type == AEGP_CollectionItemType_LAYER) {
				std::string name;
				ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
				if (!err && IsDividerLayer(name)) {
					A_long idx;
					ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &idx));
					selectedDividers.push_back({item.u.layer.layerH, idx});
				}
			}
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	// If dividers are selected, toggle only those
	// If no dividers selected, toggle all dividers in comp
	if (selectedDividers.empty()) {
		// Get all dividers in comp
		A_long numLayers = 0;
		ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
		
		for (A_long i = 0; i < numLayers && !err; i++) {
			AEGP_LayerH layer;
			ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
			if (!err && layer) {
				std::string name;
				ERR(GetLayerNameStr(suites, layer, name));
				if (!err && IsDividerLayer(name)) {
					selectedDividers.push_back({layer, i});
				}
			}
		}
	}
	
	if (selectedDividers.empty()) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "No dividers found.");
		return A_Err_NONE;
	}
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup(fold ? "Fold All" : "Unfold All"));
	
	for (auto& pair : selectedDividers) {
		AEGP_LayerH dividerLayer = pair.first;
		A_long dividerIndex = pair.second;
		std::string name;
		ERR(GetLayerNameStr(suites, dividerLayer, name));
		if (!err) {
			bool isFolded = IsDividerFolded(name);
			
			// Only toggle if not in desired state
			if (isFolded != fold) {
				std::string baseName = GetDividerName(name);
				std::string newName = BuildDividerName(fold, baseName);
				ERR(SetLayerNameStr(suites, dividerLayer, newName));
				
				// Get and update group layers
				std::vector<AEGP_LayerH> groupLayers;
				ERR(GetGroupLayers(suites, compH, dividerIndex, groupLayers));
				for (AEGP_LayerH layer : groupLayers) {
					if (!err) {
						ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(layer, AEGP_LayerFlag_SHY, fold ? TRUE : FALSE));
					}
				}
			}
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, fold ? "All groups folded." : "All groups unfolded.");
	
	return err;
}

//=============================================================================
// Idle Hook - Detect double-click on divider layers
//=============================================================================

static A_Err IdleHook(
	AEGP_GlobalRefcon	plugin_refconPV,
	AEGP_IdleRefcon		refconPV,
	A_long				*max_sleepPL)
{
	A_Err err = A_Err_NONE;
	AEGP_SuiteHandler suites(sP);
	
	AEGP_CompH compH = NULL;
	ERR(GetActiveComp(suites, &compH));
	
	if (!err && compH) {
		AEGP_Collection2H collectionH = NULL;
		A_u_long numSelected = 0;
		
		ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
		if (!err && collectionH) {
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
			
			if (numSelected == 1) {
				AEGP_CollectionItemV2 item;
				ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
				
				if (!err && item.type == AEGP_CollectionItemType_LAYER) {
					AEGP_LayerH currentLayer = item.u.layer.layerH;
					
					// Check if this is a divider layer
					std::string name;
					ERR(GetLayerNameStr(suites, currentLayer, name));
					
					if (!err && IsDividerLayer(name)) {
						auto now = std::chrono::steady_clock::now();
						auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
							now - S_last_selection_time).count();
						
						// If same layer selected again within threshold, it's a double-click
						if (currentLayer == S_last_selected_layer && elapsed < DOUBLE_CLICK_MS) {
							// Toggle this divider
							A_long idx;
							ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(currentLayer, &idx));
							if (!err) {
								ERR(ToggleDivider(suites, compH, currentLayer, idx));
							}
							
							// Reset to prevent triple-click toggle
							S_last_selected_layer = NULL;
						} else {
							// First click - remember it
							S_last_selected_layer = currentLayer;
							S_last_selection_time = now;
						}
					} else {
						// Not a divider, reset
						S_last_selected_layer = NULL;
					}
				}
			} else {
				// Multiple or no selection, reset
				S_last_selected_layer = NULL;
			}
			
			suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		}
	}
	
	*max_sleepPL = 100;  // Check every 100ms
	return err;
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
	
	if (S_cmd_create_divider) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_create_divider));
	}
	if (S_cmd_toggle_all) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_toggle_all));
	}
	
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
	
	if (command == S_cmd_create_divider) {
		err = DoCreateDivider(suites);
		*handledPB = TRUE;
	}
	else if (command == S_cmd_toggle_all) {
		// Toggle based on current state of first selected divider
		AEGP_CompH compH = NULL;
		ERR(GetActiveComp(suites, &compH));
		
		if (compH) {
			AEGP_Collection2H collectionH = NULL;
			A_u_long numSelected = 0;
			bool shouldFold = true;  // Default to fold
			
			ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
			if (!err && collectionH) {
				ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
				
				if (numSelected > 0) {
					AEGP_CollectionItemV2 item;
					ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
					if (!err && item.type == AEGP_CollectionItemType_LAYER) {
						std::string name;
						ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
						if (!err && IsDividerLayer(name)) {
							// If first selected divider is unfolded, fold. Otherwise unfold.
							shouldFold = !IsDividerFolded(name);
						}
					}
				}
				
				suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
			}
			
			err = DoToggleAll(suites, shouldFold);
		}
		*handledPB = TRUE;
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
	A_Err err = A_Err_NONE, err2 = A_Err_NONE;
	
	sP = pica_basicP;
	S_my_id = aegp_plugin_id;
	
	// Initialize double-click detection
	S_last_selected_layer = NULL;
	S_last_selection_time = std::chrono::steady_clock::now();
	
	AEGP_SuiteHandler suites(sP);
	
	// Register menu commands
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_create_divider));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_toggle_all));
	
	if (!err) {
		// Add to Layer menu
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_create_divider,
			"Create Group Divider",
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_toggle_all,
			"Fold/Unfold Groups",
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		// Register hooks
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
		
		// Register Idle hook for double-click detection
		ERR(suites.RegisterSuite5()->AEGP_RegisterIdleHook(
			S_my_id,
			IdleHook,
			NULL));
	}
	
	if (err) {
		err2 = suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "Error registering FoldLayers commands.");
	}
	
	return err;
}
