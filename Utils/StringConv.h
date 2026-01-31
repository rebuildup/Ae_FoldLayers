/*******************************************************************/
/*                                                                 */
/*      FoldLayers - String Conversion Utilities                   */
/*      UTF-8 <-> UTF-16 conversion for After Effects layer names  */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef STRING_CONV_H
#define STRING_CONV_H

#include "AEConfig.h"
#include "AEGP_SuiteHandler.h"
#include <string>
#include <vector>

// Convert UTF-16 layer name to UTF-8 std::string
A_Err GetLayerNameStr(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, std::string& name);

// Convert UTF-8 std::string to UTF-16 for layer name setting
A_Err SetLayerNameStr(AEGP_SuiteHandler& suites, AEGP_LayerH layerH, const std::string& name);

#endif // STRING_CONV_H
