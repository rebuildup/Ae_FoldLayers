/*******************************************************************/
/*                                                                 */
/*      FoldLayers - AEGP Plugin for After Effects                 */
/*      Recreates GM FoldLayers functionality                      */
/*      Developer: 361do_plugins                                   */
/*      https://github.com/rebuildup                               */
/*                                                                 */
/*      Hybrid fold state management:                              */
/*      - Layer names show visual prefix (▸ folded, ▾ unfolded)    */
/*      - Fold state stored in hidden stream groups: FD-0 (unfolded)*/
/*        and FD-1 (folded)                                        */
/*      - Hierarchy stored in FD-H: group for nesting             */
/*      - Visual prefixes update on fold/unfold operations         */
/*                                                                 */
/*******************************************************************/

#include <ctime>
#ifdef AE_OS_MAC
#include <pthread.h>
#include <unistd.h>
#endif
#define _CRT_SECURE_NO_WARNINGS
#include "FoldLayers.h"

#ifdef AE_OS_WIN
#include <windows.h>
#endif

#ifdef AE_OS_MAC
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

// Global variables
AEGP_PluginID	S_my_id				= 0;
SPBasicSuite		*sP					= NULL;

// Menu command IDs
AEGP_Command		S_cmd_create_divider	= 0;
AEGP_Command		S_cmd_fold_unfold		= 0;

#ifdef AE_OS_WIN
// Windows: Mouse hook for double-click detection
// Variables are extern in WindowsHook.cpp, so must have external linkage here
HHOOK			S_mouse_hook			= NULL;
bool				S_double_click_pending	= false;
bool				S_suppress_next_action	= false;
CRITICAL_SECTION	S_cs;
bool				S_cs_initialized		= false;
bool				S_is_divider_selected	= false; // Track selection state for hook
#endif

// Idle hook state
A_long			S_idle_counter			= 0;

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

A_Err GetLayerNameStr(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, std::string& name)
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
			// FIX: Add safety limit to prevent infinite loops on corrupted UTF-16 data
			const int MAX_UTF16_CHARS = 1024;  // Reasonable limit for layer names
			int charCount = 0;
			while (*nameP && charCount < MAX_UTF16_CHARS) {
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
				charCount++;
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

A_Err SetLayerNameStr(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& name)
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

// Build divider name with visual prefix for fold state
// Fold state: ▸ (folded), ▾ (unfolded)
// Format: ▾(hierarchy) name or ▾ name for top level
std::string BuildDividerName(bool folded, const std::string& hierarchy, const std::string& name)
{
	// Add visual prefix for fold state
	std::string result = folded ? PREFIX_FOLDED : PREFIX_UNFOLDED;

	// Add hierarchy marker if present (e.g., "▾(1/A) Group")
	if (!hierarchy.empty()) {
		result += "(" + hierarchy + ") ";
	} else {
		result += " ";
	}

	result += name;

	// Add length limit to prevent excessive string growth
	const size_t MAX_NAME_LENGTH = 1024;
	if (result.length() > MAX_NAME_LENGTH) {
		return result.substr(0, MAX_NAME_LENGTH) + "...";
	}
	return result;
}


// Helper to find child by match name (safe for all group types)
// Helper to find child by match name
// Note: Do NOT use this for Layer Root (Named Group), use AEGP_GetNewStreamRefByMatchname directly instead.
// This is safe for Contents/Vector Groups where custom streams might exist.
A_Err FindStreamByMatchName(AEGP_SuiteHandler& suites, AEGP_StreamRefH parentH, const char* matchName, AEGP_StreamRefH* outStreamH)
{
    *outStreamH = NULL;
    A_long count = 0;
    A_Err err = A_Err_NONE;
    ERR(suites.DynamicStreamSuite4()->AEGP_GetNumStreamsInGroup(parentH, &count));
    
    for (A_long i=0; i<count && !err && !*outStreamH; ++i) {
        AEGP_StreamRefH childH = NULL;
        if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByIndex(S_my_id, parentH, i, &childH) == A_Err_NONE && childH) {
            char buf[AEGP_MAX_STREAM_MATCH_NAME_SIZE + 1];
            if (suites.DynamicStreamSuite4()->AEGP_GetMatchName(childH, buf) == A_Err_NONE) {
                if (strcmp(buf, matchName) == 0) {
                    *outStreamH = childH; 
                }
            }
            if (!*outStreamH) {
                suites.StreamSuite4()->AEGP_DisposeStream(childH);
            }
        }
    }
    return *outStreamH ? A_Err_NONE : A_Err_GENERIC;
}

// Get hierarchy from hidden FD-H: group for rename recovery
// CRITICAL FIX: Added bounds checking to prevent infinite loops and buffer overflows
std::string GetHierarchyFromHiddenGroup(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
    if (!layerH) return "";

    // Check layer type
    AEGP_ObjectType layerType;
    if (suites.LayerSuite9()->AEGP_GetLayerObjectType(layerH, &layerType) != A_Err_NONE || layerType != AEGP_ObjectType_VECTOR) {
        return "";
    }

    AEGP_StreamRefH rootStreamH = NULL;
    if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefForLayer(S_my_id, layerH, &rootStreamH) != A_Err_NONE) {
        return "";
    }

    std::string hierarchy;
    if (rootStreamH) {
        AEGP_StreamRefH contentsStreamH = NULL;
        if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(S_my_id, rootStreamH, "ADBE Root Vectors Group", &contentsStreamH) == A_Err_NONE && contentsStreamH) {
            A_long numStreams = 0;
            if (suites.DynamicStreamSuite4()->AEGP_GetNumStreamsInGroup(contentsStreamH, &numStreams) == A_Err_NONE) {
                // CRITICAL FIX: Limit iteration count to prevent excessive processing
                const A_long MAX_STREAMS_TO_SCAN = 1000;
                A_long streamsToScan = (numStreams < MAX_STREAMS_TO_SCAN) ? numStreams : MAX_STREAMS_TO_SCAN;

                for (A_long i = 0; i < streamsToScan && hierarchy.empty(); i++) {
                    AEGP_StreamRefH childH = NULL;
                    if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByIndex(S_my_id, contentsStreamH, i, &childH) == A_Err_NONE && childH) {
                        AEGP_MemHandle nameH = NULL;
                        if (suites.StreamSuite4()->AEGP_GetStreamName(S_my_id, childH, FALSE, &nameH) == A_Err_NONE && nameH) {
                            void* dataP = NULL;
                            if (suites.MemorySuite1()->AEGP_LockMemHandle(nameH, &dataP) == A_Err_NONE && dataP) {
                                const A_u_short* name16 = (const A_u_short*)dataP;
                                // Check for "FD-H:" prefix (FD-H:xxx)
                                // CRITICAL FIX: Add bounds checking before array access
                                if (name16[0] == 'F' && name16[1] == 'D' && name16[2] == '-' &&
                                    name16[3] == 'H' && name16[4] == ':') {
                                    // Extract hierarchy after "FD-H:"
                                    // CRITICAL FIX: Add safety limit to UTF-16 loop
                                    const int MAX_HIERARCHY_CHARS = 256;  // Reasonable limit
                                    std::string hierStr;
                                    int j = 5; // Skip "FD-H:"
                                    while (name16[j] && j < MAX_HIERARCHY_CHARS + 5) {
                                        hierStr += (char)name16[j];
                                        j++;
                                    }
                                    // CRITICAL FIX: Only use hierarchy if we found proper termination
                                    if (!name16[j]) {  // Proper null-termination
                                        hierarchy = hierStr;
                                    }
                                }
                                suites.MemorySuite1()->AEGP_UnlockMemHandle(nameH);
                            }
                            suites.MemorySuite1()->AEGP_FreeMemHandle(nameH);
                        }
                        suites.StreamSuite4()->AEGP_DisposeStream(childH);
                    }
                }
            }
            suites.StreamSuite4()->AEGP_DisposeStream(contentsStreamH);
        }
        suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
    }

    return hierarchy;
}

