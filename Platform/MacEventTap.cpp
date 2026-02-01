/*******************************************************************/
/*                                                                 */
/*      FoldLayers - macOS Event Tap                               */
/*      Handles double-click detection on macOS                    */
/*                                                                 */
/*******************************************************************/

#include "MacEventTap.h"

#ifdef AE_OS_MAC

#include "FoldLayers.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

// Enable debug logging (set to 0 to disable in production)
#define FOLDLAYERS_DEBUG_MAC 0

#if FOLDLAYERS_DEBUG_MAC
// File-based logging for MacEventTap (since stderr isn't visible in AE)
static void DebugLog(const char* fmt, ...) {
	FILE* f = fopen("/tmp/foldlayers_debug.log", "a");
	if (f) {
		va_list args;
		va_start(args, fmt);
		vfprintf(f, fmt, args);
		va_end(args);
		fprintf(f, "\n");
		fclose(f);
	}
}
#define DEBUG_LOG(fmt, ...) DebugLog("[FoldLayers] " fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) do {} while(0)
#endif

// Global variables
bool				S_pending_fold_action = false;
CFMachPortRef		S_event_tap = NULL;
CFRunLoopSourceRef S_event_tap_source = NULL;
bool				S_event_tap_active = false;
pthread_mutex_t		S_mac_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Static state for polling fallback
static double		S_last_click_time = 0.0;
static double		S_last_event_timestamp = 0.0;
bool				S_mac_divider_selected_for_input = false;
bool				S_mac_should_warn_ax = false;
bool				S_mac_warned_ax = false;
static bool			S_ax_trusted = false;
static bool			S_ax_trusted_checked = false;

bool MacAXTrusted()
{
	if (!S_ax_trusted_checked) {
		S_ax_trusted = AXIsProcessTrusted() ? true : false;
		S_ax_trusted_checked = true;

		// Prompt user for Accessibility permissions if not trusted
		if (!S_ax_trusted) {
			DEBUG_LOG("Accessibility NOT trusted - prompting user...");

			// Use AXIsProcessTrustedWithOptions to show system prompt
			CFStringRef promptKey = kAXTrustedCheckOptionPrompt;
			CFBooleanRef promptValue = kCFBooleanTrue;
			CFTypeRef keys[] = { promptKey };
			CFTypeRef values[] = { promptValue };

			CFDictionaryRef options = CFDictionaryCreate(
				NULL,
				(const void **)keys,
				(const void **)values,
				1,
				&kCFCopyStringDictionaryKeyCallBacks,
				&kCFTypeDictionaryValueCallBacks
			);

			if (options) {
				S_ax_trusted = AXIsProcessTrustedWithOptions(options);
				CFRelease(options);
				DEBUG_LOG("After prompt - trusted: %d", S_ax_trusted);
			}
		} else {
			DEBUG_LOG("Accessibility IS trusted");
		}
	}
	return S_ax_trusted;
}

static CGEventRef EventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon)
{
	(void)proxy;
	(void)refcon;

	if (!event) return event;

	if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
		DEBUG_LOG("EventTap was disabled, re-enabling...");
		if (S_event_tap) {
			CGEventTapEnable(S_event_tap, true);
		}
		return event;
	}

	if (type == kCGEventLeftMouseDown) {
		const int64_t clickState = CGEventGetIntegerValueField(event, kCGMouseEventClickState);
		pthread_mutex_lock(&S_mac_state_mutex);
		const bool selected = S_mac_divider_selected_for_input;
		pthread_mutex_unlock(&S_mac_state_mutex);

		DEBUG_LOG("EventTap: LeftMouseDown clickState=%lld selected=%d",
			clickState, selected);

		// Only act on true double-click when a divider is selected
		if (clickState == 2 && selected) {
			// Check cooldown to prevent double-triggering from both mechanisms
			const double now = CFAbsoluteTimeGetCurrent();
			pthread_mutex_lock(&S_mac_state_mutex);
			const double timeSinceLastAction = now - S_last_event_timestamp;
			pthread_mutex_unlock(&S_mac_state_mutex);

			// Only process if enough time has passed since last action (0.2s cooldown)
			if (timeSinceLastAction > 0.2) {
				DEBUG_LOG("EventTap: Double-click on selected divider - setting pending fold action");
				pthread_mutex_lock(&S_mac_state_mutex);
				S_last_event_timestamp = now;
				pthread_mutex_unlock(&S_mac_state_mutex);
				S_pending_fold_action = true;
				return NULL;  // Suppress the event
			} else {
				DEBUG_LOG("EventTap: Double-click ignored due to cooldown (%.2fs since last action)", timeSinceLastAction);
			}
		}
	}

	return event;
}

