#include "Common/precompiled.h"
#include "interface/WindowSystem.h"
#include <atomic>

#if BOOST_OS_WINDOWS
#include <Windows.h>
#endif

// Faro milestone 2: g_isGPUInitFinished is declared (extern) by
// CafeSystem.h and used inside CemuCafe itself (LatteThread.cpp sets it when
// GPU init finishes, CafeSystem.cpp's foreground-title path waits on it),
// but upstream DEFINES the storage in main.cpp - i.e. in the CemuBin
// executable, not in any library. Any non-CMake-exe link of CemuCafe (this
// Tauri shell) must provide the storage itself. This lives here (not in
// shared code) so CemuBin is untouched: it keeps its own definition via
// main.cpp and never links CemuGuiTauri, so there is exactly one definition
// in each final binary either way.
std::atomic_bool g_isGPUInitFinished = false;

// Faro milestone 2 (see GUI_redesign_investigacion.md): WindowSystem.h
// implementation for the Tauri shell, parallel to wxWindowSystem.cpp - NOT
// linked via the CemuGui INTERFACE target (that still resolves to
// CemuWxGui for the existing wx build). This is a separate CMake target
// (CemuGuiTauri) that the Rust/Cargo build links directly instead.
//
// Functions here fall into two groups (see the plan's own research):
// - Load-bearing (copied verbatim from wxWindowSystem.cpp where possible -
//   they were already toolkit-agnostic, just homed in a wx file): the
//   WindowInfo accessors, IsKeyDown, GetKeyCodeName, IsFullScreen.
// - Stubbed for this milestone (no wx equivalent to call into - g_mainFrame
//   doesn't exist here): ShowErrorDialog (logs instead), UpdateWindowTitles,
//   NotifyGameLoaded/NotifyGameExited/RefreshGameList (no React event push
//   yet), CaptureInput (hotkey actions - HotkeySettings itself lives under
//   gui/wxgui/input, gated on a UI that doesn't exist here), Create()
//   (nothing calls this on the Tauri path - CafeSystemBootstrap_InitializeMinimal
//   replaces it).

WindowSystem::WindowInfo g_window_info{};

namespace
{
bool g_inputConfigWindowHasFocus = false;
}

void WindowSystem::Create()
{
	// no-op: the Tauri entry point doesn't go through this - see
	// CafeSystemBootstrap_InitializeMinimal (Cafe/CafeSystemBootstrap.h).
}

void WindowSystem::ShowErrorDialog(std::string_view message, std::string_view title, std::optional<WindowSystem::ErrorCategory> /*errorId*/)
{
	cemuLog_log(LogType::Force, "[{}] {}", title.empty() ? "Error" : std::string(title), std::string(message));
}

WindowSystem::WindowInfo& WindowSystem::GetWindowInfo()
{
	return g_window_info;
}

void WindowSystem::UpdateWindowTitles(bool isIdle, bool isLoading, double fps)
{
	// no-op for now - the wx version pushes this into the frame's title bar
	// text; there's no equivalent surface on the Tauri side yet (a later
	// milestone can emit this as an event to React instead).
}

void WindowSystem::GetWindowSize(int& w, int& h)
{
	w = g_window_info.width;
	h = g_window_info.height;
}

void WindowSystem::GetPadWindowSize(int& w, int& h)
{
	w = 0;
	h = 0;
}

void WindowSystem::GetWindowPhysSize(int& w, int& h)
{
	w = g_window_info.phys_width;
	h = g_window_info.phys_height;
}

void WindowSystem::GetPadWindowPhysSize(int& w, int& h)
{
	w = 0;
	h = 0;
}

double WindowSystem::GetWindowDPIScale()
{
	return g_window_info.dpi_scale;
}

double WindowSystem::GetPadDPIScale()
{
	return 1.0;
}

bool WindowSystem::IsPadWindowOpen()
{
	return false;
}

bool WindowSystem::IsKeyDown(uint32 key)
{
	return g_window_info.get_keystate(key);
}

bool WindowSystem::IsKeyDown(PlatformKeyCodes platformKey)
{
	uint32 key = 0;
#if BOOST_OS_WINDOWS
	switch (platformKey)
	{
	case PlatformKeyCodes::LCONTROL:
		key = VK_LCONTROL;
		break;
	case PlatformKeyCodes::RCONTROL:
		key = VK_RCONTROL;
		break;
	case PlatformKeyCodes::TAB:
		key = VK_TAB;
		break;
	case PlatformKeyCodes::ESCAPE:
		key = VK_ESCAPE;
		break;
	default:
		return false;
	}
#else
	return false;
#endif
	return WindowSystem::IsKeyDown(key);
}

std::string WindowSystem::GetKeyCodeName(uint32 button)
{
#if BOOST_OS_WINDOWS
	LONG scan_code = MapVirtualKeyA((UINT)button, MAPVK_VK_TO_VSC_EX);
	if (HIBYTE(scan_code))
		scan_code |= 0x100;
	switch (button)
	{
	case VK_LEFT:
	case VK_UP:
	case VK_RIGHT:
	case VK_DOWN:
	case VK_PRIOR:
	case VK_NEXT:
	case VK_END:
	case VK_HOME:
	case VK_INSERT:
	case VK_DELETE:
	case VK_DIVIDE:
	case VK_NUMLOCK:
	{
		scan_code |= 0x100;
		break;
	}
	}
	scan_code <<= 16;
	char key_name[128];
	if (GetKeyNameTextA(scan_code, key_name, std::size(key_name)) != 0)
		return key_name;
	else
		return fmt::format("key_{}", button);
#else
	return fmt::format("key_{}", button);
#endif
}

bool WindowSystem::InputConfigWindowHasFocus()
{
	return g_inputConfigWindowHasFocus;
}

void WindowSystem::NotifyGameLoaded()
{
	// no-op for now - see UpdateWindowTitles's own comment.
}

void WindowSystem::NotifyGameExited()
{
	// no-op for now.
}

void WindowSystem::RefreshGameList()
{
	// no-op for now.
}

void WindowSystem::CaptureInput(const ControllerState& currentState, const ControllerState& lastState)
{
	// no-op for now - this only gates rebindable hotkey ACTIONS (save
	// state, toggle fullscreen, etc. - see HotkeySettings under
	// gui/wxgui/input), not the actual per-frame controller polling that
	// drives gameplay input (that's KeyboardController::raw_state() reading
	// g_window_info.iter_keystates() directly, already wired via IsKeyDown
	// above).
}

bool WindowSystem::IsFullScreen()
{
	return g_window_info.is_fullscreen;
}
