// FoldLayers_Strings.cpp
// String table for FoldLayers AEGP plugin
// Developer: 361do_plugins

#include "FoldLayers.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;

static TableString g_strs[StrID_NUMTYPES] = {
	{StrID_NONE,					""},
	
	// Menu items
	{StrID_Menu_CreateDivider,		"Create Group Divider"},
	{StrID_Menu_ToggleFold,			"Fold/Unfold Groups"},
	
	// Status messages
	{StrID_DividerCreated,			"Group Divider created."},
	{StrID_Folded,					"Group folded."},
	{StrID_Unfolded,				"Group unfolded."},
	{StrID_NoDividers,				"No dividers found."},
	{StrID_NoComp,					"Please open a composition first."},
	
	// Errors
	{StrID_Error_Registration,		"Error registering menu commands."}
};

char *FoldLayers_GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}
