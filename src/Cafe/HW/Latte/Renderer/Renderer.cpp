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
	// Faro: the selection is STICKY, matching ReShade's generic_depth add-on.
	// In its on_begin_render_effects, when the heuristic finds no winner for a
	// frame the whole update block is skipped and the previously selected
	// shader resource is simply kept - it never falls back to "no depth". That
	// is deliberate: a frame where the search comes up empty is almost always
	// a hiccup in the search (an alternating depth buffer, a frame that
	// skipped the 3D pass, a buffer rebound in a different order), not the
	// scene depth actually ceasing to exist.
	//
	// This used to expire after a few frames, which is what made SSAO and SSR
	// visibly blink off and back on - both standing still and moving - because
	// they are hard-gated on HasCapturedDepth() with no smoothing of their
	// own. Holding the last known-good view for as long as it is genuinely
	// still tracked (NotifyTextureDeletion prunes it the moment the texture is
	// actually gone, so this can never dangle) removes that class of flicker
	// entirely instead of merely shortening it.
	LatteTextureView* depthView = LatteMRT::FindSceneDepthViewForSize(outputWidth, outputHeight, m_depthCaptureView);
	LatteTextureView* previousView = m_depthCaptureView;
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
		// Hold the previous selection for as long as the texture behind it is
		// still alive. No frame counter: the only thing that can legitimately
		// invalidate it is the texture actually going away, and
		// NotifyTextureDeletion handles that by pruning it from the tracked
		// set, which IsDepthViewStillTracked reports here.
		m_depthCaptureMissStreak++;
		valid = m_depthCaptureView && LatteMRT::IsDepthViewStillTracked(m_depthCaptureView);
		if (!valid)
		{
			m_depthCaptureView = nullptr;
			m_depthCaptureWidth = 0;
			m_depthCaptureHeight = 0;
		}
	}
	m_depthCaptureValid = valid;

	// Periodic full dump of the tracked depth buffers - see
	// LatteMRT::LogDepthCandidates for the question it exists to answer.
	// Every 600th capture rather than per frame, so it costs a handful of log
	// lines every few seconds instead of flooding the file.
	m_faroFrameCounter++;
	m_faroDepthSnapshotMin = std::min(m_faroDepthSnapshotMin, m_faroDepthSnapshotCount);
	m_faroDepthSnapshotMax = std::max(m_faroDepthSnapshotMax, m_faroDepthSnapshotCount);
	m_faroDepthSnapshotCount = 0;
	if ((++m_depthCaptureProbeCounter % 600) == 0)
	{
		cemuLog_log(LogType::Force, "Faro effect dispatch: GTAO ran {}/{} frames, SSR ran {}/{} frames",
			m_faroGtaoRanFrames, m_faroFrameCounter, m_faroSsrRanFrames, m_faroFrameCounter);
		cemuLog_log(LogType::Force, "Faro depth snapshots per frame: min {} max {} (a varying count means the capture point inside the frame moves)",
			m_faroDepthSnapshotMin == 0xFFFFFFFF ? 0u : m_faroDepthSnapshotMin, m_faroDepthSnapshotMax);
		m_faroDepthSnapshotMin = 0xFFFFFFFF; m_faroDepthSnapshotMax = 0;
		cemuLog_log(LogType::Force, "Faro AA chain: TAA fell back to single-pass on {}/{} frames, resolve had no usable history on {}/{} frames",
			m_faroTaaFallbackFrames, m_faroFrameCounter, m_faroTaaNoHistoryFrames, m_faroFrameCounter);
		m_faroFrameCounter = 0; m_faroGtaoRanFrames = 0; m_faroSsrRanFrames = 0;
		m_faroTaaFallbackFrames = 0; m_faroTaaNoHistoryFrames = 0;
		LatteMRT::LogDepthCandidates(outputWidth, outputHeight, m_depthCaptureView);
	}
	// A CHANGE OF BUFFER is logged as well, not just a change of validity.
	// Switching between two same-size depth buffers keeps `valid` true the
	// whole time, so a selection flip-flopping frame to frame used to produce
	// no log output at all while SSAO/SSR visibly cut in and out - which sent
	// an earlier round of this investigation after the wrong cause entirely.
	// See the hysteresis in FindSceneDepthViewForSize.
	if (valid && m_depthCaptureView != previousView && previousView != nullptr)
	{
		uint32 vertices = 0, drawcalls = 0;
		LatteMRT::GetSceneDepthViewStats(m_depthCaptureView, vertices, drawcalls);
		cemuLog_log(LogType::Force, "Faro depth capture: switched scene depth buffer to {}x{} ({} drawcalls / {} vertices last frame)",
			m_depthCaptureWidth, m_depthCaptureHeight, drawcalls, vertices);
	}
	// log transitions only, never per-frame
	if (valid != m_depthCaptureLoggedValid)
	{
		m_depthCaptureLoggedValid = valid;
		if (valid)
		{
			// Includes the workload the winning buffer was ranked on (see
			// LatteMRT::FindSceneDepthViewForSize) and the output size it was
			// matched against: when SSAO/SSR look wrong on someone else's PC
			// or in a specific game, this one line says whether the wrong
			// depth buffer was picked, and on what evidence.
			uint32 vertices = 0, drawcalls = 0;
			LatteMRT::GetSceneDepthViewStats(m_depthCaptureView, vertices, drawcalls);
			cemuLog_log(LogType::Force, "Faro depth capture: using depth {}x{} for post FX (output {}x{}, {} drawcalls / {} vertices last frame)",
				m_depthCaptureWidth, m_depthCaptureHeight, outputWidth, outputHeight, drawcalls, vertices);
		}
		else
		{
			cemuLog_log(LogType::Force, "Faro depth capture: no scene depth buffer matched output {}x{}, post FX depth passes will fall back", outputWidth, outputHeight);
		}
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