// Check if layer has specific stream/group "FoldGroupData"
bool HasDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
	if (!layerH) return false;
    
    // Check layer type: only Vector layers have "ADBE Root Vectors Group"
    AEGP_ObjectType layerType;
    if (suites.LayerSuite9()->AEGP_GetLayerObjectType(layerH, &layerType) != A_Err_NONE || layerType != AEGP_ObjectType_VECTOR) {
        return false;
    }
	
	bool hasIdentity = false;
	
	AEGP_StreamRefH rootStreamH = NULL;
	
	// Get root stream using DynamicStreamSuite
	if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefForLayer(S_my_id, layerH, &rootStreamH) != A_Err_NONE) {
		return false;
	}
	
	if (rootStreamH) {
		// Look for Contents group safely
		AEGP_StreamRefH contentsStreamH = NULL;
		if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(S_my_id, rootStreamH, "ADBE Root Vectors Group", &contentsStreamH) == A_Err_NONE && contentsStreamH) {
			
			// Search inside Contents
            // Contents usually stores groups directly.
			A_long numStreams = 0;
			if (suites.DynamicStreamSuite4()->AEGP_GetNumStreamsInGroup(contentsStreamH, &numStreams) == A_Err_NONE) {
				for (A_long i = 0; i < numStreams && !hasIdentity; i++) {
					AEGP_StreamRefH childStreamH = NULL;
					if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByIndex(S_my_id, contentsStreamH, i, &childStreamH) == A_Err_NONE && childStreamH) {
						AEGP_StreamGroupingType groupType;
						if (suites.DynamicStreamSuite4()->AEGP_GetStreamGroupingType(childStreamH, &groupType) == A_Err_NONE) {
							if (groupType == AEGP_StreamGroupingType_NAMED_GROUP) {
								AEGP_MemHandle nameH = NULL;
								if (suites.StreamSuite4()->AEGP_GetStreamName(S_my_id, childStreamH, FALSE, &nameH) == A_Err_NONE && nameH) {
									void* dataP = NULL;
									if (suites.MemorySuite1()->AEGP_LockMemHandle(nameH, &dataP) == A_Err_NONE && dataP) {
                                        // Check for "FD-" prefix
                                        const A_u_short* name16 = (const A_u_short*)dataP;
                                        bool match = true;
                                        const char* target = "FD-";
                                        for (int k=0; k<3; k++) {
                                            if (!name16[k] || name16[k] != (A_u_short)target[k]) {
                                                match = false; break;
                                            }
                                        }
                                        if (match) hasIdentity = true;
                                        
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
			suites.StreamSuite4()->AEGP_DisposeStream(contentsStreamH);
		}
		
		suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
	}
	
	return hasIdentity;
}

// Add identification group to layer
// If hierarchy is provided, stores it in a separate group "FD-H:xxx" for rename recovery
A_Err AddDividerIdentity(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& hierarchy = "")
{
	A_Err err = A_Err_NONE;
	if (!layerH) return A_Err_STRUCT;

    // Check layer type: only Vector layers have "ADBE Root Vectors Group"
    AEGP_ObjectType layerType;
    if (suites.LayerSuite9()->AEGP_GetLayerObjectType(layerH, &layerType) != A_Err_NONE || layerType != AEGP_ObjectType_VECTOR) {
        return A_Err_NONE;
    }

	AEGP_StreamRefH rootStreamH = NULL;
	// Use DynamicStreamSuite to get layer root
	err = suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefForLayer(S_my_id, layerH, &rootStreamH);
	if (err) {
		char errBuf[128];
#ifdef AE_OS_WIN
		sprintf_s(errBuf, sizeof(errBuf), "FoldLayers Debug: Failed to get Layer Stream (Err: %d)", err);
#else
		snprintf(errBuf, sizeof(errBuf), "FoldLayers Debug: Failed to get Layer Stream (Err: %d)", err);
#endif
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, errBuf);
		return err;
	}

	if (!err && rootStreamH) {
        // Get Contents Group safely
        AEGP_StreamRefH contentsStreamH = NULL;
        err = suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(S_my_id, rootStreamH, "ADBE Root Vectors Group", &contentsStreamH);

        if (!err && contentsStreamH) {
            // 1. Create/Update fold state group "FD-0" (Unfolded)
            AEGP_StreamRefH newGroupH = NULL;
            ERR(suites.DynamicStreamSuite4()->AEGP_AddStream(S_my_id, contentsStreamH, "ADBE Vector Group", &newGroupH));

            if (!err && newGroupH) {
                // Rename it to "FD-0" (Unfolded) using UTF-16
                A_UTF16Char name16[] = {'F','D','-','0', 0};
                ERR(suites.DynamicStreamSuite4()->AEGP_SetStreamName(newGroupH, name16));

                suites.StreamSuite4()->AEGP_DisposeStream(newGroupH);
            } else {
                suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers Debug: Failed to add stream to Contents");
            }

            // 2. Create/Update hierarchy group "FD-H:xxx" for rename recovery
            if (!hierarchy.empty()) {
                bool foundHierGroup = false;
                AEGP_StreamRefH hierGroupH = NULL;
                // First, try to find existing hierarchy group
                A_long numStreams = 0;
                if (suites.DynamicStreamSuite4()->AEGP_GetNumStreamsInGroup(contentsStreamH, &numStreams) == A_Err_NONE) {
                    for (A_long i = 0; i < numStreams && !foundHierGroup; i++) {
                        AEGP_StreamRefH childH = NULL;
                        if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByIndex(S_my_id, contentsStreamH, i, &childH) == A_Err_NONE && childH) {
                            AEGP_MemHandle nameH = NULL;
                            if (suites.StreamSuite4()->AEGP_GetStreamName(S_my_id, childH, FALSE, &nameH) == A_Err_NONE && nameH) {
                                void* dataP = NULL;
                                if (suites.MemorySuite1()->AEGP_LockMemHandle(nameH, &dataP) == A_Err_NONE && dataP) {
                                    const A_u_short* name16 = (const A_u_short*)dataP;
                                    // Check for "FD-H:" prefix
                                    if (name16[0] == 'F' && name16[1] == 'D' && name16[2] == '-' &&
                                        name16[3] == 'H' && name16[4] == ':') {
                                        // Found existing hierarchy group - update it
                                        std::string newName = "FD-H:" + hierarchy;
                                        std::vector<A_UTF16Char> newName16;
                                        for (char c : newName) newName16.push_back(c);
                                        newName16.push_back(0);
                                        suites.DynamicStreamSuite4()->AEGP_SetStreamName(childH, newName16.data());
                                        foundHierGroup = true;
                                    }
                                    suites.MemorySuite1()->AEGP_UnlockMemHandle(nameH);
                                }
                                suites.MemorySuite1()->AEGP_FreeMemHandle(nameH);
                            }
                            if (!foundHierGroup) {
                                suites.StreamSuite4()->AEGP_DisposeStream(childH);
                            } else {
                                suites.StreamSuite4()->AEGP_DisposeStream(childH);
                            }
                        }
                    }
                }

                // If not found, create new hierarchy group
                if (!foundHierGroup) {
                    AEGP_StreamRefH newHierGroupH = NULL;
                    ERR(suites.DynamicStreamSuite4()->AEGP_AddStream(S_my_id, contentsStreamH, "ADBE Vector Group", &newHierGroupH));
                    if (!err && newHierGroupH) {
                        std::string newName = "FD-H:" + hierarchy;
                        std::vector<A_UTF16Char> newName16;
                        for (char c : newName) newName16.push_back(c);
                        newName16.push_back(0);
                        ERR(suites.DynamicStreamSuite4()->AEGP_SetStreamName(newHierGroupH, newName16.data()));
                        suites.StreamSuite4()->AEGP_DisposeStream(newHierGroupH);
                    }
                }
            }

            suites.StreamSuite4()->AEGP_DisposeStream(contentsStreamH);
        } else {
             // If Contents not found, maybe report debug info?
             char errBuf[128];
#ifdef AE_OS_WIN
             sprintf_s(errBuf, sizeof(errBuf), "FoldLayers Debug: Failed to find Contents group (Err: %d)", err);
#else
             snprintf(errBuf, sizeof(errBuf), "FoldLayers Debug: Failed to find Contents group (Err: %d)", err);
#endif
             suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, errBuf);
        }

		suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
	}

	return err;
}


// Check if layer is a group divider
// Only layers with FD- identity (hidden stream groups) are recognized as groups
// This ensures only layers created by this plugin can be folded/unfolded
bool IsDividerLayer(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
	if (!suites.StreamSuite4()) return false; // Safety check
	return HasDividerIdentity(suites, layerH);
}

// Variant for when we already have the layer name (optimization)
// Name parameter is ignored - only FD- identity matters
// This signature is kept for compatibility with existing code
bool IsDividerLayerWithKnownName(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& name)
{
	(void)name; // Unused parameter - only FD- identity matters
	if (!suites.StreamSuite4()) return false;
	return HasDividerIdentity(suites, layerH);
}


// ----------------------------------------------------------------------------
// State Management via Hidden Streams
// ----------------------------------------------------------------------------

A_Err GetFoldGroupDataStream(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, AEGP_StreamRefH* outStreamH, bool* outIsFolded)
{
    *outStreamH = NULL;
    if (outIsFolded) *outIsFolded = true; // Default to folded if found (legacy support)
    
    // Check layer type: only Vector layers have "ADBE Root Vectors Group"
    AEGP_ObjectType layerType;
    if (suites.LayerSuite9()->AEGP_GetLayerObjectType(layerH, &layerType) != A_Err_NONE || layerType != AEGP_ObjectType_VECTOR) {
        return A_Err_NONE;
    }

    AEGP_StreamRefH rootStreamH = NULL;
    
    if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefForLayer(S_my_id, layerH, &rootStreamH) != A_Err_NONE) return A_Err_GENERIC;
    
    if (rootStreamH) {
        AEGP_StreamRefH contentsStreamH = NULL;
        if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(S_my_id, rootStreamH, "ADBE Root Vectors Group", &contentsStreamH) == A_Err_NONE && contentsStreamH) {
             A_long numStreams = 0;
             if (suites.DynamicStreamSuite4()->AEGP_GetNumStreamsInGroup(contentsStreamH, &numStreams) == A_Err_NONE) {
                 for (A_long i = 0; i < numStreams && !*outStreamH; i++) {
                     AEGP_StreamRefH childStreamH = NULL;
                     if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByIndex(S_my_id, contentsStreamH, i, &childStreamH) == A_Err_NONE && childStreamH) {
                         AEGP_MemHandle nameH = NULL;
                         if (suites.StreamSuite4()->AEGP_GetStreamName(S_my_id, childStreamH, FALSE, &nameH) == A_Err_NONE && nameH) {
                             void* dataP = NULL;
                             if (suites.MemorySuite1()->AEGP_LockMemHandle(nameH, &dataP) == A_Err_NONE && dataP) {
                                 const A_u_short* name16 = (const A_u_short*)dataP;
                                 bool match = true;
                                 const char* target = "FD-";
                                 for (int k=0; k<3; k++) {
                                     if (!name16[k] || name16[k] != (A_u_short)target[k]) {
                                         match = false; break;
                                     }
                                 }
                                 if (match) {
                                     *outStreamH = childStreamH; 
                                     if (outIsFolded) {
                                         // FD-0 (Unfolded), FD-1 (Folded). Check 3rd char (0-indexed).
                                         if (name16[3] == (A_u_short)'0') {
                                             *outIsFolded = false;
                                         }
                                     }
                                 }
                                 suites.MemorySuite1()->AEGP_UnlockMemHandle(nameH);
                             }
                             suites.MemorySuite1()->AEGP_FreeMemHandle(nameH);
                         }
                         if (!*outStreamH) {
                             suites.StreamSuite4()->AEGP_DisposeStream(childStreamH);
                         }
                     }
                 }
             }
             suites.StreamSuite4()->AEGP_DisposeStream(contentsStreamH);
        }
        suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
    }
    return *outStreamH ? A_Err_NONE : A_Err_GENERIC;
}

