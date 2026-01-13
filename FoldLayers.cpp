/*******************************************************************/
/*                                                                 */
/*      FoldLayers - AEGP Plugin for After Effects                 */
/*      Recreates GM FoldLayers functionality                      */
/*      Developer: 361do_plugins                                   */
/*      https://github.com/rebuildup                               */
/*                                                                 */
/*      Hierarchy naming:                                          */
/*      - Top level: ▾ Group Divider                               */
/*      - Nested:    ▾(1) Group Divider                            */
/*      - Deeper:    ▾(1/B) Group Divider                          */
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
static bool				S_suppress_next_action	= false;
static CRITICAL_SECTION	S_cs;
static bool				S_cs_initialized		= false;
static bool				S_is_divider_selected	= false; // Track selection state for hook
#endif

// Idle hook state
static A_long			S_idle_counter			= 0;

//=============================================================================
// Helper Functions
//=============================================================================

// Define missing constant if needed
#ifndef AEGP_LayerStream_ROOT_VECTORS_GROUP
	#define AEGP_LayerStream_ROOT_VECTORS_GROUP	((AEGP_LayerStream)0x0D) // 13 in some versions, or check SDK
#endif

//=============================================================================
// Layer Name Utilities (Forward Declarations / Moved Implementation)
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

static std::string BuildDividerName(bool folded, const std::string& hierarchy, const std::string& name)
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

