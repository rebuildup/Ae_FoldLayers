/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Fold/Unfold Commands                          */
/*      Handles fold/unfold operations for dividers                */
/*                                                                 */
/*******************************************************************/

#include "FoldUnfold.h"
#include "FoldLayers.h"

// Enable Hide Shy Layers for the active composition
// Returns A_Err_NONE on success, error code otherwise
A_Err EnsureShyModeEnabled(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;

	// Get active composition
	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_NONE;

	// Use ExtendScript to enable hideShyLayers
	// This must be called OUTSIDE of UndoGroup for reliable execution
	const char* script =
		"try {"
		"    if (app.project.activeItem && app.project.activeItem instanceof CompItem) {"
		"        var comp = app.project.activeItem;"
		"        if (!comp.hideShyLayers) {"
		"            comp.hideShyLayers = true;"
		"        }"
		"    }"
		"} catch(e) {"
		"    // Silently ignore errors"
		"}";

	AEGP_MemHandle resultH = NULL;
	AEGP_MemHandle errorH = NULL;
	ERR(suites.UtilitySuite6()->AEGP_ExecuteScript(S_my_id, script, FALSE, &resultH, &errorH));

	// Check for script errors
	if (errorH) {
		void* errorP = NULL;
		if (suites.MemorySuite1()->AEGP_LockMemHandle(errorH, &errorP) == A_Err_NONE && errorP) {
			// Log error for debugging (optional, commented out)
			// suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, (const char*)errorP);
		}
		suites.MemorySuite1()->AEGP_UnlockMemHandle(errorH);
	}

	if (resultH) suites.MemorySuite1()->AEGP_FreeMemHandle(resultH);
	if (errorH) suites.MemorySuite1()->AEGP_FreeMemHandle(errorH);

	return err;
}

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

static A_Err ToggleAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH)
{
	A_Err err = A_Err_NONE;

	std::vector<std::pair<AEGP_LayerH, A_long> > dividers;
	ERR(GetAllDividers(suites, compH, dividers));

	if (!err && !dividers.empty()) {
		// Check if all are unfolded
		bool allUnfolded = true;
		for (auto& div : dividers) {
			if (IsDividerFolded(suites, div.first)) {
				allUnfolded = false;
				break;
			}
		}

		// If all unfolded -> fold all, otherwise unfold all
		bool targetFold = allUnfolded;

		for (auto& div : dividers) {
			bool isFolded = IsDividerFolded(suites, div.first);
			if (isFolded != targetFold) {
				ERR(FoldDivider(suites, compH, div.first, div.second, targetFold));
			}
		}
	}

	return err;
}

A_Err DoFoldUnfold(AEGP_SuiteHandler& suites)
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

	// IMPORTANT: Call EnsureShyModeEnabled AFTER ending the UndoGroup
	// ExtendScript execution may be restricted inside UndoGroup
	// This ensures Hide Shy Layers is enabled for the fold/unfold to be visible
	ERR(EnsureShyModeEnabled(suites));

	return err;
}