A_Err SetGroupState(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, bool setFolded)
{
    A_Err err = A_Err_NONE;
    AEGP_StreamRefH groupDataH = NULL;
    GetFoldGroupDataStream(suites, layerH, &groupDataH);
    
    AEGP_StreamRefH targetStreamH = groupDataH;
    
    if (!targetStreamH) {
        // Create FoldGroupData if not exists
        AEGP_StreamRefH rootStreamH = NULL;
        if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefForLayer(S_my_id, layerH, &rootStreamH) == A_Err_NONE) {
             AEGP_StreamRefH contentsStreamH = NULL;
             if (suites.DynamicStreamSuite4()->AEGP_GetNewStreamRefByMatchname(S_my_id, rootStreamH, "ADBE Root Vectors Group", &contentsStreamH) == A_Err_NONE) {
                 AEGP_StreamRefH newGroupH = NULL;
                 ERR(suites.DynamicStreamSuite4()->AEGP_AddStream(S_my_id, contentsStreamH, "ADBE Vector Group", &newGroupH));
                 if (!err && newGroupH) {
                     targetStreamH = newGroupH;
                 }
                 suites.StreamSuite4()->AEGP_DisposeStream(contentsStreamH);
             }
             suites.StreamSuite4()->AEGP_DisposeStream(rootStreamH);
        }
    }
    
    if (targetStreamH) {
        // Set Name based on state
        if (setFolded) {
             A_UTF16Char name16[] = {'F','D','-','1', 0};
             ERR(suites.DynamicStreamSuite4()->AEGP_SetStreamName(targetStreamH, name16));
        } else {
             A_UTF16Char name16[] = {'F','D','-','0', 0};
             ERR(suites.DynamicStreamSuite4()->AEGP_SetStreamName(targetStreamH, name16));
        }
        suites.StreamSuite4()->AEGP_DisposeStream(targetStreamH);
    }
    
    return err;
}

