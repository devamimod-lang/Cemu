#include "Cafe/TitleList/TitleListBridge.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/GameInfo.h"

void CafeTitleListBridge_ForEachTitle(void (*callback)(uint64_t titleId, const char* name, void* userData), void* userData)
{
	for (TitleId titleId : CafeTitleList::GetAllTitleIds())
	{
		GameInfo2 info = CafeTitleList::GetGameInfo(titleId);
		std::string name = info.GetTitleName();
		callback((uint64_t)titleId, name.c_str(), userData);
	}
}
