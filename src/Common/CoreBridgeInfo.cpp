#include "Common/CoreBridgeInfo.h"
#include "Common/version.h"

extern "C" const char* Common_GetCoreVersionString()
{
	return BUILD_VERSION_WITH_NAME_STRING;
}