void InstallMacEventTap()
{
	if (S_event_tap_active) {
		DEBUG_LOG("EventTap already active");
		return;
	}
	if (S_event_tap || S_event_tap_source) {
		DEBUG_LOG("EventTap already created");
		return;
	}

	DEBUG_LOG("Attempting to install EventTap...");

	// Check Accessibility permissions first
	const bool axTrusted = MacAXTrusted();
	if (!axTrusted) {
		DEBUG_LOG("Cannot install EventTap - Accessibility not trusted");
		return;
	}

	CGEventMask mask = CGEventMaskBit(kCGEventLeftMouseDown);
	S_event_tap = CGEventTapCreate(
		kCGSessionEventTap,
		kCGHeadInsertEventTap,
		0,
		mask,
		EventTapCallback,
		NULL);

	if (!S_event_tap) {
		DEBUG_LOG("EventTap creation FAILED - polling fallback will be used");
		return;
	}

	DEBUG_LOG("EventTap created successfully");

	S_event_tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, S_event_tap, 0);
	if (!S_event_tap_source) {
		DEBUG_LOG("Failed to create run loop source for EventTap");
		CFRelease(S_event_tap);
		S_event_tap = NULL;
		return;
	}

	CFRunLoopAddSource(CFRunLoopGetCurrent(), S_event_tap_source, kCFRunLoopCommonModes);
	CGEventTapEnable(S_event_tap, true);
	S_event_tap_active = true;
	DEBUG_LOG("EventTap installed and enabled");
}

void PollMouseState()
{
	// Check if a divider is currently selected
	pthread_mutex_lock(&S_mac_state_mutex);
	const bool dividerSelected = S_mac_divider_selected_for_input;
	pthread_mutex_unlock(&S_mac_state_mutex);

	if (!dividerSelected) {
		return;  // No divider selected, skip double-click detection
	}

	// CRITICAL: Use event timing detection instead of button state
	// CGEventSourceButtonState only gives physical button state, which doesn't
	// reliably detect the second click of a double-click (button is still held down).
	// Instead, measure the time since the last left mouse down event.
	const double since = CGEventSourceSecondsSinceLastEventType(
		kCGEventSourceStateCombinedSessionState,
		kCGEventLeftMouseDown);

	// Check for valid result
	if (!(since >= 0.0)) {
		return;
	}

	const double now = CFAbsoluteTimeGetCurrent();
	const double event_ts = now - since;

	// Detect a new mouse down event (with some tolerance for timing variations)
	if (event_ts > S_last_click_time + 0.01) {
		const double diff = event_ts - S_last_click_time;
		S_last_click_time = event_ts;

#if FOLDLAYERS_DEBUG_MAC
		DEBUG_LOG("PollMouseState: click detected, diff=%.3f seconds", diff);
#endif

		// Double click threshold (0.03s to 0.7s)
		// - Minimum: 30ms to avoid false positives from very rapid clicks
		// - Maximum: 700ms to accommodate macOS system double-click setting (default 500ms)
		if (diff > 0.03 && diff < 0.7) {
			// Check cooldown to prevent double-triggering from both mechanisms
			pthread_mutex_lock(&S_mac_state_mutex);
			const double timeSinceLastAction = now - S_last_event_timestamp;
			pthread_mutex_unlock(&S_mac_state_mutex);

			// Only process if enough time has passed since last action (0.2s cooldown)
			if (timeSinceLastAction > 0.2) {
#if FOLDLAYERS_DEBUG_MAC
				DEBUG_LOG("PollMouseState: DOUBLE-CLICK DETECTED!");
#endif
				pthread_mutex_lock(&S_mac_state_mutex);
				S_last_event_timestamp = now;
				pthread_mutex_unlock(&S_mac_state_mutex);
				S_pending_fold_action = true;
				S_last_click_time = 0.0;  // Prevent consecutive triggering
			}
		}
	}
}

void InitMacEventTap()
{
	InstallMacEventTap();
}

void ShutdownMacEventTap()
{
	if (S_event_tap) {
		CGEventTapEnable(S_event_tap, false);
		if (S_event_tap_source) {
			CFRunLoopRemoveSource(CFRunLoopGetCurrent(), S_event_tap_source, kCFRunLoopCommonModes);
			CFRelease(S_event_tap_source);
			S_event_tap_source = NULL;
		}
		CFRelease(S_event_tap);
		S_event_tap = NULL;
	}
	S_event_tap_active = false;
}

#endif // AE_OS_MAC
