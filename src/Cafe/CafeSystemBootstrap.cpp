#include "Cafe/CafeSystemBootstrap.h"

#include "util/crypto/aes128.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "config/ActiveSettings.h"
#include "input/InputManager.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/SaveList.h"
#include "Cafe/GraphicPack/GraphicPack2.h"

#if BOOST_OS_WINDOWS
#include <Windows.h>
#include <shlobj.h>
#endif

namespace
{
// Faro: same directory layout as CemuApp::CreateDefaultMLCFiles
// (src/gui/wxgui/CemuApp.cpp) - duplicated rather than shared because that
// one is a static method of the wx-specific CemuApp class. Logs and returns
// false on failure instead of showing a message box - no first-run wizard
// on this path yet.
bool CreateDefaultMLCFiles(const fs::path& mlc)
{
	auto createDirIfMissing = [](const fs::path& path)
	{
		std::error_code ec;
		if (!fs::exists(path, ec))
			return fs::create_directories(path, ec);
		return true;
	};
	const fs::path directories[] = {
		mlc,
		mlc / "sys",
		mlc / "usr",
		mlc / "usr/title/00050000",
		mlc / "usr/title/0005000c",
		mlc / "usr/title/0005000e",
		mlc / "usr/save/00050010/1004a000/user/common/db",
		mlc / "usr/save/00050010/1004a100/user/common/db",
		mlc / "usr/save/00050010/1004a200/user/common/db",
		mlc / "sys/title/0005001b/1005c000/content",
	};
	for (auto& path : directories)
	{
		if (!createDirIfMissing(path))
			return false;
	}
	const auto langDir = fs::path(mlc).append("sys/title/0005001b/1005c000/content");
	auto langFile = fs::path(langDir).append("language.txt");
	std::error_code ec;
	if (!fs::exists(langFile, ec))
	{
		std::ofstream file(langFile);
		if (file.is_open())
		{
			const char* langStrings[] = {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "zh"};
			for (const char* lang : langStrings)
				file << fmt::format(R"("{}",)", lang) << std::endl;
		}
	}
	return true;
}

#if BOOST_OS_WINDOWS
// Faro: same source as CemuApp.cpp's GetAppDataRoamingPath() / DeterminePaths
// (Windows variant) - reimplemented here without wxStandardPaths (just
// GetModuleFileNameW, same as WindowsInitCwd in main.cpp) so this file has
// zero wx dependency.
fs::path GetAppDataRoamingPath()
{
	PWSTR path = nullptr;
	HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path);
	if (FAILED(result))
	{
		CoTaskMemFree(path);
		return {};
	}
	std::wstring wpath(path);
	CoTaskMemFree(path);
	return fs::path(wpath);
}

bool DeterminePathsMinimal(std::set<fs::path>& failedWriteAccess)
{
	wchar_t exePathBuf[4096];
	DWORD len = GetModuleFileNameW(nullptr, exePathBuf, std::size(exePathBuf));
	if (len == 0)
		return false;
	fs::path exePath(std::wstring(exePathBuf, len));

	fs::path data_path = exePath.parent_path();
	fs::path portablePath = data_path / "portable";
	bool isPortable = false;
	fs::path user_data_path, config_path, cache_path;
	std::error_code ec;
#ifdef CEMU_ALLOW_PORTABLE
	if (fs::is_directory(portablePath, ec))
	{
		isPortable = true;
		user_data_path = config_path = cache_path = portablePath;
	}
	else
#endif
	{
		fs::path roamingPath = GetAppDataRoamingPath() / "Cemu";
		user_data_path = config_path = cache_path = roamingPath;
	}
	if (!isPortable && fs::exists(exePath.parent_path() / "settings.xml", ec))
	{
		isPortable = true;
		user_data_path = config_path = cache_path = exePath.parent_path();
	}
	ActiveSettings::SetPaths(isPortable, exePath, user_data_path, config_path, cache_path, data_path, failedWriteAccess);
	return true;
}
#endif
} // namespace

bool CafeSystemBootstrap_InitializeMinimal()
{
#if !BOOST_OS_WINDOWS
	cemuLog_log(LogType::Force, "CafeSystemBootstrap_InitializeMinimal: not implemented on this platform yet");
	return false;
#else
	std::set<fs::path> failedWriteAccess;
	if (!DeterminePathsMinimal(failedWriteAccess))
	{
		cemuLog_log(LogType::Force, "CafeSystemBootstrap: failed to determine executable path");
		return false;
	}
	for (auto& path : failedWriteAccess)
		cemuLog_log(LogType::Force, "CafeSystemBootstrap: no write access to {}", _pathToUtf8(path));

	GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());
	std::error_code ec;
	bool isFirstStart = !fs::exists(ActiveSettings::GetConfigPath("settings.xml"), ec);
	if (!isFirstStart)
		GetConfigHandle().Load();

	fs::path mlcPath = ActiveSettings::GetMlcPath();
	if (!mlcPath.empty() && !CreateDefaultMLCFiles(mlcPath))
	{
		cemuLog_log(LogType::Force, "CafeSystemBootstrap: failed to create/verify MLC directory at {}", _pathToUtf8(mlcPath));
		return false;
	}
	if (isFirstStart)
		GetConfigHandle().Save();

	ActiveSettings::Init();

	AES128_init();
	PPCTimer_init();
	ExceptionHandler_Init();

	if (NetworkConfig::XMLExists())
		n_config.Load();

	GraphicPack2::LoadAll();
	InputManager::instance().load();

	CafeSystem::Initialize();

	CafeTitleList::Initialize(ActiveSettings::GetUserDataPath("title_list_cache.xml"));
	for (auto& it : GetConfig().game_paths)
		CafeTitleList::AddScanPath(_utf8ToPath(it));
	if (!mlcPath.empty())
		CafeTitleList::SetMLCPath(mlcPath);
	CafeTitleList::Refresh();

	CafeSaveList::Initialize();
	if (!mlcPath.empty())
	{
		CafeSaveList::SetMLCPath(mlcPath);
		CafeSaveList::Refresh();
	}

	LatteOverlay_init();

	return true;
#endif
}
