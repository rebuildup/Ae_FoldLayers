// FoldLayers_Strings.h
// String IDs for FoldLayers AEGP plugin

#pragma once

typedef enum {
	StrID_NONE = 0,
	
	// Menu items
	StrID_Menu_CreateDivider,
	StrID_Menu_ToggleFold,
	
	// Status messages
	StrID_DividerCreated,
	StrID_Folded,
	StrID_Unfolded,
	StrID_NoDividers,
	StrID_NoComp,
	
	// Errors
	StrID_Error_Registration,
	
	StrID_NUMTYPES
} StrIDType;

// Use different name to avoid conflict with SDK's String_Utils.h
char *FoldLayers_GetStringPtr(int strNum);

// Macro for easy access
#define FLSTR(id) FoldLayers_GetStringPtr(id)
