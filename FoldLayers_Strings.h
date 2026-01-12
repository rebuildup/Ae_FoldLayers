// FoldLayers_Strings.h
// String IDs for FoldLayers AEGP plugin

#pragma once

typedef enum {
	StrID_NONE = 0,
	
	// Menu items
	StrID_Menu_CreateGroup,
	StrID_Menu_FoldUnfold,
	StrID_Menu_DeleteGroup,
	StrID_Menu_FoldAll,
	StrID_Menu_UnfoldAll,
	
	// Status messages
	StrID_GroupCreated,
	StrID_GroupDeleted,
	StrID_Folded,
	StrID_Unfolded,
	StrID_AllFolded,
	StrID_AllUnfolded,
	
	// Errors
	StrID_Error_NoSelection,
	StrID_Error_NotAGroup,
	StrID_Error_Registration,
	
	// Group name prefix
	StrID_GroupPrefix_Folded,
	StrID_GroupPrefix_Unfolded,
	
	StrID_NUMTYPES
} StrIDType;

// Use different name to avoid conflict with SDK's String_Utils.h
char *FoldLayers_GetStringPtr(int strNum);

// Macro for easy access - use FLSTR to avoid conflict with SDK's STR macro
#define FLSTR(id) FoldLayers_GetStringPtr(id)
