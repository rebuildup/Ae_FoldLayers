/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Create Divider Command                        */
/*      Creates new group divider layers                           */
/*                                                                 */
/*******************************************************************/

#include "CreateDivider.h"
#include "FoldLayers.h"

A_Err DoCreateDivider(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;

	ERR(GetActiveComp(suites, &compH));
	if (!compH) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "Please open a composition first.");
		return A_Err_NONE;
	}

	// If nothing is selected, create at the very top (index 0).
	// This matches the "apply to all" intention when no layer is selected.
	A_long insertIndex = -1;
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

					// Check if parent hierarchy is already too deep
					int selDepth = GetHierarchyDepth(selHierarchy);
					if (selDepth < 0) {
						suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Cannot nest group - maximum hierarchy depth exceeded.");
						suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
						return A_Err_NONE;
					}

					// Check if creating a new child would exceed MAX_HIERARCHY_DEPTH
					if (selDepth >= MAX_HIERARCHY_DEPTH) {
						char msg[256];
#ifdef AE_OS_WIN
						sprintf_s(msg, sizeof(msg), "FoldLayers: Maximum nesting depth (%d) reached. Cannot create nested group.", MAX_HIERARCHY_DEPTH);
#else
						snprintf(msg, sizeof(msg), "FoldLayers: Maximum nesting depth (%d) reached. Cannot create nested group.", MAX_HIERARCHY_DEPTH);
#endif
						suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, msg);
						suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
						return A_Err_NONE;
					}

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
							if (!prefix.empty() && divHier.length() >= prefix.length() && divHier.substr(0, prefix.length()) == prefix) {
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

					// Build new hierarchy with overflow checks
					childCount++;  // Next number
					if (selHierarchy.empty()) {
						// Check for integer overflow
						if (childCount > 999) {
							suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Too many groups at this level.");
							suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
							return A_Err_NONE;
						}
						parentHierarchy = std::to_string(childCount);
					} else {
						// Use letters for deeper nesting: 1, 1/A, 1/A/a, 1/A/a/i...
						int depth = GetHierarchyDepth(selHierarchy);
						if (depth < 0) {
							suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Invalid hierarchy depth detected.");
							suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
							return A_Err_NONE;
						}

						char nextChar;
						if (depth == 0) {
							// Numeric level (0-9)
							if (childCount > 9) {
								suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Too many groups at this level (max 9).");
								suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
								return A_Err_NONE;
							}
							nextChar = '0' + childCount;
						} else if (depth == 1) {
							// Uppercase letters (A-Z)
							if (childCount > 26) {
								suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Too many groups at this level (max 26).");
								suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
								return A_Err_NONE;
							}
							nextChar = 'A' + (childCount - 1);
						} else if (depth == 2) {
							// Lowercase letters (a-z)
							if (childCount > 26) {
								suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Too many groups at this level (max 26).");
								suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
								return A_Err_NONE;
							}
							nextChar = 'a' + (childCount - 1);
						} else {
							// i, ii, iii style approximation (limited to 20)
							if (childCount > 20) {
								suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Too many groups at this level (max 20).");
								suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
								return A_Err_NONE;
							}
							nextChar = 'i' + (childCount - 1);
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

		// Move to insert position.
		// - If a layer is selected: place the divider directly below it.
		// - If nothing is selected: place the divider at the top (index 0).
		// Note: This project treats layer indices as 0-based (see GetCompLayerByIndex loops).
		const A_long targetIndex = (insertIndex >= 0) ? (insertIndex + 1) : 0;
		ERR(suites.LayerSuite9()->AEGP_ReorderLayer(newLayer, targetIndex));

		// Set VIDEO OFF (invisible)
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(newLayer, AEGP_LayerFlag_VIDEO_ACTIVE, FALSE));

		// Set label to 0 (None)
		ERR(suites.LayerSuite9()->AEGP_SetLayerLabel(newLayer, 0));

		// Add identity group with hierarchy info
		ERR(AddDividerIdentity(suites, newLayer, parentHierarchy));
	}

	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());

	return err;
}