A_Err SyncLayerName(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
    // Sync layer name with current fold state from FD-0/FD-1
    // This function can be used to recover visual prefix if layer name was manually edited
    A_Err err = A_Err_NONE;

    // Get current fold state from hidden stream
    AEGP_StreamRefH dataH = NULL;
    bool isFolded = true;
    if (GetFoldGroupDataStream(suites, layerH, &dataH, &isFolded) == A_Err_NONE && dataH) {
        suites.StreamSuite4()->AEGP_DisposeStream(dataH);

        // Get current name and strip existing prefix
        std::string currentName;
        ERR(GetLayerNameStr(suites, layerH, currentName));
        if (!err) {
            std::string baseName = GetDividerName(currentName);
            std::string newName = BuildDividerName(isFolded, "", baseName);
            ERR(SetLayerNameStr(suites, layerH, newName));
        }
    }

    return err;
}

bool IsDividerFolded(AEGP_SuiteHandler& suites, AEGP_LayerH layerH)
{
    // Pure ID-based: only check FD-0/FD-1 state, no name fallback
    AEGP_StreamRefH dataH = NULL;
    bool folded = true;  // Default to folded for safety
    if (GetFoldGroupDataStream(suites, layerH, &dataH, &folded) == A_Err_NONE && dataH) {
        suites.StreamSuite4()->AEGP_DisposeStream(dataH);
        return folded;
    }
    // No FD data stream found - default to folded state
    return true;
}

// Parse hierarchy from name
// NOTE: Hierarchy is now stored in FD-H: group only, not in layer name
// GetHierarchy, GetHierarchyDepth, and GetDividerName are implemented in GroupParser.cpp
// This avoids ODR violations and maintains proper module separation


//=============================================================================
// Core Functionality
//=============================================================================

