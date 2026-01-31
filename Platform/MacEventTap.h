/*******************************************************************/
/*                                                                 */
/*      FoldLayers - macOS Event Tap                               */
/*      Handles double-click detection on macOS                    */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef MAC_EVENT_TAP_H
#define MAC_EVENT_TAP_H

#include "AEConfig.h"
#include <string>

#ifdef AE_OS_MAC

#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <pthread.h>

// Global variables for macOS event handling
extern bool				S_pending_fold_action;
extern double			S_last_left_down_event_ts;
extern double			S_last_click_event_ts;
extern CFMachPortRef	S_event_tap;
extern CFRunLoopSourceRef S_event_tap_source;
extern bool				S_event_tap_active;
extern pthread_mutex_t	S_mac_state_mutex;
extern bool				S_mac_divider_selected_for_input;
extern std::string		S_mac_selected_divider_full_name;
extern bool				S_mac_selected_divider_valid;
extern double			S_mac_selected_divider_cached_at;
extern bool				S_mac_should_warn_ax;
extern bool				S_mac_warned_ax;
extern bool				S_mac_ax_hit_test_usable;

// Initialize macOS event tap
void InitMacEventTap();

// Shutdown macOS event tap
void ShutdownMacEventTap();

// Install macOS event tap
void InstallMacEventTap();

// Poll mouse state (fallback)
void PollMouseState();

// Check Accessibility trust
bool MacAXTrusted();

// Hit test for selected divider
bool MacHitTestLooksLikeSelectedDivider(CGPoint globalPos);

#endif // AE_OS_MAC

#endif // MAC_EVENT_TAP_H