// Check if layer has specific stream/group "FoldGroupData"
static bool HasDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
	/* 
	   TEMPORARILY DISABLED: AEGP_LayerStream_SOURCE not found and Suite versions need update.
	   Focusing on fixing build first.
	*/
	
	// Prevent unused parameter warnings
	(void)suites;
	(void)layerH;
	
	/*
	if (!layerH) return false;
	
	A_Err err = A_Err_NONE;
	bool hasIdentity = false;
	
// Check if layer has specific stream/group "FoldGroupData"
static bool HasDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
	A_Err err = A_Err_NONE;
	if (!layerH) return false;
	
	bool hasIdentity = false;
	
	AEGP_StreamRefH rootStreamH = NULL;
	// Use magic number 13 (AEGP_LayerStream_ROOT_VECTORS_GROUP) if not defined
	// Try to get root vectors group
	if (suites.StreamSuite4()->AEGP_GetNewLayerStream(S_my_id, layerH, AEGP_LayerStream_ROOT_VECTORS_GROUP, &rootStreamH) != A_Err_NONE) {
		// Fallback or error suppression
		return false;
	}
	
	if (rootStreamH) {
		A_long numStreams = 0;
		if (suites.DynamicStreamSuite4()->AEGP_GetNumStreams(rootStreamH, &numStreams) == A_Err_NONE) {
			for (A_long i = 0; i < numStreams && !hasIdentity; i++) {
				AEGP_StreamRefH childStreamH = NULL;
				if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamByIndex(rootStreamH, i, &childStreamH) == A_Err_NONE && childStreamH) {
					AEGP_StreamGroupingType groupType;
					if (suites.DynamicStreamSuite4()->AEGP_GetStreamGroupingType(childStreamH, &groupType) == A_Err_NONE) {
						if (groupType == AEGP_StreamGroupingType_NAMED_GROUP) {
							AEGP_MemHandle nameH = NULL;
							if (suites.StreamSuite4()->AEGP_GetStreamName(childStreamH, FALSE, &nameH) == A_Err_NONE && nameH) {
								void* dataP = NULL;
								if (suites.MemorySuite1()->AEGP_LockMemHandle(nameH, &dataP) == A_Err_NONE && dataP) {
									// Assume UTF-8 or ASCII
									if (strstr((const char*)dataP, "FoldGroupData")) {
										hasIdentity = true;
									}
									suites.MemorySuite1()->AEGP_UnlockMemHandle(nameH);
								}
								suites.MemorySuite1()->AEGP_FreeMemHandle(nameH);
							}
						}
					}
					suites.StreamSuite4()->AEGP_DisposeStream(childStreamH);
				}
			}
		}
		suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
	}
	
	return hasIdentity;
}

// Add identification group to layer
static A_Err AddDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
	A_Err err = A_Err_NONE;
	if (!layerH) return A_Err_STRUCT;
	
	AEGP_StreamRefH rootStreamH = NULL;
	// Use ROOT_VECTORS_GROUP
	err = suites.StreamSuite4()->AEGP_GetNewLayerStream(S_my_id, layerH, AEGP_LayerStream_ROOT_VECTORS_GROUP, &rootStreamH);
	if (err) {
		// Report error for debugging
		char errBuf[128];
		sprintf(errBuf, "FoldLayers Debug: Failed to get Root Stream (Err: %d)", err);
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, errBuf);
		return err;
	}
	
	if (!err && rootStreamH) {
		// Add new group "FoldGroupData" directly to root
		AEGP_StreamRefH newGroupH = NULL;
		ERR(suites.DynamicStreamSuite4()->AEGP_AddStream(rootStreamH, "ADBE Vector Group", &newGroupH));
		
		if (!err && newGroupH) {
			// Rename it to "FoldGroupData"
			ERR(suites.StreamSuite4()->AEGP_SetStreamName(newGroupH, "FoldGroupData"));
            // Debug success
            // suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers Debug: Identity Added");
			suites.StreamSuite4()->AEGP_DisposeStream(newGroupH);
		} else {
            suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers Debug: Failed to add stream");
        }
		
		suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
	}
	
	return err;
}


static bool IsDividerLayer(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
	// First check legacy name-based (fast)
	std::string name;
	if (GetLayerNameStr(suites, layerH, name) == A_Err_NONE) {
		if (name.length() >= 3) {
			unsigned char c0 = (unsigned char)name[0];
			unsigned char c1 = (unsigned char)name[1];
			unsigned char c2 = (unsigned char)name[2];
			if (c0 == 0xE2 && c1 == 0x96 && (c2 == 0xB8 || c2 == 0xBE)) {
				return true;
			}
		}
	}
	
	// If name check failed, check identity content
	if (!suites.StreamSuite4()) return false; // Safety check
	return HasDividerIdentity(suites, layerH);
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

// Parse hierarchy from name like "▾(1/B) Group" -> "1/B"
static std::string GetHierarchy(const std::string& name)
{
	// Skip the prefix (3 bytes for triangle + 1 for space)
	if (name.length() < 4) return "";
	
	size_t pos = 4;  // After "▾ " or "▸ "
	
	// Skip space if present after prefix
	while (pos < name.length() && name[pos] == ' ') pos++;
	
	// Check for hierarchy marker
	if (pos < name.length() && name[pos] == '(') {
		size_t endPos = name.find(')', pos);
		if (endPos != std::string::npos) {
			return name.substr(pos + 1, endPos - pos - 1);
		}
	}
	
	return "";
}

// Get depth from hierarchy string
static int GetHierarchyDepth(const std::string& hierarchy)
{
	if (hierarchy.empty()) return 0;
	
	int depth = 1;
	for (char c : hierarchy) {
		if (c == '/') depth++;
	}
	return depth;
}

// Get display name without prefix and hierarchy
static std::string GetDividerName(const std::string& fullName)
{
	if (fullName.length() <= 4) return "Group";
	
	size_t pos = 4;  // After prefix
	
	// Skip hierarchy if present
	if (pos < fullName.length() && fullName[pos] == '(') {
		size_t endPos = fullName.find(')', pos);
		if (endPos != std::string::npos) {
			pos = endPos + 1;
			// Skip space after hierarchy
			while (pos < fullName.length() && fullName[pos] == ' ') pos++;
		}
	}
	
	if (pos >= fullName.length()) return "Group";
	return fullName.substr(pos);
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

// Get layers that belong to this divider's group
// Considers hierarchy - stops at same or higher level divider
static A_Err GetGroupLayers(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                            A_long dividerIndex, const std::string& dividerHierarchy,
                            std::vector<AEGP_LayerH>& groupLayers)
{
	A_Err err = A_Err_NONE;
	A_long numLayers = 0;
	int myDepth = GetHierarchyDepth(dividerHierarchy);
	
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	for (A_long i = dividerIndex + 1; i < numLayers && !err; i++) {
		AEGP_LayerH layer = NULL;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer) {
			std::string name;
			ERR(GetLayerNameStr(suites, layer, name));
			
			if (!err && IsDividerLayer(suites, layer)) {
				// Check hierarchy depth
				std::string otherHierarchy = GetHierarchy(name);
				int otherDepth = GetHierarchyDepth(otherHierarchy);
				
				// Stop at same or higher level divider
				if (otherDepth <= myDepth) {
					break;
				}
				// Nested dividers are included in the group
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
	
	std::string hierarchy = GetHierarchy(dividerName);
	std::string baseName = GetDividerName(dividerName);
	std::string newName = BuildDividerName(fold, hierarchy, baseName);
	ERR(SetLayerNameStr(suites, dividerLayer, newName));
	
	std::vector<AEGP_LayerH> groupLayers;
	ERR(GetGroupLayers(suites, compH, dividerIndex, hierarchy, groupLayers));
	
	// Complex logic: 
	// If folding (fold=true), hide everything.
	// If unfolding (fold=false), hide only children of *closed* sub-dividers.
	
	int skipUntilDepth = -1; // -1 means do not skip
	
	for (size_t i = 0; i < groupLayers.size() && !err; i++) {
		AEGP_LayerH subLayer = groupLayers[i];
		
		std::string subName;
		ERR(GetLayerNameStr(suites, subLayer, subName));
		std::string subHier = GetHierarchy(subName);
		int subDepth = GetHierarchyDepth(subHier);
		
		bool shouldHide = false;
		
		if (fold) {
			// Hiding parent: hide everything
			shouldHide = true;
		} else {
			// Unfolding parent:
			// Check if we are inside a closed sub-group
			if (skipUntilDepth != -1) {
				if (subDepth > skipUntilDepth) {
					// Still deeper than the closed parent -> keep hidden
					shouldHide = true;
				} else {
					// Returned to shallower or same level -> stop skipping
					skipUntilDepth = -1;
				}
			}
			
			if (skipUntilDepth == -1) {
				// Not currently skipping, so show this layer
				shouldHide = false;
				
				// But if this layer is ITSELF a folded divider, start skipping its children
				if (IsDividerLayer(suites, subLayer)) {
					if (IsDividerFolded(subName)) {
						skipUntilDepth = subDepth; // effectively skips children (depth > subDepth)
					}
				}
			}
		}
		
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(subLayer, AEGP_LayerFlag_SHY, shouldHide ? TRUE : FALSE));
	}
	
	return err;
}

// Toggle a single divider
static A_Err ToggleDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                           AEGP_LayerH layerH, A_long layerIndex)
{
	A_Err err = A_Err_NONE;
	
	std::string name;
	ERR(GetLayerNameStr(suites, layerH, name));
	
	if (!err && IsDividerLayer(suites, layerH)) {
		bool isFolded = IsDividerFolded(name);
		ERR(FoldDivider(suites, compH, layerH, layerIndex, !isFolded));
	}
	
	return err;
}

// Get all dividers in the composition
static A_Err GetAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH,
                            std::vector<std::pair<AEGP_LayerH, A_long> >& dividers)
{
	A_Err err = A_Err_NONE;
	A_long numLayers = 0;
	
	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));
	
	for (A_long i = 0; i < numLayers && !err; i++) {
		AEGP_LayerH layer = NULL;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer) {
			std::string name;
			ERR(GetLayerNameStr(suites, layer, name));
			if (!err && IsDividerLayer(suites, layer)) {
				dividers.push_back(std::make_pair(layer, i));
			}
		}
	}
	
	return err;
}

// Check if any divider is selected
static A_Err IsDividerSelected(AEGP_SuiteHandler& suites, AEGP_CompH compH, bool* result)
{
	A_Err err = A_Err_NONE;
	*result = false;
	
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
		
		for (A_u_long i = 0; i < numSelected && !err; i++) {
			AEGP_CollectionItemV2 item;
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, i, &item));
			if (!err && item.type == AEGP_CollectionItemType_LAYER) {
				std::string name;
				ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
				if (!err && IsDividerLayer(suites, item.u.layer.layerH)) {
					*result = true;
					break;
				}
			}
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	return err;
}

// Toggle selected dividers only
static A_Err ToggleSelectedDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH)
{
	A_Err err = A_Err_NONE;
	
	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;
	
	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));
		
		if (!err && numSelected > 0) {
			for (A_u_long i = 0; i < numSelected && !err; i++) {
				AEGP_CollectionItemV2 item;
				ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, i, &item));
				if (!err && item.type == AEGP_CollectionItemType_LAYER) {
					std::string name;
					ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
					if (!err && IsDividerLayer(suites, item.u.layer.layerH)) {
						A_long idx = 0;
						ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &idx));
						if (!err) {
							ERR(ToggleDivider(suites, compH, item.u.layer.layerH, idx));
						}
					}
				}
			}
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}
	
	return err;
}

// Toggle all dividers - unfold priority, fold if all unfolded
static A_Err ToggleAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH)
{
	A_Err err = A_Err_NONE;
	
	std::vector<std::pair<AEGP_LayerH, A_long> > dividers;
	ERR(GetAllDividers(suites, compH, dividers));
	
	if (!err && !dividers.empty()) {
		// Check if all are unfolded
		bool allUnfolded = true;
		for (auto& div : dividers) {
			std::string name;
			ERR(GetLayerNameStr(suites, div.first, name));
			if (!err && IsDividerFolded(name)) {
				allUnfolded = false;
				break;
			}
		}
		
		// If all unfolded -> fold all, otherwise unfold all
		bool targetFold = allUnfolded;
		
		for (auto& div : dividers) {
			std::string name;
			ERR(GetLayerNameStr(suites, div.first, name));
			if (!err) {
				bool isFolded = IsDividerFolded(name);
				if (isFolded != targetFold) {
					ERR(FoldDivider(suites, compH, div.first, div.second, targetFold));
				}
			}
		}
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
	std::string parentHierarchy = "";
	
	// Find insert position and parent hierarchy
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
				
				// Check if selected layer is a divider - if so, nest under it
				std::string selName;
				ERR(GetLayerNameStr(suites, item.u.layer.layerH, selName));
				if (!err && IsDividerLayer(suites, item.u.layer.layerH)) {
					std::string selHierarchy = GetHierarchy(selName);
					// Find next available sub-level
					// Count existing children at this level
					std::vector<std::pair<AEGP_LayerH, A_long> > allDividers;
					ERR(GetAllDividers(suites, compH, allDividers));
					
					int childCount = 0;
					std::string prefix = selHierarchy.empty() ? "" : selHierarchy + "/";
					
					for (auto& div : allDividers) {
						std::string divName;
						ERR(GetLayerNameStr(suites, div.first, divName));
						if (!err) {
							std::string divHier = GetHierarchy(divName);
							// Check if it's a direct child
							if (!prefix.empty() && divHier.substr(0, prefix.length()) == prefix) {
								// Count only direct children (no additional /)
								std::string remainder = divHier.substr(prefix.length());
								if (remainder.find('/') == std::string::npos) {
									childCount++;
								}
							} else if (prefix.empty() && !divHier.empty() && divHier.find('/') == std::string::npos) {
								childCount++;
							}
						}
					}
					
					// Build new hierarchy
					childCount++;  // Next number
					if (selHierarchy.empty()) {
						parentHierarchy = std::to_string(childCount);
					} else {
						// Use letters for deeper nesting: 1, 1/A, 1/A/a, 1/A/a/i...
						int depth = GetHierarchyDepth(selHierarchy);
						char nextChar;
						if (depth == 0) {
							nextChar = '0' + childCount;
						} else if (depth == 1) {
							nextChar = 'A' + (childCount - 1);
						} else if (depth == 2) {
							nextChar = 'a' + (childCount - 1);
						} else {
							nextChar = 'i' + (childCount - 1);  // i, ii, iii style approximation
						}
						parentHierarchy = selHierarchy + "/" + nextChar;
					}
				}
			}
		}
		
		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		collectionH = NULL;
	}
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Create Group Divider"));
	
	// Create SHAPE layer
	AEGP_LayerH newLayer = NULL;
	ERR(suites.CompSuite11()->AEGP_CreateVectorLayerInComp(compH, &newLayer));
	
	if (!err && newLayer) {
		// Set layer name with hierarchy
		std::string dividerName = BuildDividerName(false, parentHierarchy, "Group");
		ERR(SetLayerNameStr(suites, newLayer, dividerName));
		
		// Move to insert position (BELOW the selected layer, so index + 1)
		// Note: AEGP_ReorderLayer takes the new index. 
		// If we want it after insertIndex, we should use insertIndex + 1.
		if (insertIndex >= 0) { // insertIndex is 0-based
			ERR(suites.LayerSuite9()->AEGP_ReorderLayer(newLayer, insertIndex + 1));
		}
		
		// Set VIDEO OFF (invisible)
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(newLayer, AEGP_LayerFlag_VIDEO_ACTIVE, FALSE));
		
		// Set label to 0 (None)
		ERR(suites.LayerSuite9()->AEGP_SetLayerLabel(newLayer, 0));
		
		// Add identity group
		ERR(AddDividerIdentity(suites, newLayer));
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	return err;
}

static A_Err DoFoldUnfold(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;
	
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_NONE;
	
	// Check if any divider is selected
	bool dividerSelected = false;
	ERR(IsDividerSelected(suites, compH, &dividerSelected));

    // Debug
    if (!dividerSelected) {
         // Check if a layer is selected at all
        AEGP_Collection2H collectionH = NULL;
        A_u_long numSelected = 0;
        if (suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH) == A_Err_NONE) {
            suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected);
            if (numSelected > 0) {
                 // Selected but not identified as divider
                 suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers Debug: Layer selected but not recognized as Group");
            }
            suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
        }
    }
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Fold/Unfold"));
	
	if (!err) {
		if (dividerSelected) {
			// Toggle only selected dividers
			ERR(ToggleSelectedDividers(suites, compH));
		} else {
			// Toggle all dividers
			ERR(ToggleAllDividers(suites, compH));
		}
	}
	
	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	
	return err;
}

//=============================================================================
// Windows: Mouse hook for double-click detection
//=============================================================================

#ifdef AE_OS_WIN

// Forward declaration
static A_Err ProcessDoubleClick();

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0 && wParam == WM_LBUTTONDBLCLK) {
		// Only suppress if we believe a divider is selected
		bool should_suppress = false;
		
		EnterCriticalSection(&S_cs);
		if (S_is_divider_selected) {
			S_double_click_pending = true;
			should_suppress = true;
		}
		LeaveCriticalSection(&S_cs);
		
		if (should_suppress) {
			return 1; // Block message -> No sound!
		}
	}
	return CallNextHookEx(S_mouse_hook, nCode, wParam, lParam);
}

static A_Err ProcessDoubleClick()
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
			
			if (!err && numSelected == 1) {
				AEGP_CollectionItemV2 item;
				ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item));
				
				if (!err && item.type == AEGP_CollectionItemType_LAYER) {
					std::string name;
					ERR(GetLayerNameStr(suites, item.u.layer.layerH, name));
					
					if (!err && IsDividerLayer(suites, item.u.layer.layerH)) {
						// Divider is selected and double-clicked - toggle!
						A_long idx = 0;
						ERR(suites.LayerSuite9()->AEGP_GetLayerIndex(item.u.layer.layerH, &idx));
						if (!err) {
							ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Fold/Unfold"));
							ERR(ToggleDivider(suites, compH, item.u.layer.layerH, idx));
							ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
						}
					}
				}
			}
			
			suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		}
	}
	
	return err;
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
	S_idle_counter++;
	
	AEGP_SuiteHandler suites(sP);
	
	// Default to not selected
	bool dividerSelected = false;
	
	AEGP_CompH compH = NULL;
	if (GetActiveComp(suites, &compH) == A_Err_NONE && compH) {
		// Check selection state for the hook
		IsDividerSelected(suites, compH, &dividerSelected);
	}
	
#ifdef AE_OS_WIN
	// Update shared state
	EnterCriticalSection(&S_cs);
	S_is_divider_selected = dividerSelected;
	
	// Check if a double-click is pending
	bool dblclick = false;
	if (S_double_click_pending) {
		dblclick = true;
		S_double_click_pending = false;
	}
	LeaveCriticalSection(&S_cs);
	
	if (dblclick) {
		ProcessDoubleClick();
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

extern "C" DllExport A_Err EntryPointFunc(
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
