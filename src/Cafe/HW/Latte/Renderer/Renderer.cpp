#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "WindowSystem.h"

#include "config/CemuConfig.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cemu/Logging/CemuLogging.h"

#include <imgui.h>
#include "imgui/imgui_extension.h"
#include <png.h>

#include "config/ActiveSettings.h"

std::unique_ptr<Renderer> g_renderer;

bool Renderer::GetVRAMInfo(int& usageInMB, int& totalInMB) const
{
	usageInMB = totalInMB = -1;
	
#if BOOST_OS_WINDOWS
	if (m_dxgi_wrapper)
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		if (m_dxgi_wrapper->QueryVideoMemoryInfo(info))
		{
			totalInMB = (info.Budget / 1000) / 1000;
			usageInMB = (info.CurrentUsage / 1000) / 1000;
			return true;
		}
	}
#endif

	return false;
}


void Renderer::Initialize()
{
	// imgui
	imguiFontAtlas = new ImFontAtlas();
	imguiFontAtlas->AddFontDefault();

	auto setupContext = [](ImGuiContext* context){
		ImGui::SetCurrentContext(context);
		ImGuiIO& io = ImGui::GetIO();
		io.WantSaveIniSettings = false;
		io.IniFilename = nullptr;
	};

	imguiTVContext = ImGui::CreateContext(imguiFontAtlas);
	imguiPadContext = ImGui::CreateContext(imguiFontAtlas);
	setupContext(imguiTVContext);
	setupContext(imguiPadContext);
}

void Renderer::Shutdown()
{
	// imgui
	ImGui::DestroyContext(imguiTVContext);
	ImGui::DestroyContext(imguiPadContext);
    ImGui_ClearFonts();
	delete imguiFontAtlas;
}

void Renderer::CaptureDepthForPostFX(sint32 outputWidth, sint32 outputHeight)
{
	// Faro Global Enhancements: hysteresis against single-frame "misses" -
	// FindSceneDepthViewForSize resolves by "most recently touched view of
	// this exact size", and ApplyCurrentState touches many depth views per
	// frame (shadow maps, minimaps, UI...). If any OTHER pass happens to
	// share the scene's own output size and gets bound AFTER the real scene
	// within the same frame, it briefly wins that race and this search comes
	// up empty (or matches the wrong view) for exactly one frame - even
	// though the real scene depth is still alive and about to win again next
	// frame. Without this, GTAO/GI/SSR (all gated on HasCapturedDepth(), with
	// zero smoothing of their own) all simultaneously blink off for that one
	// frame - confirmed via user testing as reflections/SSAO/GI "apareciendo
	// y desapareciendo" in fast, repeated bursts. Bridging a short streak of
	// misses with the last confirmed view (kept alive only while it's still
	// genuinely tracked - see IsDepthViewStillTracked) smooths this over
	// without touching the underlying MRU search at all.
	constexpr uint32 kDepthCaptureGraceFrames = 4;

	LatteTextureView* depthView = LatteMRT::FindSceneDepthViewForSize(outputWidth, outputHeight);
	bool freshMatch = false;
	uint32 w = 0, h = 0;
	if (depthView && depthView->baseTexture)
	{
		w = (uint32)depthView->baseTexture->width;
		h = (uint32)depthView->baseTexture->height;
		freshMatch = (w > 0 && h > 0);
	}

	bool valid;
	if (freshMatch)
	{
		m_depthCaptureView = depthView;
		m_depthCaptureWidth = w;
		m_depthCaptureHeight = h;
		m_depthCaptureMissStreak = 0;
		valid = true;
	}
	else
	{
		m_depthCaptureMissStreak++;
		bool stillAlive = m_depthCaptureView && LatteMRT::IsDepthViewStillTracked(m_depthCaptureView);
		valid = stillAlive && m_depthCaptureMissStreak <= kDepthCaptureGraceFrames;
		if (!valid)
		{
			m_depthCaptureView = nullptr;
			m_depthCaptureWidth = 0;
			m_depthCaptureHeight = 0;
		}
	}
	m_depthCaptureValid = valid;
	// log transitions only, never per-frame
	if (valid != m_depthCaptureLoggedValid)
	{
		m_depthCaptureLoggedValid = valid;
		if (valid)
			cemuLog_log(LogType::Force, "Faro depth capture: valid depth {}x{} for post FX", m_depthCaptureWidth, m_depthCaptureHeight);
		else
			cemuLog_log(LogType::Force, "Faro depth capture: no depth attachment at output time, post FX depth passes will fall back");
	}
}

bool Renderer::ImguiBegin(bool mainWindow)
{
	sint32 w = 0, h = 0;
	if (mainWindow)
		WindowSystem::GetWindowPhysSize(w, h);
	else if (WindowSystem::IsPadWindowOpen())
		WindowSystem::GetPadWindowPhysSize(w, h);
	else
		return false;
		
	if (w == 0 || h == 0)
		return false;

	// select the right context
	ImGui::SetCurrentContext(mainWindow ? imguiTVContext : imguiPadContext);

	const Vector2f window_size{(float)w, (float)h};
	auto& io = ImGui::GetIO();
	io.DisplaySize = {window_size.x, window_size.y}; // should be only updated in the renderer and only when needed

	ImGui_PrecacheFonts();
	return true;
}

uint8 Renderer::SRGBComponentToRGB(uint8 ci)
{
	const float cs = (float)ci / 255.0f;
	float cl;
	if (cs <= 0.04045)
		cl = cs / 12.92f;
	else
		cl = std::pow((cs + 0.055f) / 1.055f, 2.4f);
	cl = std::min(cl, 1.0f);
	return (uint8)(cl * 255.0f);
}

uint8 Renderer::RGBComponentToSRGB(uint8 cli)
{
	const float cl = (float)cli / 255.0f;
	float cs;
	if (cl < 0.0031308)
		cs = 12.92f * cl;
	else
		cs = 1.055f * std::pow(cl, 0.41666f) - 0.055f;
	cs = std::max(std::min(cs, 1.0f), 0.0f);
	return (uint8)(cs * 255.0f);
}

void Renderer::RequestScreenshot(ScreenshotSaveFunction onSaveScreenshot)
{
	m_screenshot_requested = true;
	m_on_save_screenshot = onSaveScreenshot;
}

void Renderer::CancelScreenshotRequest()
{
	m_screenshot_requested = false;
	m_on_save_screenshot = {};
}


void Renderer::SaveScreenshot(const std::vector<uint8>& rgb_data, int width, int height, bool mainWindow)
{
	std::thread(
		[=, screenshotRequested = std::exchange(m_screenshot_requested, false), onSaveScreenshot = std::exchange(m_on_save_screenshot, {})]() {
			if (screenshotRequested && onSaveScreenshot)
			{
				auto notificationMessage = onSaveScreenshot(rgb_data, width, height, mainWindow);
				if (notificationMessage.has_value())
					LatteOverlay_pushNotification(notificationMessage.value(), 2500);
			}
		})
		.detach();
}
