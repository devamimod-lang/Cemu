#include "Common/precompiled.h"
#include "gui/tauri/TauriGameWindow.h"
#include "interface/WindowSystem.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleId.h"
#include "util/helpers/helpers.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace
{
std::atomic_bool s_titleWindowRunning = false;
WNDPROC s_originalWndProc = nullptr;

// Faro: same logic as wxHelpers.cpp's fix_raw_keycode (wx-scoped, so
// duplicated here) - disambiguates L/R Ctrl/Alt/Shift using the scan-code
// extended-bit flags Win32 puts in a WM_KEYDOWN/UP message's lParam, the
// same way CemuApp::FilterEvent's wx event carries them. See
// WindowSystem::IsKeyDown(PlatformKeyCodes)/GetKeyCodeName for the other
// half of this contract (VK_LCONTROL/VK_RCONTROL etc.).
uint32 FixRawKeycode(uint32 keycode, uint32 rawFlags)
{
	const auto flags = (HIWORD(rawFlags) & 0xFFF);
	if (keycode == VK_SHIFT)
	{
		if (flags == 0x2A)
			return VK_LSHIFT;
		else if (flags == 0x36)
			return VK_RSHIFT;
	}
	else if (keycode == VK_CONTROL)
	{
		if (flags == 0x1d)
			return VK_LCONTROL;
		else if (flags == 0x11d)
			return VK_RCONTROL;
	}
	else if (keycode == VK_MENU)
	{
		if ((flags & 0xFF) == 0x38)
			return VK_LMENU;
	}
	return keycode;
}

LRESULT CALLBACK TauriGameWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		WindowSystem::GetWindowInfo().set_keystate(FixRawKeycode((uint32)wParam, (uint32)lParam), true);
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		WindowSystem::GetWindowInfo().set_keystate(FixRawKeycode((uint32)wParam, (uint32)lParam), false);
		break;
	case WM_KILLFOCUS:
	case WM_ACTIVATE:
		if (msg == WM_KILLFOCUS || wParam == WA_INACTIVE)
			WindowSystem::GetWindowInfo().set_keystatesup();
		break;
	default:
		break;
	}
	return CallWindowProcW(s_originalWndProc, hwnd, msg, wParam, lParam);
}

void TitleWindowThread(TitleId titleId)
{
	SetThreadName("TauriGameWindow");

	if (!glfwInit())
	{
		cemuLog_log(LogType::Force, "TauriGameWindow: glfwInit failed");
		s_titleWindowRunning = false;
		return;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Cemu", nullptr, nullptr);
	if (!window)
	{
		cemuLog_log(LogType::Force, "TauriGameWindow: glfwCreateWindow failed");
		glfwTerminate();
		s_titleWindowRunning = false;
		return;
	}

	HWND hwnd = glfwGetWin32Window(window);
	WindowSystem::WindowHandleInfo handleInfo;
	handleInfo.backend = WindowSystem::WindowHandleInfo::Backend::Windows;
	handleInfo.surface = reinterpret_cast<void*>(hwnd);
	handleInfo.display = nullptr;

	auto& windowInfo = WindowSystem::GetWindowInfo();
	windowInfo.window_main = handleInfo;
	windowInfo.canvas_main = handleInfo;
	windowInfo.width = 1280;
	windowInfo.height = 720;
	windowInfo.phys_width = 1280;
	windowInfo.phys_height = 720;
	windowInfo.dpi_scale = 1.0;
	windowInfo.app_active = true;

	// Subclass to intercept raw WM_KEYDOWN/UP (see FixRawKeycode's own
	// comment) - GLFW's own key callbacks remap to GLFW_KEY_* codes, not the
	// raw Win32 VK codes IsKeyDown/KeyboardController expect.
	s_originalWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)TauriGameWindowProc);

	glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int width, int height)
	{
		if (width <= 0 || height <= 0)
			return;
		auto& wi = WindowSystem::GetWindowInfo();
		wi.width = width;
		wi.height = height;
		wi.phys_width = width;
		wi.phys_height = height;
	});

	auto status = CafeSystem::PrepareForegroundTitle(titleId);
	if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
	{
		cemuLog_log(LogType::Force, "TauriGameWindow: PrepareForegroundTitle failed for titleId {:016x}", titleId);
		glfwDestroyWindow(window);
		glfwTerminate();
		s_titleWindowRunning = false;
		return;
	}
	CafeSystem::LaunchForegroundTitle();

	// Vulkan presents directly via vkQueuePresentKHR (done internally by
	// VulkanRenderer/the emulation thread) - this loop only needs to keep
	// pumping window messages, no glfwSwapBuffers (that's an OpenGL-context
	// concept and this window has none, GLFW_CLIENT_API=GLFW_NO_API).
	while (!glfwWindowShouldClose(window) && CafeSystem::IsTitleRunning())
	{
		glfwWaitEventsTimeout(0.1);
	}

	glfwDestroyWindow(window);
	glfwTerminate();
	s_titleWindowRunning = false;
}
} // namespace

bool TauriGameWindow_LaunchTitle(uint64_t titleId)
{
	bool expected = false;
	if (!s_titleWindowRunning.compare_exchange_strong(expected, true))
	{
		cemuLog_log(LogType::Force, "TauriGameWindow_LaunchTitle: a title window is already running");
		return false;
	}
	std::thread(TitleWindowThread, (TitleId)titleId).detach();
	return true;
}