A_Err GetActiveComp(AEGP_SuiteHandler& suites, AEGP_CompH* compH)
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
// Pure ID-based: uses FD-H: group for hierarchy information
A_Err GetGroupLayers(AEGP_SuiteHandler& suites, AEGP_CompH compH,
                            A_long dividerIndex, const std::string& dividerHierarchy,
                            std::vector<AEGP_LayerH>& groupLayers)
{
	A_Err err = A_Err_NONE;
	A_long numLayers = 0;
	int myDepth = GetHierarchyDepth(dividerHierarchy);

	// Check for invalid hierarchy depth
	if (myDepth < 0) {
		return A_Err_GENERIC;
	}

	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));

	for (A_long i = dividerIndex + 1; i < numLayers && !err; i++) {
		AEGP_LayerH layer = NULL;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer) {
			if (IsDividerLayer(suites, layer)) {
				// Pure ID-based: get hierarchy from FD-H: group
				std::string otherHierarchy = GetHierarchyFromHiddenGroup(suites, layer);
				int otherDepth = GetHierarchyDepth(otherHierarchy);

				// Check for invalid sub-hierarchy depth
				if (otherDepth < 0) {
					err = A_Err_GENERIC;
					break;
				}

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




// Fold/unfold a divider
// Updates FD-0/FD-1 hidden stream AND layer name with visual prefix (▸/▾)
A_Err FoldDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH,
                         AEGP_LayerH dividerLayer, A_long dividerIndex, bool fold)
{
	A_Err err = A_Err_NONE;

	if (!dividerLayer || !compH) return A_Err_STRUCT;

	// Validate hierarchy depth before proceeding
	std::string hierarchy = GetHierarchyFromHiddenGroup(suites, dividerLayer);
	int depth = GetHierarchyDepth(hierarchy);
	if (depth < 0) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Invalid hierarchy depth - cannot fold/unfold group.");
		return A_Err_GENERIC;
	}

	// Persist State - check if this fails
	A_Err stateErr = SetGroupState(suites, dividerLayer, fold);
	if (stateErr) {
		// SetGroupState failed - report error but don't proceed
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to set fold state - operation cancelled.");
		return stateErr;
	}

	// Update layer name to show visual fold state (▸ for folded, ▾ for unfolded)
	std::string currentName;
	ERR(GetLayerNameStr(suites, dividerLayer, currentName));
	std::string baseName = GetDividerName(currentName); // Strip existing prefix if any
	std::string originalName = currentName; // Store for rollback
	std::string newName = BuildDividerName(fold, hierarchy, baseName); // Include hierarchy in name
	ERR(SetLayerNameStr(suites, dividerLayer, newName));

	// Get group layers
	std::vector<AEGP_LayerH> groupLayers;
	ERR(GetGroupLayers(suites, compH, dividerIndex, hierarchy, groupLayers));
	if (err) {
		// Failed to get group layers - rollback state
		SetGroupState(suites, dividerLayer, !fold);
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to get group layers - changes rolled back.");
		return err;
	}

	// Track modified layers for rollback
	std::vector<AEGP_LayerH> modifiedLayers;
	std::vector<A_Boolean> originalShyStates;

	// Store original shy states for rollback
	for (size_t i = 0; i < groupLayers.size(); i++) {
		AEGP_LayerH subLayer = groupLayers[i];
		if (subLayer) {
			AEGP_LayerFlags flags;
			if (suites.LayerSuite9()->AEGP_GetLayerFlags(subLayer, &flags) == A_Err_NONE) {
				A_Boolean shyFlag = (flags & AEGP_LayerFlag_SHY) != 0;
				modifiedLayers.push_back(subLayer);
				originalShyStates.push_back(shyFlag);
			}
		}
	}

	// Apply fold/unfold to group layers
	int skipUntilDepth = -1; // -1 means do not skip

	for (size_t i = 0; i < groupLayers.size() && !err; i++) {
		AEGP_LayerH subLayer = groupLayers[i];

		std::string subName;
		ERR(GetLayerNameStr(suites, subLayer, subName));
		const bool subIsDivider = IsDividerLayerWithKnownName(suites, subLayer, subName);
		std::string subHier;
		int subDepth = 0;
		if (subIsDivider) {
			// Pure ID-based: get hierarchy from FD-H: group, not from name
			subHier = GetHierarchyFromHiddenGroup(suites, subLayer);
			subDepth = GetHierarchyDepth(subHier);
			// Validate sub-hierarchy depth
			if (subDepth < 0) {
				err = A_Err_GENERIC;
				break;
			}
		}

		bool shouldHide = false;

		if (fold) {
			shouldHide = true;
		} else {
			if (skipUntilDepth != -1) {
				// When unfolding a parent group, keep the contents of folded nested
				// dividers hidden. Non-divider layers don't carry hierarchy markers,
				// so rely on divider boundaries to decide when the skip ends.
				if (!subIsDivider) {
					shouldHide = true;
				} else if (subDepth > skipUntilDepth) {
					shouldHide = true;
				} else {
					skipUntilDepth = -1;
				}
			}

			if (skipUntilDepth == -1) {
				shouldHide = false;
				if (subIsDivider) {
					if (IsDividerFolded(suites, subLayer)) {
						skipUntilDepth = subDepth;
					}
				}
			}
		}

		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(subLayer, AEGP_LayerFlag_SHY, shouldHide ? TRUE : FALSE));
	}

	// If error occurred, rollback all changes
	if (err) {
		// Rollback shy states
		for (size_t i = 0; i < modifiedLayers.size(); i++) {
			if (i < originalShyStates.size()) {
				suites.LayerSuite9()->AEGP_SetLayerFlag(modifiedLayers[i], AEGP_LayerFlag_SHY, originalShyStates[i]);
			}
		}
		// Rollback state and layer name
		SetGroupState(suites, dividerLayer, !fold);
		SetLayerNameStr(suites, dividerLayer, originalName); // Restore original name
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Error during fold/unfold - all changes rolled back.");
	}

	return err;
}

// Toggle a single divider
A_Err ToggleDivider(AEGP_SuiteHandler& suites, AEGP_CompH compH, 
                           AEGP_LayerH layerH, A_long layerIndex)
{
	A_Err err = A_Err_NONE;
	
	if (IsDividerLayer(suites, layerH)) {
		bool isFolded = IsDividerFolded(suites, layerH);
		ERR(FoldDivider(suites, compH, layerH, layerIndex, !isFolded));
	}
	
	return err;
}

// Get all dividers in the composition
A_Err GetAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH,
                            std::vector<std::pair<AEGP_LayerH, A_long> >& dividers)
{
	A_Err err = A_Err_NONE;
	A_long numLayers = 0;

	ERR(suites.LayerSuite9()->AEGP_GetCompNumLayers(compH, &numLayers));

	for (A_long i = 0; i < numLayers && !err; i++) {
		AEGP_LayerH layer = NULL;
		ERR(suites.LayerSuite9()->AEGP_GetCompLayerByIndex(compH, i, &layer));
		if (!err && layer && IsDividerLayer(suites, layer)) {
			dividers.push_back(std::make_pair(layer, i));
		}
	}

	return err;
}

