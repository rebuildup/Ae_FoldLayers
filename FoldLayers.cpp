/*******************************************************************/
/*                                                                 */
/*      FoldLayers - AEGP Plugin for After Effects                 */
/*      Recreates GM FoldLayers functionality with extensions      */
/*      Developer: 361do_plugins                                   */
/*      https://github.com/rebuildup                               */
/*                                                                 */
/*      Features:                                                  */
/*      - Create fold groups from selected layers                  */
/*      - Fold/Unfold groups (toggle shy flag)                     */
/*      - Nested groups support: ▼(1), ▼(1/A), ▼(1/A/i)            */
/*      - Fold All / Unfold All commands                           */
/*                                                                 */
/*******************************************************************/

#include "FoldLayers.h"

// Global variables
static AEGP_PluginID	S_my_id				= 0;
static SPBasicSuite		*sP					= NULL;

// Menu command IDs
static AEGP_Command		S_cmd_create_group	= 0;
static AEGP_Command		S_cmd_fold_unfold	= 0;
static AEGP_Command		S_cmd_delete_group	= 0;
static AEGP_Command		S_cmd_fold_all		= 0;
static AEGP_Command		S_cmd_unfold_all	= 0;

//=============================================================================
// Helper Functions
//=============================================================================

// Check if layer name starts with fold group prefix
static bool IsFoldGroupLayer(const std::string& name)
{
	// Check for ▶ or ▼ prefix (UTF-8: E2 96 B6 or E2 96 BC)
	if (name.length() >= 3) {
		unsigned char c0 = (unsigned char)name[0];
		unsigned char c1 = (unsigned char)name[1];
		unsigned char c2 = (unsigned char)name[2];
		if (c0 == 0xE2 && c1 == 0x96 && (c2 == 0xB6 || c2 == 0xBC)) {
			return true;
		}
	}
	return false;
}

// Check if layer is in folded state
static bool IsGroupFolded(const std::string& name)
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

// Parse hierarchy from group name: "▼(1/A) Name" -> "1/A"
static std::string ParseHierarchy(const std::string& name)
{
	size_t start = name.find('(');
	size_t end = name.find(')');
	
	if (start != std::string::npos && end != std::string::npos && end > start) {
		return name.substr(start + 1, end - start - 1);
	}
	return "";
}

// Parse group name without prefix and hierarchy
static std::string ParseGroupName(const std::string& fullName)
{
	// Skip prefix (4 bytes: 3 for triangle + 1 for space)
	if (fullName.length() <= 4) return "FoldGroup";
	
	std::string rest = fullName.substr(4);
	
	// Skip hierarchy if present
	size_t endParen = rest.find(") ");
	if (endParen != std::string::npos) {
		return rest.substr(endParen + 2);
	}
	return rest;
}

// Generate next hierarchy level
static std::string GenerateChildHierarchy(const std::string& parentHierarchy)
{
	if (parentHierarchy.empty()) {
		return "1";
	}
	
	int depth = 1;
	for (char c : parentHierarchy) {
		if (c == '/') depth++;
	}
	
	char nextId;
	if (depth == 1) {
		nextId = 'A';
	} else {
		nextId = 'a';
	}
	
	return parentHierarchy + "/" + nextId;
}

