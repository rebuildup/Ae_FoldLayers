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

// MouseProc - handles double-click detection for Windows
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

// ProcessDoubleClick implementation is in FoldLayers.cpp

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
