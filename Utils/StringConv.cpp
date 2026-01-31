/*******************************************************************/
/*                                                                 */
/*      FoldLayers - String Conversion Utilities                   */
/*      UTF-8 <-> UTF-16 conversion for After Effects layer names  */
/*                                                                 */
/*******************************************************************/

#include "StringConv.h"
#include "FoldLayers.h"

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
			const int MAX_UTF16_CHARS = 1024;
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
