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

// Global variables
bool				S_pending_fold_action = false;
double				S_last_left_down_event_ts = 0.0;
double				S_last_click_event_ts = 0.0;
CFMachPortRef		S_event_tap = NULL;
CFRunLoopSourceRef S_event_tap_source = NULL;
bool				S_event_tap_active = false;
pthread_mutex_t		S_mac_state_mutex = PTHREAD_MUTEX_INITIALIZER;
bool				S_mac_divider_selected_for_input = false;
std::string			S_mac_selected_divider_full_name;
bool				S_mac_selected_divider_valid = false;
double				S_mac_selected_divider_cached_at = 0.0;
bool				S_mac_should_warn_ax = false;
bool				S_mac_warned_ax = false;
bool				S_mac_ax_hit_test_usable = true;

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
	return AXIsProcessTrusted() ? true : false;
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

		// Only act on true double-click.
		if (clickState == 2 && (selected || cachedValid)) {
			// If cache is very stale, don't risk false positives.
			if (cachedAt > 0.0) {
				const double now = CFAbsoluteTimeGetCurrent();
				const double age = now - cachedAt;
				if (age < 0.0 || age > 2.0) return event;
			}

			const CGPoint loc = CGEventGetLocation(event);
			const bool axTrusted = MacAXTrusted();
			if (!axTrusted) {
				pthread_mutex_lock(&S_mac_state_mutex);
				S_mac_should_warn_ax = true;
				pthread_mutex_unlock(&S_mac_state_mutex);

				S_pending_fold_action = true;
				S_last_click_event_ts = 0.0;
				return NULL;
			}

			if (!S_mac_ax_hit_test_usable || MacHitTestLooksLikeSelectedDivider(loc)) {
				S_pending_fold_action = true;
				S_last_click_event_ts = 0.0;
				return NULL;
			}
		}
	}

	return event;
}

void InstallMacEventTap()
{
	if (S_event_tap_active) return;
	if (S_event_tap || S_event_tap_source) return;

	CGEventMask mask = CGEventMaskBit(kCGEventLeftMouseDown);
	S_event_tap = CGEventTapCreate(
		kCGSessionEventTap,
		kCGHeadInsertEventTap,
		0,
		mask,
		EventTapCallback,
		NULL);

	if (!S_event_tap) {
		return;
	}

	S_event_tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, S_event_tap, 0);
	if (!S_event_tap_source) {
		CFRelease(S_event_tap);
		S_event_tap = NULL;
		return;
	}

	CFRunLoopAddSource(CFRunLoopGetCurrent(), S_event_tap_source, kCFRunLoopCommonModes);
	CGEventTapEnable(S_event_tap, true);
	S_event_tap_active = true;
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
	// The event tap callback sets S_last_click_event_ts = 0.0 to prevent
	// double-triggering when both mechanisms are active.

	const double now = CFAbsoluteTimeGetCurrent();
	const double since = CGEventSourceSecondsSinceLastEventType(
		kCGEventSourceStateCombinedSessionState,
		kCGEventLeftMouseDown);
	if (!(since >= 0.0)) {
		return;
	}
	const double event_ts = now - since;

	// Detect arrival of a new left-mouse-down event.
	if (event_ts > S_last_left_down_event_ts + 1e-4) {
		S_last_left_down_event_ts = event_ts;

		const double diff = event_ts - S_last_click_event_ts;
		S_last_click_event_ts = event_ts;

		// Double click threshold (0.05s to 0.5s)
		if (diff > 0.05 && diff < 0.5) {
			S_pending_fold_action = true;
			S_last_click_event_ts = 0.0;
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