// Check if any divider is selected
A_Err IsDividerSelected(AEGP_SuiteHandler& suites, AEGP_CompH compH, bool* result)
{
	A_Err err = A_Err_NONE;
	*result = false;

	AEGP_Collection2H collectionH = NULL;
	A_u_long numSelected = 0;

	ERR(suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH));
	if (!err && collectionH) {
		ERR(suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected));

		for (A_u_long i = 0; i < numSelected && !err && !*result; i++) {
			AEGP_CollectionItemV2 item;
			ERR(suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, i, &item));
			if (!err && item.type == AEGP_CollectionItemType_LAYER) {
				if (IsDividerLayer(suites, item.u.layer.layerH)) {
					*result = true;
				}
			}
		}

		suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
	}

	return err;
}

// Toggle selected dividers only
A_Err ToggleSelectedDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH)
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
					if (IsDividerLayer(suites, item.u.layer.layerH)) {
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
A_Err ToggleAllDividers(AEGP_SuiteHandler& suites, AEGP_CompH compH)
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

//=============================================================================
// Command Handlers
//=============================================================================

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
				if (!err && IsDividerLayer(suites, item.u.layer.layerH)) {
					// Pure ID-based: get hierarchy from FD-H: group, not from name
					std::string selHierarchy = GetHierarchyFromHiddenGroup(suites, item.u.layer.layerH);

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
						// Pure ID-based: get hierarchy from FD-H: group, not from name
						std::string divHier = GetHierarchyFromHiddenGroup(suites, div.first);
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
	
	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Create Group Layer"));

	// Create SHAPE layer
	AEGP_LayerH newLayer = NULL;
	ERR(suites.CompSuite11()->AEGP_CreateVectorLayerInComp(compH, &newLayer));

	if (!err && newLayer) {
		// Set layer name with prefix to show unfolded state and hierarchy
		// New groups are always created in unfolded state
		// Include parent hierarchy if present (e.g., "▾(1/A) Group" for nested groups)
		std::string dividerName = BuildDividerName(false, parentHierarchy, "Group");
		ERR(SetLayerNameStr(suites, newLayer, dividerName));
		if (err) {
			suites.UtilitySuite6()->AEGP_EndUndoGroup();
			return err;
		}

		// Move to insert position.
		// - If a layer is selected: place the divider directly below it.
		// - If nothing is selected: place the divider at the top (index 0).
		// Note: This project treats layer indices as 0-based (see GetCompLayerByIndex loops).
		const A_long targetIndex = (insertIndex >= 0) ? (insertIndex + 1) : 0;
		ERR(suites.LayerSuite9()->AEGP_ReorderLayer(newLayer, targetIndex));
		if (err) {
			suites.UtilitySuite6()->AEGP_EndUndoGroup();
			return err;
		}

		// CRITICAL FIX: After reorder operation, verify handle is still valid
		// by attempting a basic operation. If it fails, we need to report an error.
		AEGP_LayerFlags flags;
		A_Err verifyErr = suites.LayerSuite9()->AEGP_GetLayerFlags(newLayer, &flags);
		A_Boolean videoActive = (flags & AEGP_LayerFlag_VIDEO_ACTIVE) != 0;
		if (verifyErr) {
			suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Layer handle became invalid after reorder - operation failed.");
			suites.UtilitySuite6()->AEGP_EndUndoGroup();
			return verifyErr;
		}

		// Set VIDEO OFF (invisible)
		ERR(suites.LayerSuite9()->AEGP_SetLayerFlag(newLayer, AEGP_LayerFlag_VIDEO_ACTIVE, FALSE));

		// Set label to 0 (None)
		ERR(suites.LayerSuite9()->AEGP_SetLayerLabel(newLayer, 0));

		// Add identity group with hierarchy info
		ERR(AddDividerIdentity(suites, newLayer, parentHierarchy));
	}

	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());

	// Enable shy mode after creating group (outside UndoGroup for reliable script execution)
	ERR(EnsureShyModeEnabled(suites));

	return err;
}

// Enable Hide Shy Layers for the active composition
// Uses AEGP_DynamicStreamSuite if available, otherwise falls back to ExtendScript
A_Err EnsureShyModeEnabled(AEGP_SuiteHandler& suites)
{
    AEGP_CompH compH = NULL;
    A_Err err = GetActiveComp(suites, &compH);
    if (!compH || err != A_Err_NONE) return err;

    // Try to get comp settings stream to check/set hideShyLayers directly
    // Note: This approach may not be available in all SDK versions
    // Fall back to ExtendScript which is more universally available

    // Use ExtendScript to enable hideShyLayers
    // This must be called OUTSIDE of UndoGroup for reliable execution
    // Note: Script must be on a single line for AEGP_ExecuteScript on macOS
    const char* script =
        "try { if (app.project.activeItem && app.project.activeItem instanceof CompItem) { var comp = app.project.activeItem; if (!comp.hideShyLayers) { comp.hideShyLayers = true; } } } catch(e) { e.toString(); }";

    AEGP_MemHandle resultH = NULL;
    AEGP_MemHandle errorH = NULL;

    err = suites.UtilitySuite6()->AEGP_ExecuteScript(S_my_id, script, FALSE, &resultH, &errorH);

    // Report any script errors for debugging
    if (errorH) {
        void* errorP = NULL;
        if (suites.MemorySuite1()->AEGP_LockMemHandle(errorH, &errorP) == A_Err_NONE && errorP) {
            const char* errorStr = (const char*)errorP;
            if (errorStr && *errorStr) {
                suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, errorStr);
            }
            suites.MemorySuite1()->AEGP_UnlockMemHandle(errorH);
        }
        suites.MemorySuite1()->AEGP_FreeMemHandle(errorH);
    }

    if (resultH) suites.MemorySuite1()->AEGP_FreeMemHandle(resultH);

    return err;
}

