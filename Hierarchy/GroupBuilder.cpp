/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Group Name Builder                            */
/*      Constructs divider layer names with hierarchy markers      */
/*                                                                 */
/*******************************************************************/

#include "GroupBuilder.h"
#include "FoldLayers.h"

std::string BuildDividerName(bool folded, const std::string& hierarchy, const std::string& name)
{
	std::string result = folded ? PREFIX_FOLDED : PREFIX_UNFOLDED;
	if (!hierarchy.empty()) {
		result += "(";
		result += hierarchy;
		result += ") ";
	}
	result += name;
	return result;
}
