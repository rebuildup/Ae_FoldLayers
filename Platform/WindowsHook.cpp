/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Windows Mouse Hook                            */
/*      Handles double-click detection on Windows                  */
/*                                                                 */
/*******************************************************************/

#include "WindowsHook.h"

#ifdef AE_OS_WIN

#include "FoldLayers.h"

// Global variables defined in FoldLayers.cpp
extern HHOOK			S_mouse_hook;
extern bool				S_double_click_pending;
extern bool				S_suppress_next_action;
extern CRITICAL_SECTION	S_cs;
extern bool				S_cs_initialized;
extern bool				S_is_divider_selected;

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

A_Err ProcessDoubleClick()
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

void InitWindowsHook()
{
	// Initialize critical section
	InitializeCriticalSection(&S_cs);
	S_cs_initialized = true;
	S_double_click_pending = false;

	// Install mouse hook to detect double-clicks
	S_mouse_hook = SetWindowsHookEx(WH_MOUSE, MouseProc, NULL, GetCurrentThreadId());
}

void ShutdownWindowsHook()
{
	if (S_mouse_hook) {
		UnhookWindowsHookEx(S_mouse_hook);
		S_mouse_hook = NULL;
	}
	if (S_cs_initialized) {
		DeleteCriticalSection(&S_cs);
		S_cs_initialized = false;
	}
}

#endif // AE_OS_WIN