A_Err DoFoldUnfold(AEGP_SuiteHandler& suites)
{
	A_Err err = A_Err_NONE;
	AEGP_CompH compH = NULL;

	ERR(GetActiveComp(suites, &compH));
	if (!compH) return A_Err_NONE;
	if (err) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to get active composition");
		return err;
	}

	// Check if any divider is selected
	bool dividerSelected = false;
	ERR(IsDividerSelected(suites, compH, &dividerSelected));
	if (err) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to check selection");
		return err;
	}

	ERR(suites.UtilitySuite6()->AEGP_StartUndoGroup("Fold/Unfold"));
	if (err) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to start undo group");
		return err;
	}

	if (dividerSelected) {
		// Toggle only selected dividers
		ERR(ToggleSelectedDividers(suites, compH));
		if (err) {
			suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to toggle selected dividers");
			suites.UtilitySuite6()->AEGP_EndUndoGroup(); // Clean up
			return err;
		}
	} else {
		// Toggle all dividers
		ERR(ToggleAllDividers(suites, compH));
		if (err) {
			suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to toggle all dividers");
			suites.UtilitySuite6()->AEGP_EndUndoGroup(); // Clean up
			return err;
		}
	}

	ERR(suites.UtilitySuite6()->AEGP_EndUndoGroup());
	if (err) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Failed to end undo group");
		return err;
	}

	// IMPORTANT: Call EnsureShyModeEnabled AFTER ending the UndoGroup
	// ExtendScript execution may be restricted inside UndoGroup
	// This ensures Hide Shy Layers is enabled for the fold/unfold to be visible
	// Note: Shy mode enabling is critical for fold/unfold to be visible to users
	A_Err shyErr = EnsureShyModeEnabled(suites);
	if (shyErr) {
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Warning - Could not enable Hide Shy Layers mode. Please enable it manually in the composition panel.");
		// Still return success for fold/unfold operation, but warn the user
	}

	return err;
}

//=============================================================================
// Windows: Mouse hook for double-click detection
//=============================================================================

#ifdef AE_OS_WIN

// Forward declaration
A_Err ProcessDoubleClick();

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
					if (IsDividerLayer(suites, item.u.layer.layerH)) {
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

#ifdef AE_OS_MAC
// Global mouse state tracking for higher frequency polling
bool S_pending_fold_action = false;
double S_last_left_down_event_ts = 0.0;
double S_last_click_event_ts = 0.0;

CFMachPortRef S_event_tap = NULL;
CFRunLoopSourceRef S_event_tap_source = NULL;
bool S_event_tap_active = false;

pthread_mutex_t S_mac_state_mutex = PTHREAD_MUTEX_INITIALIZER;
bool S_mac_divider_selected_for_input = false;
std::string S_mac_selected_divider_full_name;
bool S_mac_selected_divider_valid = false;
double S_mac_selected_divider_cached_at = 0.0;
bool S_mac_should_warn_ax = false;
bool S_mac_warned_ax = false;
// S_mac_ax_hit_test_usable is protected by S_mac_state_mutex to ensure thread safety.
// This flag tracks whether Accessibility hit-testing is reliable in the current environment.
bool S_mac_ax_hit_test_usable = true;

bool MacAXTrusted()
{
	// Do not cache permanently: users often toggle Accessibility permissions
	// while AE is running, and we must reflect that immediately.
	return AXIsProcessTrusted() ? true : false;
}

static bool CFStringContainsDividerName(CFStringRef s, const std::string& fullName)
{
	if (!s) return false;
	char buf[2048];
	if (!CFStringGetCString(s, buf, (CFIndex)sizeof(buf), kCFStringEncodingUTF8)) return false;
	const std::string hay(buf);
	if (!fullName.empty() && hay.find(fullName) != std::string::npos) return true;
	return false;
}

bool MacHitTestLooksLikeSelectedDivider(CGPoint globalPos)
{
	// Best-effort: If we can use Accessibility to find the UI element under the
	// mouse, only allow the toggle when that element appears to reference the
	// selected divider's name. If this fails, return false to avoid false positives.
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
		// Even when trusted, some environments return errors. Mark as unusable so
		// we can fall back to selection-only behavior instead of "never toggling".
		pthread_mutex_lock(&S_mac_state_mutex);
		S_mac_ax_hit_test_usable = false;
		pthread_mutex_unlock(&S_mac_state_mutex);
		return false;
	}
	pthread_mutex_lock(&S_mac_state_mutex);
	S_mac_ax_hit_test_usable = true;
	pthread_mutex_unlock(&S_mac_state_mutex);

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
				// Without Accessibility permission we cannot reliably hit-test the UI element.
				// Fall back to selection-only behavior (may toggle from elsewhere); keep swallow to suppress beep.
				pthread_mutex_lock(&S_mac_state_mutex);
				S_mac_should_warn_ax = true;
				pthread_mutex_unlock(&S_mac_state_mutex);

				S_pending_fold_action = true;
				S_last_click_event_ts = 0.0;
				return NULL;
			}

			// If hit-testing isn't usable, fall back to selection-only behavior so
			// the feature still works (but may toggle from elsewhere).
			// CRITICAL FIX: Read S_mac_ax_hit_test_usable with mutex protection
			pthread_mutex_lock(&S_mac_state_mutex);
			const bool axHitTestUsable = S_mac_ax_hit_test_usable;
			pthread_mutex_unlock(&S_mac_state_mutex);

			if (!axHitTestUsable || MacHitTestLooksLikeSelectedDivider(loc)) {
			// Suppress AE's default double-click handling (and its "beep"),
			// and route the action to our fold/unfold logic.
			S_pending_fold_action = true;
			S_last_click_event_ts = 0.0; // ensure polling fallback won't re-trigger
			return NULL; // swallow event
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
		// Event tap creation failed - likely due to missing Accessibility permissions
		// Polling fallback will be used instead
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

void PollMouseState() {
	// If the event tap is active, it becomes the source of truth for double-click
	// detection/suppression. The polling fallback is intentionally disabled to
	// avoid triggering toggles from unrelated double-clicks elsewhere in AE UI.
	if (S_event_tap_active) return;

	// Check if a divider is currently selected
	pthread_mutex_lock(&S_mac_state_mutex);
	const bool dividerSelected = S_mac_divider_selected_for_input;
	pthread_mutex_unlock(&S_mac_state_mutex);

	// DEBUG: Report event tap status and selection (remove after fixing)
	static int debugCounter = 0;
	AEGP_SuiteHandler suites(sP);
	if ((debugCounter++ % 100) == 0) {
		pthread_mutex_lock(&S_mac_state_mutex);
		const bool eventTapActive = S_event_tap_active;
		pthread_mutex_unlock(&S_mac_state_mutex);
		char debugBuf[256];
		snprintf(debugBuf, sizeof(debugBuf), "FoldLayers Debug: eventTap=%d, dividerSelected=%d",
				 (int)eventTapActive, (int)dividerSelected);
		suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, debugBuf);
	}

	if (!dividerSelected) {
		return;  // No divider selected, skip double-click detection
	}

	// Polling-based approach: use the timestamp of the most recent left-mouse-down
	// event, derived from "seconds since last event". This avoids missing clicks
	// due to low polling frequency and avoids using CPU-time (clock()).
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
			S_last_click_event_ts = 0.0; // prevent rapid multi-trigger (e.g., triple-click)
			// DEBUG: Report double-click detection (remove after fixing)
			suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Polling detected double-click");
		}
	}
}
#endif

