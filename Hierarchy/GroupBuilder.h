/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Group Name Builder                            */
/*      Constructs divider layer names with hierarchy markers      */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef GROUP_BUILDER_H
#define GROUP_BUILDER_H

#include "AEConfig.h"
#include <string>

// Build divider name with fold state prefix, hierarchy, and base name
// Example: BuildDividerName(false, "1/A", "MyGroup") -> "▾(1/A) MyGroup"
std::string BuildDividerName(bool folded, const std::string& hierarchy, const std::string& name);

#endif // GROUP_BUILDER_H
