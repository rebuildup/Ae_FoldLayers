// FoldLayers_Strings.cpp
// String table for FoldLayers AEGP plugin
// Developer: 361do_plugins

#include "FoldLayers.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
	{StrID_NONE,					""},
	
	// Menu items - will appear in Layer menu
	{StrID_Menu_CreateGroup,		"FoldLayers: Create Group"},
	{StrID_Menu_FoldUnfold,			"FoldLayers: Fold/Unfold"},
	{StrID_Menu_DeleteGroup,		"FoldLayers: Delete Group"},
	{StrID_Menu_FoldAll,			"FoldLayers: Fold All"},
	{StrID_Menu_UnfoldAll,			"FoldLayers: Unfold All"},
	
	// Status messages
	{StrID_GroupCreated,			"Group created successfully."},
	{StrID_GroupDeleted,			"Group deleted."},
	{StrID_Folded,					"Group folded."},
	{StrID_Unfolded,				"Group unfolded."},
	{StrID_AllFolded,				"All groups folded."},
	{StrID_AllUnfolded,				"All groups unfolded."},
	
	// Errors
	{StrID_Error_NoSelection,		"Please select layers to group."},
	{StrID_Error_NotAGroup,			"Selected layer is not a fold group."},
	{StrID_Error_Registration,		"Error registering menu commands."},
	
	// Group name prefixes (folded/unfolded icons)
	{StrID_GroupPrefix_Folded,		"\xE2\x96\xB6 "},  // ▶ (UTF-8)
	{StrID_GroupPrefix_Unfolded,	"\xE2\x96\xBC "}   // ▼ (UTF-8)
};

char *GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}