// Build group layer name
static std::string BuildGroupName(bool folded, const std::string& hierarchy, const std::string& name)
{
	std::string result = folded ? PREFIX_FOLDED : PREFIX_UNFOLDED;
	
	if (!hierarchy.empty()) {
		result += "(";
		result += hierarchy;
		result += ") ";
	}
	
	result += name;
	return result;
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
// Command Handlers
//=============================================================================

static A_Err DoCreateGroup(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_GENERIC;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
	}
	
	if (numSelected == 0) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, FLSTR(StrID_Error_NoSelection));
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	std::string parentHierarchy = "";
	if (numSelected == 1) {
		AEGP_CollectionItemV2 item;
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
		if (!err && item.type == AEGP_CollectionItemType_LAYER) {
			std::string layerName;
			ERR(GetLayerNameStr(suites, item.u.layer.layerH, layerName));
			if (!err && IsFoldGroupLayer(layerName)) {
				parentHierarchy = ParseHierarchy(layerName);
			}
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Create Fold Group"));
	
	// Find highest existing hierarchy number at current level
	std::string newHierarchy;
	if (parentHierarchy.empty()) {
		A_long numLayers = 0;
		int maxNum = 0;
		ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
		
		for (A_long i = 0; i < numLayers && !err; i++) {
			AEGP_LayerH checkLayer;
			ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &checkLayer));
			if (!err && checkLayer) {
				std::string checkName;
				ERR(GetLayerNameStr(suites, checkLayer, checkName));
				if (!err && IsFoldGroupLayer(checkName)) {
					std::string hier = ParseHierarchy(checkName);
					if (!hier.empty() && hier.find('/') == std::string::npos) {
						try {
							int num = std::stoi(hier);
							if (num > maxNum) maxNum = num;
						} catch (...) {}
					}
				}
			}
		}
		newHierarchy = std::to_string(maxNum + 1);
	} else {
		newHierarchy = GenerateChildHierarchy(parentHierarchy);
	}
	
	// Get first selected layer to use as reference for creating group layer
	AEGP_CollectionItemV2 firstItem;
	AEGP_LayerH firstLayerH = NULL;
	A_long firstLayerIndex = 0;
	
	ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &firstItem));
	if (!err && firstItem.type == AEGP_CollectionItemType_LAYER) {
		firstLayerH = firstItem.u.layer.layerH;
		ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(firstLayerH, &firstLayerIndex));
	}
	
	// Create a NULL object using CompSuite
	// Signature: AEGP_CreateNullInComp(const A_UTF16Char *nameZ0, AEGP_CompH compH, const A_Time *durationPT0, AEGP_LayerH *new_null_layerPH)
	AEGP_LayerH newLayer = NULL;
	ERR(suites.CompSuite11()->AEGP_CreateNullInComp(
		NULL,      // name (set later via SetLayerName)
		compH,     // comp handle
		NULL,      // duration (null = comp duration)
		&newLayer  // result
	));
	
	if (!err && newLayer) {
		// Set layer name with fold prefix
		std::string groupName = BuildGroupName(false, newHierarchy, "FoldGroup");
		ERR(SetLayerNameStr(suites, newLayer, groupName));
		
		// Move group layer to be above the first selected layer
		if (firstLayerIndex > 0) {
			ERR(suites.LayerSuite9()->AEGP_ReorderLayer(newLayer, firstLayerIndex));
		}
		
		// Set shy flag on all selected layers and parent them
		for (A_u_long i = 0; i < numSelected && !err; i++) {
			AEGP_CollectionItemV2 item;
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, i, &item));
			if (!err && item.type == AEGP_CollectionItemType_LAYER) {
				ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(
					item.u.layer.layerH, 
					AEGP_LayerFlag_SHY, 
					TRUE
				));
				
				ERR(suites.LayerSuite9()->AEGP_SetLayerParent(
					item.u.layer.layerH,
					newLayer
				));
			}
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	if (collectionH) {
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, FLSTR(StrID_GroupCreated));
	
	return err;
}

static A_Err DoFoldUnfold(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_GENERIC;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
	}
	
	if (numSelected == 0) {
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	AEGP_CollectionItemV2 item;
	ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
	if (err || item.type != AEGP_CollectionItemType_LAYER) {
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	AEGP_LayerH groupLayer = item.u.layer.layerH;
	std::string layerName;
	ERR(GetLayerNameStr(suites, groupLayer, layerName));
	
	if (!IsFoldGroupLayer(layerName)) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, FLSTR(StrID_Error_NotAGroup));
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	bool currentlyFolded = IsGroupFolded(layerName);
	std::string hierarchy = ParseHierarchy(layerName);
	std::string groupName = ParseGroupName(layerName);
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup(currentlyFolded ? "Unfold Group" : "Fold Group"));
	
	// Toggle fold state - update layer name prefix
	std::string newName = BuildGroupName(!currentlyFolded, hierarchy, groupName);
	ERR(SetLayerNameStr(suites, groupLayer, newName));
	
	// Toggle shy flags on child layers
	A_long numLayers = 0;
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	for (A_long i = 0; i < numLayers && !err; i++) {
		AEGP_LayerH checkLayer;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &checkLayer));
		if (!err && checkLayer && checkLayer != groupLayer) {
			AEGP_LayerH parentLayer = NULL;
			ERR(suites.LayerSuite9()->AEGP_GetLayerParent(checkLayer, &parentLayer));
			
			if (!err && parentLayer == groupLayer) {
				if (!currentlyFolded) {
					// Folding: set shy
					ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(checkLayer, AEGP_LayerFlag_SHY, TRUE));
				} else {
					// Unfolding: clear shy
					ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(checkLayer, AEGP_LayerFlag_SHY, FALSE));
				}
			}
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	if (collectionH) {
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, 
		currentlyFolded ? FLSTR(StrID_Unfolded) : FLSTR(StrID_Folded));
	
	return err;
}

