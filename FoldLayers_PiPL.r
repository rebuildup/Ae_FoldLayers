/*******************************************************************/
/*  FoldLayers PiPL (Plug-in Property List)                        */
/*  AEGP Plugin for After Effects                                  */
/*  Developer: 361do_plugins                                       */
/*  https://github.com/rebuildup                                   */
/*******************************************************************/

#include "AEConfig.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
	{	/* array properties */
		/* [1] */
		Kind {
			AEGP
		},
		/* [2] */
		Name {
			"FoldLayers"
		},
		/* [3] */
		Category {
			"361do_plugins"
		},
		/* [4] */
		Version {
			65536	/* 1.0 */
		},
		/* [5] Code entry points */
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EntryPointFunc"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EntryPointFunc"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EntryPointFunc"},
		CodeMacARM64 {"EntryPointFunc"},
#endif
	}
};
