/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Group Hierarchy Parser                        */
/*      Parses hierarchy information from divider layer names       */
/*                                                                 */
/*******************************************************************/

#include "GroupParser.h"
#include "FoldLayers.h"

std::string GetHierarchy(const std::string& name)
{
	// CRITICAL FIX: Reject extremely long names to prevent DoS/buffer overflow
	const size_t MAX_LAYER_NAME_LENGTH = 4096;
	if (name.length() > MAX_LAYER_NAME_LENGTH) {
		return "";
	}

	// CRITICAL FIX: Limit hierarchy extraction to prevent excessive string operations
	const size_t MAX_HIERARCHY_LENGTH = 256;

	size_t pos = 0;
	if (name.length() >= UTF8_PREFIX_BYTES && (name.substr(0, UTF8_PREFIX_BYTES) == PREFIX_FOLDED || name.substr(0, UTF8_PREFIX_BYTES) == PREFIX_UNFOLDED)) {
		pos = UTF8_PREFIX_BYTES;
		// Skip space if present after prefix
		while (pos < name.length() && name[pos] == ' ') pos++;
	}

	// Check for hierarchy marker
	if (pos < name.length() && name[pos] == '(') {
		size_t endPos = name.find(')', pos);
		if (endPos != std::string::npos) {
			// CRITICAL FIX: Validate hierarchy length before extraction
			size_t hierarchyLen = endPos - pos - 1;
			if (hierarchyLen > MAX_HIERARCHY_LENGTH) {
				return "";
			}
			// CRITICAL FIX: Validate hierarchy contains only valid characters
			std::string hierarchy = name.substr(pos + 1, hierarchyLen);
			for (char c : hierarchy) {
				// Only allow: digits, letters, forward slash
				if (!((c >= '0' && c <= '9') ||
				      (c >= 'A' && c <= 'Z') ||
				      (c >= 'a' && c <= 'z') ||
				      c == '/')) {
					return "";
				}
			}
			return hierarchy;
		}
	}

	return "";
}

int GetHierarchyDepth(const std::string& hierarchy)
{
	if (hierarchy.empty()) return 0;

	int depth = 1;
	for (char c : hierarchy) {
		if (c == '/') depth++;
	}
	return depth;
}

std::string GetDividerName(const std::string& fullName)
{
	size_t pos = 0;

	// Check if starts with Prefix (UTF8_PREFIX_BYTES bytes)
	if (fullName.length() >= UTF8_PREFIX_BYTES) {
		if (fullName.substr(0, UTF8_PREFIX_BYTES) == PREFIX_FOLDED || fullName.substr(0, UTF8_PREFIX_BYTES) == PREFIX_UNFOLDED) {
			pos = UTF8_PREFIX_BYTES;
		}
	}

	// Skip hierarchy if present
	if (pos < fullName.length() && fullName[pos] == '(') {
		size_t endPos = fullName.find(')', pos);
		if (endPos != std::string::npos) {
			pos = endPos + 1;
			// Skip space after hierarchy
			while (pos < fullName.length() && fullName[pos] == ' ') pos++;
		}
	}

	if (pos > 0 && pos >= fullName.length()) return "Group";
	if (pos == 0) return fullName;
	return fullName.substr(pos);
}
