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
#define FOLDLAYERS_DEBUG_MAC 1

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
static bool			S_last_mouse_down = false;
static double		S_last_click_time = 0.0;
bool				S_mac_divider_selected_for_input = false;
std::string			S_mac_selected_divider_full_name;
bool				S_mac_selected_divider_valid = false;
double				S_mac_selected_divider_cached_at = 0.0;
bool				S_mac_should_warn_ax = false;
bool				S_mac_warned_ax = false;
bool				S_mac_ax_hit_test_usable = true;
static bool			S_ax_trusted = false;
static bool			S_ax_trusted_checked = false;
std::string			S_mac_last_logged_divider_name;  // Track last logged divider to avoid spam

static bool CFStringContainsDividerName(CFStringRef s, const std::string& fullName)
{
	if (!s) return false;
	char buf[2048];
	if (!CFStringGetCString(s, buf, (CFIndex)sizeof(buf), kCFStringEncodingUTF8)) return false;
	const std::string hay(buf);
	if (!fullName.empty() && hay.find(fullName) != std::string::npos) return true;
	return false;
}

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

bool MacHitTestLooksLikeSelectedDivider(CGPoint globalPos)
{
	std::string fullName;
	pthread_mutex_lock(&S_mac_state_mutex);
	const bool valid = S_mac_selected_divider_valid;
	const double cachedAt = S_mac_selected_divider_cached_at;
	if (valid) {
		fullName = S_mac_selected_divider_full_name;
	}
	pthread_mutex_unlock(&S_mac_state_mutex);
	if (!valid) return false;
	// Avoid stale matches if selection changed long ago.
	if (cachedAt > 0.0) {
		const double now = CFAbsoluteTimeGetCurrent();
		const double age = now - cachedAt;
		if (age < 0.0 || age > 2.0) return false;
	}
	if (fullName.empty()) return false;

	AXUIElementRef systemWide = AXUIElementCreateSystemWide();
	if (!systemWide) return false;

	AXUIElementRef hit = NULL;
	AXError axErr = AXUIElementCopyElementAtPosition(systemWide, globalPos.x, globalPos.y, &hit);
	CFRelease(systemWide);
	if (axErr != kAXErrorSuccess || !hit) {
		S_mac_ax_hit_test_usable = false;
		return false;
	}
	S_mac_ax_hit_test_usable = true;

	CFTypeRef titleV = NULL;
	if (AXUIElementCopyAttributeValue(hit, kAXTitleAttribute, &titleV) == kAXErrorSuccess && titleV) {
		if (CFGetTypeID(titleV) == CFStringGetTypeID() && CFStringContainsDividerName((CFStringRef)titleV, fullName)) {
			CFRelease(titleV);
			CFRelease(hit);
			return true;
		}
		CFRelease(titleV);
	}

	CFTypeRef valueV = NULL;
	if (AXUIElementCopyAttributeValue(hit, kAXValueAttribute, &valueV) == kAXErrorSuccess && valueV) {
		if (CFGetTypeID(valueV) == CFStringGetTypeID() && CFStringContainsDividerName((CFStringRef)valueV, fullName)) {
			CFRelease(valueV);
			CFRelease(hit);
			return true;
		}
		CFRelease(valueV);
	}

	CFTypeRef descV = NULL;
	if (AXUIElementCopyAttributeValue(hit, kAXDescriptionAttribute, &descV) == kAXErrorSuccess && descV) {
		if (CFGetTypeID(descV) == CFStringGetTypeID() && CFStringContainsDividerName((CFStringRef)descV, fullName)) {
			CFRelease(descV);
			CFRelease(hit);
			return true;
		}
		CFRelease(descV);
	}

	CFRelease(hit);
	return false;
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
		const bool cachedValid = S_mac_selected_divider_valid;
		const double cachedAt = S_mac_selected_divider_cached_at;
		pthread_mutex_unlock(&S_mac_state_mutex);

		DEBUG_LOG("EventTap: LeftMouseDown clickState=%lld selected=%d cachedValid=%d",
			clickState, selected, cachedValid);

		// Only act on true double-click.
		if (clickState == 2 && (selected || cachedValid)) {
			// If cache is very stale, don't risk false positives.
			if (cachedAt > 0.0) {
				const double now = CFAbsoluteTimeGetCurrent();
				const double age = now - cachedAt;
				if (age < 0.0 || age > 2.0) {
					DEBUG_LOG("EventTap: Cache too stale (%.2fs), ignoring", age);
					return event;
				}
			}

			const CGPoint loc = CGEventGetLocation(event);
			const bool axTrusted = MacAXTrusted();
			if (!axTrusted) {
				DEBUG_LOG("EventTap: AX not trusted, allowing selection-based fold");
				pthread_mutex_lock(&S_mac_state_mutex);
				S_mac_should_warn_ax = true;
				pthread_mutex_unlock(&S_mac_state_mutex);

				S_pending_fold_action = true;
				return NULL;
			}

			DEBUG_LOG("EventTap: AX trusted, performing hit test at (%.1f, %.1f)", loc.x, loc.y);
			if (!S_mac_ax_hit_test_usable || MacHitTestLooksLikeSelectedDivider(loc)) {
				DEBUG_LOG("EventTap: Hit test passed! Setting pending fold action");
				S_pending_fold_action = true;
				return NULL;
			} else {
				DEBUG_LOG("EventTap: Hit test FAILED, not folding");
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

	// CRITICAL: Do NOT check S_event_tap_active here.
	// Even when the event tap is active, it may fail to suppress events
	// (e.g., due to permissions issues or macOS changes). The polling
	// fallback ensures double-click detection still works in those cases.
	// The polling uses button state transitions to detect clicks even
	// while the mouse button is held down between double-clicks.

	// Use button state detection to catch clicks even while button is held down
	const bool isDown = CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft);

#if FOLDLAYERS_DEBUG_MAC
	static int pollCount = 0;
	if ((pollCount++ % 60) == 0) {
		DEBUG_LOG("PollMouseState: isDown=%d, last_down=%d, selected=%d",
			isDown, S_last_mouse_down, dividerSelected);
	}
#endif

	if (isDown && !S_last_mouse_down) {
		// Transition from up to down - new click detected
		const double now = CFAbsoluteTimeGetCurrent();
		const double diff = now - S_last_click_time;
		S_last_click_time = now;

		DEBUG_LOG("CLICK DETECTED: diff=%.3f seconds", diff);

		// Double click threshold (0.05s to 0.5s)
		if (diff > 0.05 && diff < 0.5) {
			DEBUG_LOG("DOUBLE-CLICK DETECTED! Setting pending fold action");
			S_pending_fold_action = true;
			S_last_click_time = 0.0; // Prevent consecutive triggering
		}
	}
	S_last_mouse_down = isDown;
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