static A_Err IdleHook(
	AEGP_GlobalRefcon	plugin_refconPV,
	AEGP_IdleRefcon		refconPV,
	A_long				*max_sleepPL)
{
	(void)plugin_refconPV;  // Unused parameter
	(void)refconPV;         // Unused parameter

	S_idle_counter++;
	A_Err err = A_Err_NONE;

	AEGP_SuiteHandler suites(sP);
	
	AEGP_CompH compH = NULL;
	if (GetActiveComp(suites, &compH) != A_Err_NONE || !compH) {
         *max_sleepPL = 200;
         return A_Err_NONE;
    }

	// Default to not selected
	bool dividerSelected = false;
	IsDividerSelected(suites, compH, &dividerSelected);
	
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

#ifdef AE_OS_MAC
	// Cache selected divider name for hit-testing in the event tap.
	pthread_mutex_lock(&S_mac_state_mutex);
	S_mac_divider_selected_for_input = dividerSelected;
	pthread_mutex_unlock(&S_mac_state_mutex);

	if (dividerSelected) {
		AEGP_Collection2H collectionH = NULL;
		A_u_long numSelected = 0;
		if (suites.CompSuite11()->AEGP_GetNewCollectionFromCompSelection(S_my_id, compH, &collectionH) == A_Err_NONE && collectionH) {
			suites.CollectionSuite2()->AEGP_GetCollectionNumItems(collectionH, &numSelected);
			if (numSelected == 1) {
				AEGP_CollectionItemV2 item;
				if (suites.CollectionSuite2()->AEGP_GetCollectionItemByIndex(collectionH, 0, &item) == A_Err_NONE &&
					item.type == AEGP_CollectionItemType_LAYER) {
					std::string name;
					if (GetLayerNameStr(suites, item.u.layer.layerH, name) == A_Err_NONE &&
						IsDividerLayerWithKnownName(suites, item.u.layer.layerH, name)) {
						pthread_mutex_lock(&S_mac_state_mutex);
						S_mac_selected_divider_full_name = name;
						S_mac_selected_divider_valid = true;
						S_mac_selected_divider_cached_at = CFAbsoluteTimeGetCurrent();
						pthread_mutex_unlock(&S_mac_state_mutex);
					}
				}
			}
			suites.CollectionSuite2()->AEGP_DisposeCollection(collectionH);
		}
	}

	// Install event tap (may fail if Accessibility permissions not granted)
	InstallMacEventTap();

	// Poll for mouse state as fallback when event tap is not available
	// This detects double-clicks based on mouse event timing
	PollMouseState();

    // Process pending fold action from double-click
    if (S_pending_fold_action) {
        S_pending_fold_action = false;
        // DEBUG: Report that we detected a double-click (remove after fixing)
        suites.UtilitySuite6()->AEGP_ReportInfo(S_my_id, "FoldLayers: Double-click detected");

		if (dividerSelected) {
			const bool axTrusted = MacAXTrusted();
			// CRITICAL FIX: Read S_mac_ax_hit_test_usable with mutex protection
			pthread_mutex_lock(&S_mac_state_mutex);
			const bool axHitTestUsable = S_mac_ax_hit_test_usable;
			pthread_mutex_unlock(&S_mac_state_mutex);

			if (axTrusted && axHitTestUsable) {
				// Final gate: only toggle when the double-click happened on the selected divider row.
				CGEventRef ev = CGEventCreate(NULL);
				if (ev) {
					const CGPoint loc = CGEventGetLocation(ev);
					CFRelease(ev);
					if (MacHitTestLooksLikeSelectedDivider(loc)) {
						DoFoldUnfold(suites);
					}
				}
			} else {
				// No Accessibility permission OR hit-test is unusable: allow selection-based toggle.
				DoFoldUnfold(suites);
			}
        }
    }

	// One-time guidance if we're running without Accessibility trust.
	pthread_mutex_lock(&S_mac_state_mutex);
	const bool shouldWarnAx = S_mac_should_warn_ax && !S_mac_warned_ax;
	if (shouldWarnAx) S_mac_warned_ax = true;
	pthread_mutex_unlock(&S_mac_state_mutex);
	if (shouldWarnAx) {
		suites.UtilitySuite6()->AEGP_ReportInfo(
			S_my_id,
			"FoldLayers: For accurate double-click hit-testing on Mac, enable After Effects in System Settings > Privacy & Security > Accessibility. (Without it, FoldLayers falls back to selection-only behavior.)");
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
	(void)plugin_refconPV;  // Unused parameter
	(void)refconPV;         // Unused parameter
	(void)active_window;    // Unused parameter

	A_Err err = A_Err_NONE;
#ifdef AE_OS_MAC
	InstallMacEventTap();
    PollMouseState();
    if (S_pending_fold_action) {
        S_pending_fold_action = false;
        // Try executing immediately
        AEGP_SuiteHandler suites(sP);
        AEGP_CompH compH = NULL;
        if (GetActiveComp(suites, &compH) == A_Err_NONE && compH) {
             bool sel = false;
             if (IsDividerSelected(suites, compH, &sel) == A_Err_NONE && sel) {
                 DoFoldUnfold(suites);
             }
        }
    }
#endif
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
	(void)plugin_refconPV;   // Unused parameter
	(void)refconPV;          // Unused parameter
	(void)hook_priority;     // Unused parameter
	(void)already_handledB;  // Unused parameter

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

#ifdef AE_OS_MAC
	// Best-effort install. If it fails, polling fallback still works (but can't suppress AE beep).
	InstallMacEventTap();
#endif
	
	AEGP_SuiteHandler suites(sP);
	
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_create_divider));
	ERR(suites.CommandSuite1()->AEGP_GetUniqueCommand(&S_cmd_fold_unfold));
	
	if (!err) {
		ERR(suites.CommandSuite1()->AEGP_InsertMenuCommand(
			S_cmd_create_divider,
			"Create Group Layer",
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