static A_Err DoDeleteGroup(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_GENERIC;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
	}
	
	if (numSelected == 0) {
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	AEGP_CollectionItemV2 item;
	ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
	if (err || item.type != AEGP_CollectionItemType_LAYER) {
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	AEGP_LayerH groupLayer = item.u.layer.layerH;
	std::string layerName;
	ERR(GetLayerNameStr(suites, groupLayer, layerName));
	
	if (!IsFoldGroupLayer(layerName)) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, FLSTR(StrID_Error_NotAGroup));
		if (collectionH) suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		return A_Err_NONE;
	}
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Delete Fold Group"));
	
	// Unparent and unhide all child layers
	A_long numLayers = 0;
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	for (A_long i = 0; i < numLayers && !err; i++) {
		AEGP_LayerH checkLayer;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &checkLayer));
		if (!err && checkLayer && checkLayer != groupLayer) {
			AEGP_LayerH parentLayer = NULL;
			ERR(suites.LayerSuite9()->AEGP_GetLayerParent(checkLayer, &parentLayer));
			
			if (!err && parentLayer == groupLayer) {
				ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(checkLayer, AEGP_LayerFlag_SHY, FALSE));
				ERR(suites.LayerSuite9()->AEGP_SetLayerParent(checkLayer, NULL));
			}
		}
	}
	
	// Delete the group layer
	ERR(suites.LayerSuite9()->AEGP_DeleteLayer(groupLayer));
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	if (collectionH) {
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, FLSTR(StrID_GroupDeleted));
	
	return err;
}

static A_Err DoFoldAll(AEGP_SuiteHandler& suites, bool fold)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_GENERIC;
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup(fold ? "Fold All Groups" : "Unfold All Groups"));
	
	A_long numLayers = 0;
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	// First pass: find all group layers and update names
	std::vector<AEGP_LayerH> groupLayers;
	for (A_long i = 0; i < numLayers && !err; i++) {
		AEGP_LayerH layer;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer) {
			std::string name;
			ERR(GetLayerNameStr(suites, layer, name));
			if (!err && IsFoldGroupLayer(name)) {
				groupLayers.push_back(layer);
				
				bool currentFolded = IsGroupFolded(name);
				if (currentFolded != fold) {
					std::string hier = ParseHierarchy(name);
					std::string groupName = ParseGroupName(name);
					std::string newName = BuildGroupName(fold, hier, groupName);
					ERR(SetLayerNameStr(suites, layer, newName));
				}
			}
		}
	}
	
	// Second pass: update shy flags on child layers
	for (AEGP_LayerH groupLayer : groupLayers) {
		for (A_long i = 0; i < numLayers && !err; i++) {
			AEGP_LayerH layer;
			ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
			if (!err && layer) {
				AEGP_LayerH parent = NULL;
				ERR(suites.LayerSuite9()->AEGP_GetLayerParent(layer, &parent));
				if (!err && parent == groupLayer) {
					ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(layer, AEGP_LayerFlag_SHY, fold ? TRUE : FALSE));
				}
			}
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, 
		fold ? FLSTR(StrID_AllFolded) : FLSTR(StrID_AllUnfolded));
	
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
	
	if (S_cmd_create_group) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_create_group));
	}
	if (S_cmd_fold_unfold) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_fold_unfold));
	}
	if (S_cmd_delete_group) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_delete_group));
	}
	if (S_cmd_fold_all) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_fold_all));
	}
	if (S_cmd_unfold_all) {
		ERR(suites.CommandSuite1()->AEGP_EnableCommand(S_cmd_unfold_all));
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
	
	if (command == S_cmd_create_group) {
		err = DoCreateGroup(suites);
		*handledPB = TRUE;
	}
	else if (command == S_cmd_fold_unfold) {
		err = DoFoldUnfold(suites);
		*handledPB = TRUE;
	}
	else if (command == S_cmd_delete_group) {
		err = DoDeleteGroup(suites);
		*handledPB = TRUE;
	}
	else if (command == S_cmd_fold_all) {
		err = DoFoldAll(suites, true);
		*handledPB = TRUE;
	}
	else if (command == S_cmd_unfold_all) {
		err = DoFoldAll(suites, false);
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
	
	AEGP_SuiteHandler suites(sP);
	
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_create_group));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_fold_unfold));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_delete_group));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_fold_all));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_unfold_all));
	
	if (!err) {
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_create_group,
			FLSTR(StrID_Menu_CreateGroup),
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_fold_unfold,
			FLSTR(StrID_Menu_FoldUnfold),
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_delete_group,
			FLSTR(StrID_Menu_DeleteGroup),
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_fold_all,
			FLSTR(StrID_Menu_FoldAll),
			AEGP_Menu_LAYER,
			AEGP_MENU_INSERT_SORTED));
		
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_unfold_all,
			FLSTR(StrID_Menu_UnfoldAll),
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
	}
	
	if (err) {
		err2 = suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, FLSTR(StrID_Error_Registration));
	}
	
	return err;
}
