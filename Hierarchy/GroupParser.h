/*******************************************************************/
/*                                                                 */
/*      FoldLayers - Group Hierarchy Parser                        */
/*      Parses hierarchy information from divider layer names       */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef GROUP_PARSER_H
#define GROUP_PARSER_H

#include "AEConfig.h"
#include <string>

// Parse hierarchy from name like "▾(1/B) Group" -> "1/B"
std::string GetHierarchy(const std::string& name);

// Get depth from hierarchy string (e.g., "1/A" -> 2, "1/A/i" -> 3)
int GetHierarchyDepth(const std::string& hierarchy);

// Get display name without prefix and hierarchy
// Example: "▾(1/B) My Group" -> "My Group"
std::string GetDividerName(const std::string& fullName);

#endif // GROUP_PARSER_H
