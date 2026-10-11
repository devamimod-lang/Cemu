#include "Cafe/HW/Latte/ISA/RegDefines.h"

#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "HW/Latte/Renderer/RendererCore.h"
#include "config/ActiveSettings.h"
#include "WindowSystem.h"
#include "Cafe/OS/libs/erreula/erreula.h"
#include "input/InputManager.h"
#include "Cafe/OS/libs/swkbd/swkbd.h"

uint32 prevScissorX = 0;
uint32 prevScissorY = 0;
uint32 prevScissorWidth = 0;
uint32 prevScissorHeight = 0;

bool hasValidFramebufferAttached = false;

struct LatteMRTQuad
{
	sint32 width;
	sint32 height;
};

struct
{
	LatteMRTQuad currentRenderSize;
	LatteMRTQuad currentEffectiveSize;
	struct
	{
		sint32 width;
		sint32 height;
	}currentGuestViewport;
	bool renderTargetIsResized;
	// tracking
	sint32 rtUpdateListCount;
	LatteTextureView* rtUpdateList[64];
	sint32 rtUpdateListSlice[64];
	sint32 rtUpdateListMip[64];
}sLatteRenderTargetState;

struct
{
	struct
	{
		LatteTextureView* view{};
	}colorBuffer[8];
	struct
	{
		LatteTextureView* view{};
		bool hasStencil{false};
	}depthBuffer;
}sLatteCurrentRendertargets{};

LatteCachedFBO::LatteCachedFBO(uint64 key) : key(key)
{
	for (sint32 i = 0; i < 8; i++)
	{
		LatteTextureView* colorTexView = sLatteCurrentRendertargets.colorBuffer[i].view;
		colorBuffer[i].texture = colorTexView;
		if (colorTexView)
		{
			vectorAppendUnique(colorTexView->list_associatedFbo, this);
			m_referencedTextures.emplace_back(colorTexView->baseTexture);
		}
	}

	if (sLatteCurrentRendertargets.depthBuffer.view)
	{
		LatteTextureView* depthTexView = sLatteCurrentRendertargets.depthBuffer.view;
		depthBuffer.texture = depthTexView;
		depthBuffer.hasStencil = sLatteCurrentRendertargets.depthBuffer.hasStencil;
		if (depthTexView)
		{
			vectorAppendUnique(depthTexView->list_associatedFbo, this);
			m_referencedTextures.emplace_back(depthTexView->baseTexture);
		}
	}

	calculateEffectiveRenderAreaSize();
}

namespace
{
	// Faro Global Enhancements: scene-depth detection, modeled on ReShade's
	// own built-in "generic depth" add-on (examples/09-depth/
	// generic_depth_addon.cpp in crosire/reshade) - the reference solution to
	// exactly this problem: out of every depth buffer an application binds,
	// work out which one holds the main 3D scene, with no cooperation from
	// the application itself.
	//
	// The previous heuristic here was "most recently bound depth view whose
	// size exactly matches the output size". That is weaker than ReShade's in
	// two ways that both showed up in practice:
	//
	//  1. Recency is not evidence. A shadow map, minimap, cutscene letterbox
	//     or UI depth buffer that happens to share the scene's resolution and
	//     gets bound AFTER the scene within the same frame simply wins. Only
	//     a tie-breaker based on how much geometry was actually drawn into
	//     each buffer distinguishes "the main scene" from "something else the
	//     same size" - which is why ReShade ranks candidates by per-frame
	//     vertex/draw-call counts rather than by binding order.
	//  2. Exact-size-only has no fallback. When the 3D scene is rendered at a
	//     different resolution than the buffer being presented (any graphic
	//     pack resolution mod, and plenty of games natively), nothing matches
	//     at all and every depth-driven effect switches off outright instead
	//     of degrading. ReShade's check_aspect_ratio accepts a buffer whose
	//     aspect ratio matches and whose scale is in a sane band.
	//
	// Deliberately NOT reset per frame: entries persist and are pruned only
	// via NotifyDepthTextureDeleted (the texture is actually gone), so a
	// stale view can only linger as long as Cemu's own texture cache keeps it
	// alive anyway. Only the per-frame STATS roll over (see
	// AdvanceDepthStatsFrame).
	struct FaroDepthStats
	{
		uint32 vertices = 0;
		uint32 drawcalls = 0;
	};

	struct FaroDepthCandidate
	{
		LatteTextureView* view = nullptr;
		// Geometry recorded into this buffer during the frame currently being
		// built, and during the last frame that completed. The heuristic
		// reads lastFrame: at present time the current frame's own stats are
		// complete, but reading the completed frame keeps the choice stable
		// across the swap boundary the same way ReShade reads
		// 'last_frame_stats'.
		FaroDepthStats current;
		// Work recorded since the last depth-buffer UNBIND, as opposed to
		// `current` which accumulates across the whole frame. The snapshot
		// rule needs the per-segment figure: see the unbind branch in
		// NotifyDepthAttachmentUsed.
		FaroDepthStats segment;
		FaroDepthStats lastFrame;
		uint64 lastUsedFrame = 0;
		uint64 firstSeenFrame = 0;
	};

	std::vector<FaroDepthCandidate> s_sceneDepthCandidates;
	// The depth view draw calls are currently being attributed to - set by
	// NotifyDepthAttachmentUsed (which ApplyCurrentState already calls on
	// every framebuffer bind), consumed by NotifyDepthDrawcall.
	LatteTextureView* s_currentDepthView = nullptr;
	uint64 s_depthStatsFrameIndex = 0;
	// Highest per-frame geometry count already snapshotted this frame - see
	// NotifyDepthAttachmentUsed's unbind branch for ReShade's best-copy rule.
	uint32 s_depthSnapshotBestVertices = 0;

	void NotifyDepthTextureDeleted(LatteTexture* texture)
	{
		if (s_currentDepthView && s_currentDepthView->baseTexture == texture)
			s_currentDepthView = nullptr;
		s_sceneDepthCandidates.erase(
			std::remove_if(s_sceneDepthCandidates.begin(), s_sceneDepthCandidates.end(),
				[texture](const FaroDepthCandidate& candidate) { return candidate.view->baseTexture == texture; }),
			s_sceneDepthCandidates.end());
	}

	FaroDepthCandidate* FindDepthCandidate(LatteTextureView* view)
	{
		auto it = std::find_if(s_sceneDepthCandidates.begin(), s_sceneDepthCandidates.end(),
			[view](const FaroDepthCandidate& candidate) { return candidate.view == view; });
		return it == s_sceneDepthCandidates.end() ? nullptr : &*it;
	}

	// Port of ReShade's own check_aspect_ratio (generic_depth_addon.cpp), with
	// its 'similar_aspect_ratio' and 'multiples_of_resolution' modes folded
	// into one test. Both are wanted here rather than either alone: Cemu's
	// graphic packs routinely render the 3D scene at an exact 2x/3x/4x
	// multiple of the presented buffer, which lands outside ReShade's own
	// 0.5x..1.85x "similar" band, while non-integer internal resolutions do
	// fall inside it.
	bool FaroDepthAspectMatches(sint32 candidateWidth, sint32 candidateHeight, sint32 outputWidth, sint32 outputHeight)
	{
		if (candidateWidth <= 0 || candidateHeight <= 0 || outputWidth <= 0 || outputHeight <= 0)
			return false;

		const float cw = (float)candidateWidth, ch = (float)candidateHeight;
		const float ow = (float)outputWidth, oh = (float)outputHeight;

		// Reject anything that is not the same shape as the output - this is
		// what rules out square shadow maps (1024x1024 against a 16:9 scene).
		if (std::abs((ow / oh) - (cw / ch)) > 0.1f)
			return false;

		float wRatio = ow / cw;
		float hRatio = oh / ch;
		if (wRatio <= 1.85f && wRatio >= 0.5f && hRatio <= 1.85f && hRatio >= 0.5f)
			return true;

		// Near-integer multiple in either direction (scene larger or smaller
		// than what gets presented).
		float wIntegral, hIntegral;
		if (std::modf(wRatio, &wIntegral) <= 0.02f && std::modf(hRatio, &hIntegral) <= 0.02f)
			return true;
		float wInvIntegral, hInvIntegral;
		return std::modf(cw / ow, &wInvIntegral) <= 0.02f && std::modf(ch / oh, &hInvIntegral) <= 0.02f;
	}
}

void LatteMRT::NotifyDepthAttachmentUsed(LatteTextureView* view)
{
	LatteTextureView* previous = s_currentDepthView;
	s_currentDepthView = view;
	if (!view)
	{
		// Unbinding a depth buffer. This is the point where a depth-rendering
		// sequence has just finished, so it is the right moment to take a
		// private copy before the game starts doing something else with that
		// buffer - see Renderer::CaptureSceneDepthSnapshot.
		//
		// Which unbind to copy on uses ReShade's own rule (its
		// `best_copy_stats` check in on_clear_depth_impl): copy whenever this
		// sequence drew at least as much geometry as the best one copied so
		// far this frame. A frame typically unbinds several depth buffers, and
		// this converges on the heaviest one - the main scene - without
		// needing to know in advance which unbind is the last.
		// ONLY the buffer the selection heuristic already picked is worth
		// copying. Without this check the best-copy rule above copies whatever
		// the frame's first depth-rendering sequence happened to be - observed
		// in practice snapshotting a 64x64 buffer (a shadow or cubemap face)
		// and handing that to GTAO as if it were a 720p scene. It also meant
		// the snapshot image got recreated for every differently-sized buffer
		// the game unbound, which is what made the recreation path run
		// mid-frame at all.
		if (previous && g_renderer && previous == g_renderer->GetCapturedDepthView())
		{
			if (FaroDepthCandidate* candidate = FindDepthCandidate(previous))
			{
				// Compared on the SEGMENT - the work drawn since the previous
				// unbind - and not on the frame-cumulative `current`. That
				// distinction is the whole point, and getting it wrong is what
				// made the AO flash.
				//
				// `current` only ever grows within a frame, so comparing it
				// against the best-so-far meant the LAST unbind of the frame
				// always won. A game that renders the scene, clears depth, and
				// then draws a small second pass therefore ended up
				// snapshotting the freshly CLEARED buffer, because its
				// cumulative count was still the highest. The AO then had
				// essentially empty depth for that frame - which reads as a
				// bright flash wherever the surrounding depth is dark, i.e.
				// precisely when the camera is low and most of the screen is
				// near geometry.
				//
				// ReShade avoids this by resetting stats.current at every
				// clear (on_clear_depth_impl) so each segment is judged on its
				// own workload. Same rule here.
				if (candidate->segment.vertices >= s_depthSnapshotBestVertices && candidate->segment.vertices > 3)
				{
					s_depthSnapshotBestVertices = candidate->segment.vertices;
					g_renderer->CaptureSceneDepthSnapshot(previous);
					g_renderer->NotifyDepthSnapshotTaken();
				}
				// NOT reset here. The segment is reset by a CLEAR
				// (NotifyDepthBufferCleared), never by an unbind, and that
				// distinction matters: a game routinely binds and unbinds the
				// same depth buffer several times while still accumulating
				// into the SAME contents - terrain, then structures, then
				// props - with no clear in between.
				//
				// Resetting per unbind split one coherent scene into chunks
				// and judged each chunk on its own. Only the heaviest chunk
				// then cleared the best-so-far bar, so the snapshot was taken
				// partway through the scene and the geometry drawn in the
				// later chunks was simply missing from it. Those pixels read
				// as cleared depth, got no occlusion, and showed up as
				// patches of floor and structures that looked see-through.
				//
				// Counting from the last clear is what ReShade does
				// (stats.current is reset in on_clear_depth_impl) and it is
				// the only boundary that actually means "these are different
				// depth contents".
			}
		}
		return;
	}

	FaroDepthCandidate* candidate = FindDepthCandidate(view);
	if (!candidate)
	{
		FaroDepthCandidate fresh;
		fresh.view = view;
		fresh.firstSeenFrame = s_depthStatsFrameIndex;
		fresh.lastUsedFrame = s_depthStatsFrameIndex;
		s_sceneDepthCandidates.push_back(fresh);
		return;
	}
	candidate->lastUsedFrame = s_depthStatsFrameIndex;
}

// A depth buffer was cleared: whatever was drawn into it before this point is
// gone, so the per-segment workload used by the snapshot rule starts over.
//
// This is the boundary ReShade uses (it resets stats.current inside
// on_clear_depth_impl) and it is the only one that genuinely marks "different
// depth contents". An unbind does not - see the unbind branch in
// NotifyDepthAttachmentUsed for what resetting there instead cost.
//
// Note the frame's best-so-far bar is deliberately NOT reset here: the point
// of that bar is to end the frame holding the HEAVIEST segment, so a light
// pass drawn after a clear must not displace the real scene already captured.
void LatteMRT::NotifyDepthBufferCleared(LatteTexture* texture)
{
	if (!texture)
		return;
	for (auto& candidate : s_sceneDepthCandidates)
	{
		if (candidate.view->baseTexture == texture)
			candidate.segment = {};
	}
}

void LatteMRT::NotifyDepthDrawcall(uint32 vertexCount, uint32 instanceCount)
{
	if (!s_currentDepthView)
		return;
	FaroDepthCandidate* candidate = FindDepthCandidate(s_currentDepthView);
	if (!candidate)
		return;

	// Saturating add: a runaway count would only corrupt the ranking, and
	// these are compared, never used as real geometry totals.
	const uint64 vertices = (uint64)vertexCount * (uint64)std::max(instanceCount, 1u);
	candidate->current.vertices = (uint32)std::min<uint64>((uint64)candidate->current.vertices + vertices, std::numeric_limits<uint32>::max());
	candidate->segment.vertices = (uint32)std::min<uint64>((uint64)candidate->segment.vertices + vertices, std::numeric_limits<uint32>::max());
	if (candidate->current.drawcalls != std::numeric_limits<uint32>::max())
		candidate->current.drawcalls++;
	if (candidate->segment.drawcalls != std::numeric_limits<uint32>::max())
		candidate->segment.drawcalls++;
	candidate->lastUsedFrame = s_depthStatsFrameIndex;
}

void LatteMRT::AdvanceDepthStatsFrame()
{
	for (auto& candidate : s_sceneDepthCandidates)
	{
		candidate.lastFrame = candidate.current;
		candidate.current = {};
		candidate.segment = {};
	}
	s_depthStatsFrameIndex++;
	// A fresh frame has to earn its own snapshot; without this reset the first
	// heavy frame would keep every later frame from ever copying again.
	s_depthSnapshotBestVertices = 0;
	if (g_renderer)
		g_renderer->OnFaroFrameBoundary();
	// Deliberately not cleared: whatever was bound last stays the
	// attribution target until the next bind, matching how Cemu keeps the
	// render target state across the swap.
}

LatteTextureView* LatteMRT::FindSceneDepthViewForSize(sint32 width, sint32 height, LatteTextureView* currentlySelected)
{
	// Selection, in ReShade generic_depth's own order: filter out buffers
	// that cannot be the scene, then rank what is left by how much geometry
	// went into it.
	const FaroDepthCandidate* best = nullptr;
	bool bestIsExactSize = false;
	uint32 bestVertices = 0;
	uint32 bestDrawcalls = 0;

	for (const auto& candidate : s_sceneDepthCandidates)
	{
		// Judge a candidate on the best of "this frame so far" and "the last
		// completed frame", rather than on the completed frame alone.
		//
		// This matters because games commonly PING-PONG between two depth
		// buffers on alternating frames. Looking only at the completed frame,
		// buffer A reports zero work on every frame where B was the one being
		// drawn into, and vice versa - so on any given frame BOTH look unused
		// and both get rejected, the search comes up empty, and every
		// depth-driven effect blinks off. (ReShade does not hit this because
		// it never drops its previous selection when a frame finds no winner;
		// see CaptureDepthForPostFX, which now behaves the same way.)
		//
		// Using `current` as well is safe here: this runs at backbuffer-copy
		// time, after the frame's 3D geometry has already been submitted, so
		// `current` is a complete and in fact fresher picture than
		// `lastFrame`.
		const uint32 candVertices = std::max(candidate.current.vertices, candidate.lastFrame.vertices);
		const uint32 candDrawcalls = std::max(candidate.current.drawcalls, candidate.lastFrame.drawcalls);

		// Minimum workload to be considered a 3D scene at all. ReShade's own
		// threshold here is just "more than 3 vertices", which turns out to be
		// far too permissive in this codebase: a log of a real session showed
		// the selection alternating between two 1280x720 buffers reporting 30
		// and 12 vertices (10 and 4 draw calls) while the actual scene buffer,
		// when present, reported 4082 draw calls and 3.86M vertices. Both junk
		// buffers cleared ReShade's bar, and with workloads that small and
		// that noisy the hysteresis below gets crossed by nothing at all -
		// which is what kept SSAO/SSR cutting in and out.
		//
		// Three orders of magnitude separate the real scene from that noise,
		// so the exact values here are not delicate. Requiring BOTH signals
		// matters though: a long run of 2-vertex draws is still not a scene.
		//
		// When nothing clears the bar the result is no match, and that is the
		// correct answer rather than a failure - on a title screen or a 2D
		// menu there is no 3D scene for depth-driven effects to shade.
		constexpr uint32 kMinSceneVertices = 300;
		constexpr uint32 kMinSceneDrawcalls = 16;
		if (candDrawcalls < kMinSceneDrawcalls || candVertices < kMinSceneVertices)
			continue;
		// Skip buffers that only just appeared: their first frame's stats are
		// partial, so they would be ranked against everyone else unfairly.
		if (s_depthStatsFrameIndex <= candidate.firstSeenFrame + 1)
			continue;
		// Skip buffers not touched recently. Two frames of slack rather than
		// one, for the same alternating-buffer reason as above.
		if (candidate.lastUsedFrame + 2 < s_depthStatsFrameIndex)
			continue;

		sint32 viewWidth, viewHeight;
		candidate.view->baseTexture->GetEffectiveSize(viewWidth, viewHeight, candidate.view->firstMip);
		if (!FaroDepthAspectMatches(viewWidth, viewHeight, width, height))
			continue;

		const bool isExactSize = (viewWidth == width && viewHeight == height);

		// An exact resolution match always outranks a merely aspect-
		// compatible one. This is a Cemu-specific addition on top of
		// ReShade's ranking, and it is what keeps the common case behaving
		// exactly as it did before this heuristic existed: a Wii U game's
		// scene depth normally IS the same size as the buffer being
		// presented, so the loosened aspect test only ever comes into play
		// when nothing matches exactly.
		if (!best || (isExactSize && !bestIsExactSize))
		{
			best = &candidate;
			bestIsExactSize = isExactSize;
			bestVertices = candVertices;
			bestDrawcalls = candDrawcalls;
			continue;
		}
		if (bestIsExactSize && !isExactSize)
			continue;

		// Same tier - rank by workload. Cemu issues no indirect draws, so
		// ReShade's "prefer_vertices" branch is the only one that applies;
		// draw calls break ties.
		//
		// Hysteresis: a candidate has to beat the buffer ALREADY SELECTED by a
		// wide margin to take over, and conversely the selected one keeps its
		// place even when it is momentarily behind. Without this, a game that
		// owns two depth buffers OF THE SAME SIZE - which is the normal case,
		// since the exact-resolution tier above cannot separate them - flips
		// between them on alternating frames as their per-frame workloads
		// cross over. The selection stayed "valid" the whole time, so nothing
		// logged, while SSAO/SSR silently alternated between the real scene
		// depth and a near-empty buffer: seen in practice as SSAO cutting out
		// in bursts, which reads as the image flashing brighter. (A real
		// example from a log: the scene buffer reporting 4082 drawcalls /
		// 3.86M vertices and a junk buffer of the same size reporting 10
		// drawcalls / 30 vertices, both 1280x720.)
		const bool bestIsIncumbent = (currentlySelected && best->view == currentlySelected);
		const bool candIsIncumbent = (currentlySelected && candidate.view == currentlySelected);
		uint32 beatVertices = bestVertices;
		uint32 beatDrawcalls = bestDrawcalls;
		if (bestIsIncumbent && !candIsIncumbent)
		{
			// Challenger must be clearly better - 2x the geometry.
			beatVertices = (bestVertices > (std::numeric_limits<uint32>::max() / 2)) ? std::numeric_limits<uint32>::max() : bestVertices * 2;
			beatDrawcalls = (bestDrawcalls > (std::numeric_limits<uint32>::max() / 2)) ? std::numeric_limits<uint32>::max() : bestDrawcalls * 2;
		}
		else if (candIsIncumbent && !bestIsIncumbent)
		{
			// Incumbent only has to be within half of the current leader.
			beatVertices = bestVertices / 2;
			beatDrawcalls = bestDrawcalls / 2;
		}

		if (candVertices > beatVertices ||
			(candVertices == beatVertices && candDrawcalls > beatDrawcalls))
		{
			best = &candidate;
			bestIsExactSize = isExactSize;
			bestVertices = candVertices;
			bestDrawcalls = candDrawcalls;
		}
	}

	return best ? best->view : nullptr;
}

// Diagnostic for the long-standing "SSAO/SSR flicker" that predates every
// selection heuristic in this file - the original MRU code already carried a
// 4-frame grace window as a band-aid for it, and three successive rounds of
// improving the SELECTION did not move it. The question this answers is
// whether the game keeps several scene-sized depth buffers and alternates
// which one holds the current frame's geometry: in that case no amount of
// picking the "right" buffer helps, because the one picked only holds valid
// scene depth on some frames and whatever remains on the others, and that is
// a content problem rather than a selection problem.
//
// Dumps the full tracked set rather than just the winner, so the alternation
// is visible if it is there.
void LatteMRT::LogDepthCandidates(sint32 outputWidth, sint32 outputHeight, LatteTextureView* selected)
{
	cemuLog_log(LogType::Force, "Faro depth probe: output {}x{}, {} tracked depth buffers", outputWidth, outputHeight, (uint32)s_sceneDepthCandidates.size());
	for (const auto& candidate : s_sceneDepthCandidates)
	{
		sint32 viewWidth = 0, viewHeight = 0;
		candidate.view->baseTexture->GetEffectiveSize(viewWidth, viewHeight, candidate.view->firstMip);
		cemuLog_log(LogType::Force, "  {} {}x{}  this frame: {} draws / {} verts | last frame: {} draws / {} verts | lastUsed {} frames ago{}",
			candidate.view == selected ? "->" : "  ",
			viewWidth, viewHeight,
			candidate.current.drawcalls, candidate.current.vertices,
			candidate.lastFrame.drawcalls, candidate.lastFrame.vertices,
			(uint32)(s_depthStatsFrameIndex - candidate.lastUsedFrame),
			FaroDepthAspectMatches(viewWidth, viewHeight, outputWidth, outputHeight) ? "" : " [aspect reject]");
	}
}

void LatteMRT::GetSceneDepthViewStats(LatteTextureView* view, uint32& vertices, uint32& drawcalls)
{
	vertices = 0;
	drawcalls = 0;
	if (const FaroDepthCandidate* candidate = FindDepthCandidate(view))
	{
		vertices = candidate->lastFrame.vertices;
		drawcalls = candidate->lastFrame.drawcalls;
	}
}

bool LatteMRT::IsDepthViewStillTracked(LatteTextureView* view)
{
	return FindDepthCandidate(view) != nullptr;
}

void LatteMRT::NotifyTextureDeletion(LatteTexture* texture)
{
	for (sint32 i = 0; i < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS; i++)
	{
		if (sLatteCurrentRendertargets.colorBuffer[i].view && sLatteCurrentRendertargets.colorBuffer[i].view->baseTexture == texture)
		{
			sLatteCurrentRendertargets.colorBuffer[i].view = nullptr;
		}
	}
	NotifyDepthTextureDeleted(texture);
}

LatteCachedFBO* LatteMRT::CreateCachedFBO(uint64 key)
{
	return g_renderer->rendertarget_createCachedFBO(key);
}

void LatteMRT::DeleteCachedFBO(LatteCachedFBO* cfbo)
{
	// color textures
	for (sint32 i = 0; i < 8; i++)
	{
		if (cfbo->colorBuffer[i].texture == NULL)
			continue;
		cfbo->colorBuffer[i].texture->list_fboLookup.erase(std::remove(cfbo->colorBuffer[i].texture->list_fboLookup.begin(), cfbo->colorBuffer[i].texture->list_fboLookup.end(), cfbo), cfbo->colorBuffer[i].texture->list_fboLookup.end());
		cfbo->colorBuffer[i].texture->list_associatedFbo.erase(std::remove(cfbo->colorBuffer[i].texture->list_associatedFbo.begin(), cfbo->colorBuffer[i].texture->list_associatedFbo.end(), cfbo), cfbo->colorBuffer[i].texture->list_associatedFbo.end());
	}
	// depth texture
	if (cfbo->depthBuffer.texture)
	{
		cfbo->depthBuffer.texture->list_fboLookup.erase(std::remove(cfbo->depthBuffer.texture->list_fboLookup.begin(), cfbo->depthBuffer.texture->list_fboLookup.end(), cfbo), cfbo->depthBuffer.texture->list_fboLookup.end());
		cfbo->depthBuffer.texture->list_associatedFbo.erase(std::remove(cfbo->depthBuffer.texture->list_associatedFbo.begin(), cfbo->depthBuffer.texture->list_associatedFbo.end(), cfbo), cfbo->depthBuffer.texture->list_associatedFbo.end());
	}
	g_renderer->rendertarget_deleteCachedFBO(cfbo);
	delete cfbo;
}

LatteCachedFBO* g_emptyFBO = nullptr;

void LatteMRT::SetColorAttachment(uint32 index, LatteTextureView* view)
{
	sLatteCurrentRendertargets.colorBuffer[index].view = view;
}

void LatteMRT::SetDepthAndStencilAttachment(LatteTextureView* view, bool hasStencil)
{
	sLatteCurrentRendertargets.depthBuffer.view = view;
	sLatteCurrentRendertargets.depthBuffer.hasStencil = hasStencil;
}

LatteTextureView* LatteMRT::GetColorAttachment(uint32 index)
{
	cemu_assert_debug(index < 8);
	return sLatteCurrentRendertargets.colorBuffer[index].view;
}

LatteTextureView* LatteMRT::GetDepthAttachment()
{
	return sLatteCurrentRendertargets.depthBuffer.view;
}

void LatteMRT::ApplyCurrentState()
{
	uint64 key = 0;
	LatteTextureView* fboLookupView = NULL;
	for (sint32 i = 0; i < 8; i++)
	{
		LatteTextureView* colorView = sLatteCurrentRendertargets.colorBuffer[i].view;
		if (colorView)
		{
			key += ((uint64)colorView);
			key = std::rotl<uint64>(key, 5);
			fboLookupView = colorView;
		}
		key = std::rotl<uint64>(key, 7);
	}
	if (sLatteCurrentRendertargets.depthBuffer.view)
	{
		key += ((uint64)sLatteCurrentRendertargets.depthBuffer.view);
		key = std::rotl<uint64>(key, 5);
		key += (sLatteCurrentRendertargets.depthBuffer.hasStencil);
		if (fboLookupView == NULL)
		{
			fboLookupView = sLatteCurrentRendertargets.depthBuffer.view;
		}
		LatteMRT::NotifyDepthAttachmentUsed(sLatteCurrentRendertargets.depthBuffer.view);
	}
	else
	{
		// No depth attachment on this framebuffer - clear the attribution
		// target, otherwise NotifyDepthDrawcall keeps crediting draws to
		// whichever depth buffer happened to be bound LAST. Games bind plenty
		// of depth-less framebuffers (HUD, UI, post-process fullscreen
		// quads), and all of those draws were landing on an unrelated depth
		// buffer, which is exactly the kind of noise that makes the
		// workload ranking in FindSceneDepthViewForSize pick the wrong
		// buffer.
		//
		// This unbind is also the moment to snapshot the scene depth - see
		// LatteMRT::NotifyDepthAttachmentUsed, which decides whether this
		// particular unbind is worth copying.
		LatteMRT::NotifyDepthAttachmentUsed(nullptr);
	}
	// use fboLookupTexture to find cached FBO
	if (fboLookupView == nullptr)
	{
		if (!g_emptyFBO)
			g_emptyFBO = CreateCachedFBO(key);
		g_renderer->rendertarget_bindFramebufferObject(g_emptyFBO);
		return;
	}
	// look for FBO
	for (auto& fbo : fboLookupView->list_fboLookup)
	{
		if (fbo->key == key)
		{
			// found matching FBO
			g_renderer->rendertarget_bindFramebufferObject(fbo);
			return;
		}
	}
	// create new cached FBO
	LatteCachedFBO* cfbo = CreateCachedFBO(key);
	g_renderer->rendertarget_bindFramebufferObject(cfbo);
	fboLookupView->list_fboLookup.push_back(cfbo);
	// some extra checks to verify that looked up fbo matches active buffers
	cemu_assert_debug(cfbo->colorBuffer[0].texture == sLatteCurrentRendertargets.colorBuffer[0].view);
	cemu_assert_debug(cfbo->colorBuffer[1].texture == sLatteCurrentRendertargets.colorBuffer[1].view);
	cemu_assert_debug(cfbo->colorBuffer[2].texture == sLatteCurrentRendertargets.colorBuffer[2].view);
	cemu_assert_debug(cfbo->depthBuffer.texture == sLatteCurrentRendertargets.depthBuffer.view);
}

void LatteMRT::BindColorBufferOnly(LatteTextureView* view)
{
	cemu_assert_debug(!view->baseTexture->isDepth);
	SetColorAttachment(0, view);
	for (sint32 i = 1; i < 8; i++)
		SetColorAttachment(i, nullptr);
	SetDepthAndStencilAttachment(nullptr, false);
	ApplyCurrentState();
}

void LatteMRT::BindDepthBufferOnly(LatteTextureView* view)
{
	cemu_assert_debug(view->baseTexture->isDepth);
	for (sint32 i = 0; i < 8; i++)
		SetColorAttachment(i, nullptr);
	SetDepthAndStencilAttachment(view, view->baseTexture->hasStencil);
	ApplyCurrentState();
}

LatteTextureView* LatteMRT_CreateDepthBuffer(MPTR depthBufferPhysMem, uint32 width, uint32 height, uint32 pitch, Latte::E_HWTILEMODE tileMode, Latte::E_GX2SURFFMT format, uint32 swizzle, sint32 viewSlice)
{
	LatteTextureView* textureView = LatteTexture_CreateMapping(depthBufferPhysMem, MPTR_NULL, width, height, viewSlice+1, pitch, tileMode, swizzle, 0, 1, viewSlice, 1, format, viewSlice > 0 ? Latte::E_DIM::DIM_2D_ARRAY : Latte::E_DIM::DIM_2D, Latte::E_DIM::DIM_2D, true);
	LatteMRT::SetDepthAndStencilAttachment(textureView, textureView->baseTexture->hasStencil);
	return textureView;
}

sint32 _depthBufferSizeWarningCount = 0;

LatteTextureView* LatteMRT::GetColorAttachmentTexture(uint32 index, bool createNew, bool checkForTextureChanges)
{
	uint32* colorBufferRegBase = LatteGPUState.contextRegister+(mmCB_COLOR0_BASE + index);
	uint32 regColorBufferBase = colorBufferRegBase[mmCB_COLOR0_BASE - mmCB_COLOR0_BASE] & 0xFFFFFF00; // the low 8 bits are ignored? How to Survive seems to rely on this
	uint32 regColorSize = colorBufferRegBase[mmCB_COLOR0_SIZE - mmCB_COLOR0_BASE];
	uint32 regColorInfo = colorBufferRegBase[mmCB_COLOR0_INFO - mmCB_COLOR0_BASE];
	uint32 regColorView = colorBufferRegBase[mmCB_COLOR0_VIEW - mmCB_COLOR0_BASE];
	// decode color buffer reg info
	Latte::E_HWTILEMODE colorBufferTileMode = (Latte::E_HWTILEMODE)((regColorInfo >> 8) & 0xF);
	uint32 numberType = (regColorInfo >> 12) & 7;
	Latte::E_GX2SURFFMT colorBufferFormat = GetColorBufferFormat(index, LatteGPUState.contextNew);

	MPTR colorBufferPhysMem = regColorBufferBase;

	uint32 colorBufferSwizzle = 0;
	if ( Latte::TM_IsMacroTiled(colorBufferTileMode) )
	{
		colorBufferSwizzle = colorBufferPhysMem & 0x700;
		colorBufferPhysMem = colorBufferPhysMem & ~(7 << 8);
	}

	// get view slice and view slice num
	uint32 viewFirstSlice = (regColorView & 0x7FF);
	uint32 viewNumSlices = ((regColorView >> 13) & 0x7FF) - viewFirstSlice + 1;
	if (viewNumSlices != 1)
	{
		debug_printf("viewNumSlices is not 1! (%d)\n", viewNumSlices);
	}
	uint32 colorBufferPitch = (((regColorSize >> 0) & 0x3FF) + 1);
	colorBufferPitch <<= 3;
	uint32 pitchHeight = (((regColorSize >> 10) & 0xFFFFF) + 1);
	pitchHeight <<= 6;
	uint32 colorBufferHeight = pitchHeight / colorBufferPitch;
	uint32 colorBufferWidth = colorBufferPitch;

	// colorbuffer width/height has to be padded to 8/32 alignment but the actual resolution might be smaller
	// use the scissor box as a clue to figure out the original resolution if possible
	if(LatteGPUState.allowFramebufferSizeOptimization)
	{
		uint32 scissorBoxWidth = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_BR.get_BR_X();
		uint32 scissorBoxHeight = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_BR.get_BR_Y();
		if (((scissorBoxWidth + 7) & ~7) == colorBufferWidth)
			colorBufferWidth = scissorBoxWidth;
		if (((colorBufferHeight + 31) & ~31) == colorBufferHeight)
			colorBufferHeight = scissorBoxHeight;
	}

	// log resolution changes if the above heuristic takes effect
	// this is useful to find resolutions which need to be updated in gfx pack texture rules
#if 0
	uint32 colorBufferHeight2 = pitchHeight / colorBufferPitch;
	static std::unordered_set<uint64> s_foundColorBufferResMappings;
	if (colorBufferPitch != colorBufferWidth || colorBufferHeight != colorBufferHeight2)
	{
		// only log unique, source and dest resolution. Encode into a key with 16 bits per component
		uint64 resHash = (uint64)colorBufferWidth | ((uint64)colorBufferHeight << 16) | ((uint64)colorBufferPitch << 32) | ((uint64)colorBufferHeight2 << 48);
		if( !s_foundColorBufferResMappings.contains(resHash) )
		{
			s_foundColorBufferResMappings.insert(resHash);
			cemuLog_log(LogType::Force, "[COLORBUFFER-DBG] Using res {}x{} instead of {}x{}", colorBufferWidth, colorBufferHeight, colorBufferPitch, colorBufferHeight2);
		}
	}
#endif

	bool colorBufferWasFound = false;
	sint32 viewFirstMip = 0; // todo

	cemu_assert_debug(viewNumSlices == 1);

	LatteTextureView* colorBufferView = LatteTextureViewLookupCache::lookupSliceEx(colorBufferPhysMem, colorBufferWidth, colorBufferHeight, colorBufferPitch, viewFirstMip, viewFirstSlice, colorBufferFormat, false);

	if (colorBufferView == nullptr)
	{
		// create color buffer view
		colorBufferView = LatteTexture_CreateMapping(colorBufferPhysMem, 0, colorBufferWidth, colorBufferHeight, (viewFirstSlice + viewNumSlices), colorBufferPitch, colorBufferTileMode, colorBufferSwizzle>>8, viewFirstMip, 1, viewFirstSlice, viewNumSlices, (Latte::E_GX2SURFFMT)colorBufferFormat, (viewFirstSlice + viewNumSlices)>1? Latte::E_DIM::DIM_2D_ARRAY: Latte::E_DIM::DIM_2D, Latte::E_DIM::DIM_2D, false, true);
		LatteGPUState.repeatTextureInitialization = true;
		checkForTextureChanges = false;
	}

	if (colorBufferView->baseTexture->swizzle != colorBufferSwizzle)
	{
		colorBufferView->baseTexture->lastRenderTargetSwizzle = colorBufferSwizzle;
	}

	// check for texture changes
	if (checkForTextureChanges)
		LatteTexture_UpdateDataToLatest(colorBufferView->baseTexture);
	// mark as used
	LatteTC_MarkTextureStillInUse(colorBufferView->baseTexture);
	return colorBufferView;
}

// get mask of all used color buffers
uint8 LatteMRT::GetActiveColorBufferMask(const LatteDecompilerShader* pixelShader, const LatteContextRegister& lcr)
{
	if (!pixelShader) [[unlikely]]
		return 0;
	const uint32* regView = lcr.GetRawView();
	// check if color buffer output is active
	const Latte::LATTE_CB_COLOR_CONTROL& colorControlReg = lcr.CB_COLOR_CONTROL;
	uint32 colorBufferDisable = colorControlReg.get_SPECIAL_OP() == Latte::LATTE_CB_COLOR_CONTROL::E_SPECIALOP::DISABLE;
	if (colorBufferDisable)
		return 0;
	cemu_assert_debug(colorControlReg.get_DEGAMMA_ENABLE() == false); // not supported
	// start with color buffer mask from pixel shader output
	uint8 colorBufferMask = pixelShader->pixelColorOutputMask;
	// combine color buffer mask with color channel mask from mmCB_TARGET_MASK (disable render buffer if all colors are blocked)
	uint32 channelTargetMask = lcr.CB_TARGET_MASK.get_MASK();
	for (uint32 i = 0; i < 8; i++)
	{
		if (((channelTargetMask >> (i * 4)) & 0xF) == 0)
			colorBufferMask &= ~(1 << i);
	}
	// render targets smaller than the scissor size are not allowed
	// this fixes a few render issues in Cemu but we dont know if this matches HW behavior
	// also check for color buffers without a valid pointer
	cemu_assert_debug(lcr.PA_SC_GENERIC_SCISSOR_TL.get_WINDOW_OFFSET_DISABLE() == true); // todo (not exposed by GX2 API)
	uint32 scissorAccessWidth = lcr.PA_SC_GENERIC_SCISSOR_BR.get_BR_X();
	uint32 scissorAccessHeight = lcr.PA_SC_GENERIC_SCISSOR_BR.get_BR_Y();
	for (uint32 i = 0; i < 8; i++)
	{
		if( (colorBufferMask&(1<<i)) == 0 )
			continue;
		if (regView[mmCB_COLOR0_BASE + i] == MPTR_NULL) [[unlikely]]
			colorBufferMask &= ~(1 << i);
		// get width/height
		uint32 regColorSize = regView[mmCB_COLOR0_SIZE + i];
		uint32 regColorInfo = regView[mmCB_COLOR0_INFO + i];
		// decode color buffer reg info
		uint32 colorBufferPitch = (((regColorSize >> 0) & 0x3FF) + 1);
		colorBufferPitch <<= 3;
		uint32 pitchHeight = (((regColorSize >> 10) & 0xFFFFF) + 1);
		pitchHeight <<= 6;
		uint32 colorBufferHeight = pitchHeight / colorBufferPitch;
		uint32 colorBufferWidth = colorBufferPitch;

		if ((colorBufferWidth < (sint32)scissorAccessWidth) || (colorBufferHeight < (sint32)scissorAccessHeight))
		{
            // log this?
			colorBufferMask &= ~(1<<i);
		}
	}
	return colorBufferMask;
}

// returns true if depth/stencil buffer is used
bool LatteMRT::GetActiveDepthBufferMask(const LatteContextRegister& lcr)
{
	bool depthBufferMask = true;
	// if depth test is not used then detach the depth buffer
	bool depthEnable = lcr.DB_DEPTH_CONTROL.get_Z_ENABLE();
	bool stencilTestEnable = lcr.DB_DEPTH_CONTROL.get_STENCIL_ENABLE();
	bool backStencilEnable = lcr.DB_DEPTH_CONTROL.get_BACK_STENCIL_ENABLE();

	if (!depthEnable && !stencilTestEnable && !backStencilEnable)
		depthBufferMask = false;

	return depthBufferMask;
}

const uint32 _colorBufferFormatBits[] =
{
	0, // 0
	0x200, // 1
	0, // 2
	0, // 3
	0x100, // 4
	0x300, // 5
	0x400, // 6
	0x800, // 7
};

Latte::E_GX2SURFFMT LatteMRT::GetColorBufferFormat(const uint32 index, const LatteContextRegister& lcr)
{
	cemu_assert_debug(index < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS);
	uint32 regColorInfo = lcr.GetRawView()[mmCB_COLOR0_INFO + index];
	uint32 colorBufferFormat = (regColorInfo >> 2) & 0x3F; // base HW format
	uint32 numberType = (regColorInfo >> 12) & 7;
	colorBufferFormat |= _colorBufferFormatBits[numberType];
	return (Latte::E_GX2SURFFMT)colorBufferFormat;
}

// return GX2 format of current depth buffer
Latte::E_GX2SURFFMT LatteMRT::GetDepthBufferFormat(const LatteContextRegister& lcr)
{
	uint32 regDepthBufferInfo = lcr.GetRawView()[mmDB_DEPTH_INFO];
	switch (regDepthBufferInfo & 7)
	{
	case 1:
		return Latte::E_GX2SURFFMT::D16_UNORM;
	case 3:
		return Latte::E_GX2SURFFMT::D24_S8_UNORM;
	case 5:
		return Latte::E_GX2SURFFMT::D24_S8_FLOAT;
	case 6:
		return Latte::E_GX2SURFFMT::D32_FLOAT;
	case 7:
		return Latte::E_GX2SURFFMT::D32_S8_FLOAT;
	default:
		debug_printf("Invalid DB_DEPTH_INFO depthbuffer format (%d)\n", (regDepthBufferInfo & 7));
		break;
	}
	return Latte::E_GX2SURFFMT::D16_UNORM;
}

bool LatteMRT::UpdateCurrentFBO()
{
	catchOpenGLError();
	sLatteRenderTargetState.rtUpdateListCount = 0;
	// combine color buffer mask with pixel output mask from pixel shader
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	uint8 colorBufferMask = GetActiveColorBufferMask(pixelShader, LatteGPUState.contextNew);
	bool depthBufferMask = GetActiveDepthBufferMask(LatteGPUState.contextNew);

	bool hasResizedTexture = false; // set to true if any of the color buffers or the depth buffer reference a resized texture (via graphic pack texture rules)
	sLatteRenderTargetState.renderTargetIsResized = false;
	// real size
	LatteMRTQuad* rtRealSize = &sLatteRenderTargetState.currentRenderSize;
	rtRealSize->width = 0;
	rtRealSize->height = 0;
	// effective size (differs from real size only if graphic pack rules overwrite texture sizes)
	LatteMRTQuad* rtEffectiveSize = &sLatteRenderTargetState.currentEffectiveSize;
	rtEffectiveSize->width = 0;
	rtEffectiveSize->height = 0;
	// get scissor box
	cemu_assert_debug(LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_TL.get_WINDOW_OFFSET_DISABLE() == true); // todo (not exposed by GX2 API?)
	uint32 scissorX = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_TL.get_TL_X();
	uint32 scissorY = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_TL.get_TL_Y();
	uint32 scissorWidth = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_BR.get_BR_X() - scissorX;
	uint32 scissorHeight = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_BR.get_BR_Y() - scissorY;
	uint32 scissorAccessWidth = scissorX + scissorWidth;
	uint32 scissorAccessHeight = scissorY + scissorHeight;
	// color buffers
	for (uint32 i = 0; i < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS; i++)
	{
		if (((colorBufferMask)&(1 << i)) == 0)
		{
			// unbind
			SetColorAttachment(i, nullptr);
			continue;
		}
		LatteTextureView* colorAttachmentView = GetColorAttachmentTexture(i, true, true);
		SetColorAttachment(i, colorAttachmentView);
		// after the drawcall mark the texture as updated
		sLatteRenderTargetState.rtUpdateList[sLatteRenderTargetState.rtUpdateListCount] = colorAttachmentView;
		sLatteRenderTargetState.rtUpdateListCount++;

		sint32 colorAttachmentWidth, colorAttachmentHeight;
		colorAttachmentView->baseTexture->GetSize(colorAttachmentWidth, colorAttachmentHeight, colorAttachmentView->firstMip);

		// set effective size
		sint32 effectiveWidth, effectiveHeight;
		colorAttachmentView->baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, colorAttachmentView->firstMip);
		if (rtEffectiveSize->width == 0 && rtEffectiveSize->height == 0)
		{
			rtEffectiveSize->width = effectiveWidth;
			rtEffectiveSize->height = effectiveHeight;
		}
		else if (rtEffectiveSize->width != effectiveWidth && rtEffectiveSize->height != effectiveHeight)
		{
			cemuLog_logDebug(LogType::Force, "Color buffer size mismatch ({}x{}). Effective size: {}x{} Real size: {}x{} Mismatching texture: {:08x} {}x{} fmt {:04x}", rtEffectiveSize->width, rtEffectiveSize->height, effectiveWidth, effectiveHeight, colorAttachmentView->baseTexture->width, colorAttachmentView->baseTexture->height, colorAttachmentView->baseTexture->physAddress, colorAttachmentView->baseTexture->width, colorAttachmentView->baseTexture->height, (uint32)colorAttachmentView->baseTexture->format);
		}
		// currently the first color attachment defines the size of the current render target
		if (rtRealSize->width == 0 && rtRealSize->height == 0)
		{
			rtRealSize->width = colorAttachmentWidth;
			rtRealSize->height = colorAttachmentHeight;
		}

		if (colorAttachmentView)
			continue;
	}
	// depth buffer
	if (depthBufferMask)
	{
		uint32 regDepthBuffer = LatteGPUState.contextRegister[mmDB_HTILE_DATA_BASE];
		uint32 regDepthSize = LatteGPUState.contextRegister[mmDB_DEPTH_SIZE];
		uint32 regDepthBufferInfo = LatteGPUState.contextRegister[mmDB_DEPTH_INFO];

		// get format and tileMode from info reg
		Latte::E_GX2SURFFMT depthBufferFormat = GetDepthBufferFormat(LatteGPUState.contextNew);
		Latte::E_HWTILEMODE depthBufferTileMode = (Latte::E_HWTILEMODE)((regDepthBufferInfo >> 15) & 0xF);
		MPTR depthBufferPhysMem = regDepthBuffer << 8;

		uint32 depthBufferPitch = (((regDepthSize >> 0) & 0x3FF) + 1);
		uint32 depthBufferHeight = ((((regDepthSize >> 10) & 0xFFFFF) + 1) / depthBufferPitch);
		depthBufferPitch <<= 3;
		depthBufferHeight <<= 3;
		uint32 depthBufferWidth = depthBufferPitch;

		if (depthBufferWidth < 2)
		{
			debug_printf("depthBufferWidth has invalid value %d\n", depthBufferWidth);
			depthBufferWidth = 2;
		}
		if (depthBufferHeight < 2)
		{
			debug_printf("depthBufferHeight has invalid value %d\n", depthBufferHeight);
			depthBufferHeight = 2;
		}

		bool blockDepthBuffer = false;
		if (scissorAccessWidth > depthBufferPitch || scissorAccessHeight > depthBufferHeight)
		{
			SetDepthAndStencilAttachment(nullptr, false);
			blockDepthBuffer = true;
			// set effective size
			if( rtEffectiveSize->width == 0 && rtEffectiveSize->height == 0 )
			{
				rtEffectiveSize->width = rtRealSize->width;
				rtEffectiveSize->height = rtRealSize->height;
			}
		}

		if (blockDepthBuffer == false)
		{
			if (rtRealSize->width == 0)
			{
				rtRealSize->width = depthBufferWidth;
				rtRealSize->height = depthBufferHeight;
			}
			uint32 regDepthView = LatteGPUState.contextRegister[mmDB_DEPTH_VIEW];
			uint32 depthBufferViewFirstSlice = (regDepthView & 0x7FF);
			uint32 depthBufferViewNumSlices = ((regDepthView >> 13) & 0x7FF) - depthBufferViewFirstSlice + 1;
			cemu_assert_debug(depthBufferViewNumSlices == 1); // binding multiple layers makes no sense?

			uint32 depthBufferSwizzle = 0;
			if (Latte::TM_IsMacroTiled(depthBufferTileMode))
			{
				depthBufferSwizzle = (depthBufferPhysMem >> 8) & 7;
				depthBufferPhysMem = depthBufferPhysMem & ~(7 << 8);
			}

			if (depthBufferPhysMem != MPTR_NULL)
			{
				LatteTextureView* depthBufferView = LatteTextureViewLookupCache::lookupSliceEx(depthBufferPhysMem, depthBufferWidth, depthBufferHeight, depthBufferPitch, 0, depthBufferViewFirstSlice, depthBufferFormat, true);
				if (!depthBufferView)
				{
					// create new depth buffer view and if it doesn't exist then also create the texture
					depthBufferView = LatteTexture_CreateMapping(depthBufferPhysMem, 0, depthBufferWidth, depthBufferHeight, depthBufferViewFirstSlice+1, depthBufferPitch, depthBufferTileMode, depthBufferSwizzle, 0, 1, depthBufferViewFirstSlice, 1, depthBufferFormat, depthBufferViewFirstSlice > 0 ? Latte::E_DIM::DIM_2D_ARRAY : Latte::E_DIM::DIM_2D, Latte::E_DIM::DIM_2D, true, true);
					LatteGPUState.repeatTextureInitialization = true;
				}
				else
				{
					// check for texture changes
					LatteTexture_UpdateDataToLatest(depthBufferView->baseTexture);
				}
				// set effective size
				sint32 effectiveWidth, effectiveHeight;
				depthBufferView->baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, depthBufferView->firstMip);
				if (rtEffectiveSize->width == 0 && rtEffectiveSize->height == 0)
				{
					rtEffectiveSize->width = effectiveWidth;
					rtEffectiveSize->height = effectiveHeight;
				}
				else if (rtEffectiveSize->width > effectiveWidth && rtEffectiveSize->height > effectiveHeight)
				{
					if (_depthBufferSizeWarningCount < 100)
					{
						cemuLog_logDebug(LogType::Force, "Depth buffer size too small. Effective size: {}x{} Real size: {}x{} Mismatching texture: {:08x} {}x{} fmt {:04x}", effectiveWidth, effectiveHeight, depthBufferView->baseTexture->width, depthBufferView->baseTexture->height, depthBufferView->baseTexture->physAddress, depthBufferView->baseTexture->width, depthBufferView->baseTexture->height, (uint32)depthBufferView->baseTexture->format);
						_depthBufferSizeWarningCount++;
					}
				}
				LatteTC_MarkTextureStillInUse(depthBufferView->baseTexture);
				// after the drawcall mark the texture as updated
				sLatteRenderTargetState.rtUpdateList[sLatteRenderTargetState.rtUpdateListCount] = depthBufferView;
				sLatteRenderTargetState.rtUpdateListCount++;
				SetDepthAndStencilAttachment(depthBufferView, depthBufferView->baseTexture->hasStencil);
			}
		}
		else
		{
			SetDepthAndStencilAttachment(nullptr, false);
		}
	}
	else
	{
		SetDepthAndStencilAttachment(nullptr, false);
	}
	catchOpenGLError();
	if (colorBufferMask || depthBufferMask)
	{
		hasValidFramebufferAttached = true;
	}
	else
	{
		hasValidFramebufferAttached = false;
		return true;
	}
	if( rtEffectiveSize->width != rtRealSize->width || rtEffectiveSize->height != rtRealSize->height )
	{
		//debug_printf("RenderTarget rescaled. Real: %dx%d Resized: %dx%d\n", rtRealSize->width, rtRealSize->height, rtEffectiveSize->width, rtEffectiveSize->height);
		sLatteRenderTargetState.renderTargetIsResized = true;
	}
	if (sLatteRenderTargetState.currentEffectiveSize.width == 0)
	{
		debug_printf("Render target effective size is 0. May indicate a bug in Cemu or invalid color/depth buffers\n");
		return false;
	}
	return true;
}

// return a vec4 with xy being the scale ratio for render target extend and zw being the virtual viewport dimensions
void LatteMRT::GetCurrentFragCoordScale(float* coordScale)
{
	if (sLatteRenderTargetState.renderTargetIsResized)
	{
		coordScale[0] = (float)sLatteRenderTargetState.currentRenderSize.width / (float)sLatteRenderTargetState.currentEffectiveSize.width;
		coordScale[1] = (float)sLatteRenderTargetState.currentRenderSize.height / (float)sLatteRenderTargetState.currentEffectiveSize.height;
		coordScale[2] = sLatteRenderTargetState.currentGuestViewport.width;
		coordScale[3] = sLatteRenderTargetState.currentGuestViewport.height;
	}
	else
	{
		coordScale[0] = 1.0f;
		coordScale[1] = 1.0f;
		coordScale[2] = sLatteRenderTargetState.currentGuestViewport.width;
		coordScale[3] = sLatteRenderTargetState.currentGuestViewport.height;
	}
}

void LatteMRT::GetVirtualViewportDimensions(sint32& width, sint32& height)
{
	width = sLatteRenderTargetState.currentGuestViewport.width;
	height = sLatteRenderTargetState.currentGuestViewport.height;
}

namespace
{
// Faro TAA: Halton(2,3), 8-tap centered sequence (same table used by Azahar's
// own PICA vertex jitter, ported here for consistency - the jitter pattern
// itself was never the source of that project's TAA bug, only an unrelated
// post-process history-sampling issue).
constexpr std::array<std::pair<float, float>, 8> kTaaHalton = {{
	{0.0f, -1.0f / 6.0f},
	{-0.25f, 1.0f / 6.0f},
	{0.25f, -7.0f / 18.0f},
	{-0.375f, -1.0f / 18.0f},
	{0.125f, 5.0f / 18.0f},
	{-0.125f, -5.0f / 18.0f},
	{0.375f, 1.0f / 18.0f},
	{-0.0625f, 7.0f / 18.0f},
}};
uint64 s_taaFrameIndex = 0;
} // namespace

// Advances the TAA jitter to the next Halton tap. Called once per real
// presented frame (LatteRenderTarget_itHLESwapScanBuffer) - NOT per draw call -
// so every draw within one frame samples the same sub-pixel offset.
void LatteRenderTarget_advanceTaaJitter()
{
	s_taaFrameIndex++;
}

// Faro TAA: applies uniformly to every draw call's vertex shader (via
// SET_POSITION - see LatteDecompilerEmitGLSLHeader.hpp), not just the game's
// main 3D scene pass - Cemu has no generic way to distinguish that pass from
// shadow maps, reflections, or 2D/UI overlays (see FSR2_TAA_investigacion.md).
// In practice this means UI elements get the same tiny sub-pixel wobble as
// the 3D scene; accepted as a known limitation of camera-only jitter.
void LatteMRT::GetCurrentTaaJitter(float* jitter)
{
	// Faro TAA: jitter is disabled on purpose. The resolve pass (RendererOuputShader.cpp)
	// blends history against the current frame at matching pixel coordinates with no
	// reprojection (no motion vectors, no delta between this frame's and the previous
	// frame's view/projection matrices) - real per-frame camera jitter needs that
	// reprojection to cancel back out, otherwise every jittered frame draws geometry
	// shifted by a different sub-pixel offset and the resolve blends those raw, which
	// reads as constant shimmer/trembling on edges, grass, shadows and distant detail
	// (confirmed - this is what happens if jitter is enabled here). Without reprojection,
	// jittering the camera is strictly worse than not jittering it at all, so this always
	// returns zero for now. The push-constant delivery mechanism (see SET_POSITION's own
	// comment in LatteDecompilerEmitGLSLHeader.hpp) is left in place so real jitter can be
	// re-enabled here later if/when a reprojected resolve is implemented.
	jitter[0] = 0.0f;
	jitter[1] = 0.0f;
}

// flag all FBO textures as updated via GPU
// also handle texture readback
void LatteRenderTarget_trackUpdates()
{
	// after the drawcall, mark the render target textures as dynamically updated
	uint64 eventCounter = LatteTexture_getNextUpdateEventCounter();
	for (sint32 i = 0; i < sLatteRenderTargetState.rtUpdateListCount; i++)
	{
		LatteTextureView* texView = sLatteRenderTargetState.rtUpdateList[i];
		LatteTexture* baseTexture = texView->baseTexture;
		LatteTexture_TrackTextureGPUWrite(baseTexture, texView->firstSlice, texView->firstMip, eventCounter);
		// texture readback
		if (baseTexture->enableReadback)
		{
			LatteTextureReadback_Initate(texView);
		}
	}
}

void LatteRenderTarget_itHLESwapScanBuffer()
{
	performanceMonitor.cycle[performanceMonitor.cycleIndex].frameCounter++;
	if(LatteGPUState.frameCounter > 5)
		performanceMonitor.gpuTime_frameTime.endMeasuring();
	LattePerformanceMonitor_frameEnd();
	LatteGPUState.frameCounter++;
	LatteRenderTarget_advanceTaaJitter();
	// Faro Global Enhancements: roll per-depth-buffer draw statistics over to
	// "last frame" - see LatteMRT::FindSceneDepthViewForSize, which ranks on
	// the completed frame's numbers.
	LatteMRT::AdvanceDepthStatsFrame();
	g_renderer->SwapBuffers(true, true);

	catchOpenGLError();
	performanceMonitor.gpuTime_frameTime.beginMeasuring();

	LatteTC_CleanupUnusedTextures();
	LatteSHRC_CleanupShaderStateCache();
#ifdef ENABLE_OPENGL
	LatteDraw_cleanupAfterFrame();
#endif
	LatteQuery_CancelActiveGPU7Queries();
	LatteBufferCache_notifySwapTVScanBuffer();
	LattePerformanceMonitor_frameBegin();
}

void LatteRenderTarget_applyTextureColorClear(LatteTexture* texture, uint32 sliceIndex, uint32 mipIndex, float r, float g, float b, float a, uint64 eventCounter)
{
	if (texture->isDepth)
	{
		cemuLog_logDebug(LogType::Force, "Unsupported clear depth as color");
	}
	else
	{
		g_renderer->texture_clearColorSlice(texture, sliceIndex, mipIndex, r, g, b, a);
		LatteTexture_MarkDynamicTextureAsChanged(texture->baseView, sliceIndex, mipIndex, eventCounter);
	}
}

void LatteRenderTarget_applyTextureDepthClear(LatteTexture* texture, uint32 sliceIndex, uint32 mipIndex, bool hasDepthClear, bool hasStencilClear, float depthValue, uint8 stencilValue, uint64 eventCounter)
{
	if(texture->isDepth)
	{
		if (hasDepthClear)
		{
			// A clear is the real boundary between one set of depth contents
			// and the next, and it is where the snapshot rule has to start
			// counting again - see LatteMRT::NotifyDepthBufferCleared.
			LatteMRT::NotifyDepthBufferCleared(texture);
		}
		g_renderer->texture_clearDepthSlice(texture, sliceIndex, mipIndex, hasDepthClear, hasStencilClear, depthValue, stencilValue);
	}
	else
	{
		// clearing a color texture using depth clear
		if (hasStencilClear)
			return; // operation likely not intended as a color clear
		//cemu_assert_debug(!hasStencilClear);
		if (hasDepthClear)
		{
			g_renderer->texture_clearColorSlice(texture, sliceIndex, mipIndex, depthValue, depthValue, depthValue, depthValue);
		}

	}
	LatteTexture_MarkDynamicTextureAsChanged(texture->baseView, sliceIndex, mipIndex, eventCounter);
}

void LatteRenderTarget_itHLEClearColorDepthStencil(uint32 clearMask,
												   MPTR colorBufferMPTR, Latte::E_GX2SURFFMT colorBufferFormat, Latte::E_HWTILEMODE colorBufferTilemode, uint32 colorBufferWidth, uint32 colorBufferHeight, uint32 colorBufferPitch, uint32 colorBufferViewFirstSlice, uint32 colorBufferViewNumSlice,
												   MPTR depthBufferMPTR, Latte::E_GX2SURFFMT depthBufferFormat, Latte::E_HWTILEMODE depthBufferTileMode, sint32 depthBufferWidth, sint32 depthBufferHeight, sint32 depthBufferPitch, sint32 depthBufferViewFirstSlice, sint32 depthBufferViewNumSlice,
												   float r, float g, float b, float a, float clearDepth, uint32 clearStencil)
{
	uint32 depthBufferMipIndex = 0; // todo
	uint32 colorBufferMipIndex = 0; // todo

	bool hasColorClear = (clearMask & 1);
	bool hasDepthClear = (clearMask & 2);
	bool hasStencilClear = (clearMask & 4);

	// extract swizzle offset from pointer
	uint32 colorBufferSwizzle = 0;
	uint32 depthBufferSwizzle = 0;
	if (Latte::TM_IsMacroTiled(colorBufferTilemode))
	{
		colorBufferSwizzle = (colorBufferMPTR >> 8) & 7;
		colorBufferMPTR = colorBufferMPTR & ~(7 << 8);
	}
	if (Latte::TM_IsMacroTiled(depthBufferTileMode))
	{
		depthBufferSwizzle = (depthBufferMPTR >> 8) & 7;
		depthBufferMPTR = depthBufferMPTR & ~(7 << 8);
	}

	cemu_assert_debug(colorBufferViewNumSlice <= 1);
	cemu_assert_debug(depthBufferViewNumSlice <= 1);

	// clear color buffer (if flag set)
	uint64 eventCounter = LatteTexture_getNextUpdateEventCounter();
	if ((clearMask & 1) != 0 && colorBufferMPTR != MPTR_NULL && colorBufferWidth > 0 && colorBufferHeight > 0)
	{
		// clear color
		sint32 searchIndex = 0;
		bool targetFound = false;
		while (true)
		{
			LatteTextureView* colorView = LatteTC_LookupTextureByData(colorBufferMPTR, colorBufferWidth, colorBufferHeight, colorBufferPitch, 0, 1, colorBufferViewFirstSlice, 1, &searchIndex);
			if (!colorView)
				break;
			if (Latte::GetFormatBits(colorBufferFormat) != Latte::GetFormatBits(colorView->baseTexture->format))
				continue;

			if (colorView->baseTexture->pitch == colorBufferPitch && colorView->baseTexture->height == colorBufferHeight)
				targetFound = true;

			LatteRenderTarget_applyTextureColorClear(colorView->baseTexture, colorBufferViewFirstSlice, colorBufferMipIndex, r, g, b, a, eventCounter);

		}
		if (targetFound == false)
		{
			// create new texture with matching format
			cemu_assert_debug(colorBufferViewNumSlice <= 1);
			LatteTextureView* newColorView = LatteTexture_CreateMapping(colorBufferMPTR, MPTR_NULL, colorBufferWidth, colorBufferHeight, colorBufferViewFirstSlice+1, colorBufferPitch, colorBufferTilemode, colorBufferSwizzle, 0, 1, colorBufferViewFirstSlice, 1, colorBufferFormat, colorBufferViewFirstSlice > 0 ? Latte::E_DIM::DIM_2D_ARRAY : Latte::E_DIM::DIM_2D, Latte::E_DIM::DIM_2D, false);
			LatteRenderTarget_applyTextureColorClear(newColorView->baseTexture, colorBufferViewFirstSlice, colorBufferMipIndex, r, g, b, a, eventCounter);
		}
	}
	// clear depth or stencil buffer (if flag set)
	if ((hasDepthClear || hasStencilClear) && depthBufferMPTR != MPTR_NULL)
	{
		std::vector<LatteTexture*> list_depthClearTextures;
		LatteTC_LookupTexturesByPhysAddr(depthBufferMPTR, list_depthClearTextures);
		bool foundMatchingDepthBuffer = false;
		// todo - support for clearing depth mips?
		cemu_assert_debug(depthBufferViewNumSlice == 1);

		for (auto& texItr : list_depthClearTextures)
		{
			// only clear depth buffers if they are smaller
			if (texItr->pitch > depthBufferPitch)
				continue;
			if (depthBufferViewFirstSlice >= texItr->depth)
				continue; // slice out of range

			if (texItr->pitch == depthBufferPitch && texItr->height == depthBufferHeight)
				foundMatchingDepthBuffer = true;

			// todo - calculate actual sliceIndex and mipIndex since the textures in list_depthClearTextures dont necessarily share the same base
			LatteRenderTarget_applyTextureDepthClear(texItr, depthBufferViewFirstSlice, depthBufferMipIndex, hasDepthClear, hasStencilClear, clearDepth, clearStencil, eventCounter);
		}

		if (foundMatchingDepthBuffer == false)
		{
			LatteTextureView* newDepthBufferView = LatteMRT_CreateDepthBuffer(depthBufferMPTR, depthBufferWidth, depthBufferHeight, depthBufferPitch, depthBufferTileMode, (Latte::E_GX2SURFFMT)depthBufferFormat, depthBufferSwizzle, depthBufferViewFirstSlice);
			LatteRenderTarget_applyTextureDepthClear(newDepthBufferView->baseTexture, depthBufferViewFirstSlice, depthBufferMipIndex, hasDepthClear, hasStencilClear, clearDepth, clearStencil, eventCounter);
		}
	}
}

sint32 _currentOutputImageWidth = 0;
sint32 _currentOutputImageHeight = 0;

void LatteRenderTarget_getScreenImageArea(sint32* x, sint32* y, sint32* width, sint32* height, sint32* fullWidth, sint32* fullHeight, bool padView)
{
	int w, h;
	if(padView && WindowSystem::IsPadWindowOpen())
		WindowSystem::GetPadWindowPhysSize(w, h);
	else
		WindowSystem::GetWindowPhysSize(w, h);

	sint32 scaledOutputX;
	sint32 scaledOutputY;
	if (GetConfig().fullscreen_scaling == kKeepAspectRatio)
	{
		// calculate maximum possible resolution with intact aspect ratio
		scaledOutputX = w;
		scaledOutputY = _currentOutputImageHeight * w / std::max(_currentOutputImageWidth, 1);
		if (scaledOutputY > h)
		{
			scaledOutputX = _currentOutputImageWidth * h / std::max(_currentOutputImageHeight, 1);
			scaledOutputY = h;
		}
	}
	else
	{
		scaledOutputX = w;
		scaledOutputY = h;
	}

	*x = (w - scaledOutputX) / 2;
	*y = (h - scaledOutputY) / 2;
	*width = scaledOutputX;
	*height = scaledOutputY;
	if (fullWidth)
		*fullWidth = w;
	if (fullHeight)
		*fullHeight = h;
}

void LatteRenderTarget_copyToBackbuffer(LatteTextureView* textureView, bool isPadView)
{
	// make sure texture is updated to latest data in cache
	LatteTexture_UpdateDataToLatest(textureView->baseTexture);
	// mark source texture as still in use
	LatteTC_MarkTextureStillInUse(textureView->baseTexture);

	sint32 effectiveWidth, effectiveHeight;
	textureView->baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, 0);
	_currentOutputImageWidth = effectiveWidth;
	_currentOutputImageHeight = effectiveHeight;

	// Faro Global Enhancements (Phase 0): probe scene depth for later passes.
	// Recording only - see Renderer::CaptureDepthForPostFX. Needs the output
	// resolution (just computed above) to pick the right tracked depth view -
	// see FindSceneDepthViewForSize's own comment.
	//
	// Scene depth is captured for the post-processing effects only. This was
	// briefly widened to also capture whenever TAA was active, because the
	// motion estimator matched on depth too; that estimator is disabled (see
	// RunFaroMotionEstimation) and the widened gate made depth capture run in
	// configurations that never used it, so it is back to the SSAO-verified
	// condition. Re-widen it only alongside a consumer that needs it.
	if (GetConfig().faro_postfx_enabled)
	{
		g_renderer->CaptureDepthForPostFX(effectiveWidth, effectiveHeight);
	}

	sint32 imageX, imageY;
	sint32 imageWidth, imageHeight;
	sint32 fullscreenWidth, fullscreenHeight;

	LatteRenderTarget_getScreenImageArea(&imageX, &imageY, &imageWidth, &imageHeight, &fullscreenWidth, &fullscreenHeight, isPadView);

	bool clearBackground = false;
	if (imageWidth != fullscreenWidth || imageHeight != fullscreenHeight)
		clearBackground = true;

	const bool renderUpsideDown = ActiveSettings::RenderUpsideDownEnabled();
	// force disable bicubic scaling if output resolution is smaller than input
	// resolution. Deliberately strict (<, not <=): at an exact 1:1 match
	// (internal resolution == output resolution, a common config for users who
	// just want FSR1's RCAS sharpening or FXAA's edge smoothing without any
	// real magnification) there's no actual scaling happening either way, but
	// FSR1/FXAA still have a real visible effect at 1:1 unlike the older
	// filters (which are effectively identity at 1:1 regardless of which list
	// they come from) - so 1:1 must route through the upscale path or those
	// two filters silently never run.
	const bool downscaling = (imageWidth < effectiveWidth || imageHeight < effectiveHeight);
	// check for graphic pack shaders
	RendererOutputShader* shader = nullptr;
	LatteTextureView::MagFilter filter = LatteTextureView::MagFilter::kLinear;
	bool usedFsr1 = false;
	for(const auto& gp : GraphicPack2::GetActiveGraphicPacks())
	{
		if(downscaling)
		{
			shader = gp->GetDownscalingShader(renderUpsideDown);
			if (shader)
			{
				filter = gp->GetDownscalingMagFilter();
				break;
			}
		}
		else
		{
			shader = gp->GetUpscalingShader(renderUpsideDown);
			if (shader)
			{
				filter = gp->GetUpscalingMagFilter();
				break;
			}
		}

		shader = gp->GetOuputShader(renderUpsideDown);
		if (shader)
		{
			filter = downscaling ? gp->GetDownscalingMagFilter() : gp->GetUpscalingMagFilter();
			break;
		}
	}

	if (shader == nullptr)
	{
		sint32 scaling_filter = downscaling ? GetConfig().downscale_filter : GetConfig().upscale_filter;

		if (scaling_filter == kLinearFilter)
		{
			if(renderUpsideDown)
				shader = RendererOutputShader::s_copy_shader_ud;
			else
				shader = RendererOutputShader::s_copy_shader;

			filter = LatteTextureView::MagFilter::kLinear;
		}
		else if (scaling_filter == kBicubicFilter)
		{
			if (renderUpsideDown)
				shader = RendererOutputShader::s_bicubic_shader_ud;
			else
				shader = RendererOutputShader::s_bicubic_shader;

			filter = LatteTextureView::MagFilter::kLinear;
		}
		else if (scaling_filter == kBicubicHermiteFilter)
		{
			if (renderUpsideDown)
				shader = RendererOutputShader::s_hermit_shader_ud;
			else
				shader = RendererOutputShader::s_hermit_shader;

			filter = LatteTextureView::MagFilter::kLinear;
		}
		else if (scaling_filter == kNearestNeighborFilter)
		{
			if (renderUpsideDown)
				shader = RendererOutputShader::s_copy_shader_ud;
			else
				shader = RendererOutputShader::s_copy_shader;

			filter = LatteTextureView::MagFilter::kNearestNeighbor;
		}
		else if (scaling_filter == kFsr1Filter)
		{
			// FSR1's EASU only makes sense for upscaling (see GeneralSettings2.cpp's
			// own note on why it's not even offered as a downscale option there) -
			// falls back to Bicubic here too, as defense in depth for a
			// settings.xml written before that UI restriction existed. Also
			// falls back on Metal, which has no FSR1 port yet (s_fsr1_easu_shader/
			// s_fsr1_rcas_shader stay null there) - either way, this avoids ever
			// hitting the cemu_assert(shader) below with a null shader.
			if (downscaling || (renderUpsideDown ? !RendererOutputShader::s_fsr1_easu_shader_ud : !RendererOutputShader::s_fsr1_easu_shader) || !RendererOutputShader::s_fsr1_rcas_shader)
			{
				shader = renderUpsideDown ? RendererOutputShader::s_bicubic_shader_ud : RendererOutputShader::s_bicubic_shader;
			}
			else
			{
				// This is EASU (pass 1 of FSR1) - RCAS (pass 2) always runs as a
				// genuinely separate pass, since real RCAS needs to read
				// already-upscaled neighbour pixels that don't exist yet during
				// EASU itself. See DrawBackbufferQuadFsr1's own doc comment.
				shader = renderUpsideDown ? RendererOutputShader::s_fsr1_easu_shader_ud : RendererOutputShader::s_fsr1_easu_shader;
				usedFsr1 = true;
			}

			filter = LatteTextureView::MagFilter::kLinear;
		}
	}
	cemu_assert(shader);
	// TEMP DEBUG (Faro GTAO diagnostic): log once whether the FSR1 dispatch
	// branch is even taken, and with what resolutions - to determine why
	// RunFaroGtaoPass is never being reached despite master+SSAO+depth all
	// confirmed valid.
	if (GetConfig().faro_postfx_enabled)
	{
		static bool s_gtaoDispatchDebugLogged = false;
		if (!s_gtaoDispatchDebugLogged)
		{
			s_gtaoDispatchDebugLogged = true;
			cemuLog_log(LogType::Force, "Faro GTAO debug: usedFsr1={} downscaling={} imageWH={}x{} effectiveWH={}x{} aaMode={}",
				usedFsr1, downscaling, imageWidth, imageHeight, effectiveWidth, effectiveHeight, GetConfig().antialiasing_mode.GetValue());
		}
	}
	// FSR1 always needs a 2nd pass (RCAS, see s_fsr1_rcas_shader's doc comment)
	// on top of the EASU pass selected above. FXAA/SMAA (only offered as
	// add-ons to FSR1 - see GeneralSettings2.cpp / CemuConfig.h:AntialiasingMode)
	// then need further passes over the already-upscaled-and-sharpened image
	// (FXAA: 1 more pass via DrawBackbufferQuadTwoPass, SMAA: 3 more passes via
	// DrawBackbufferQuadFsr1Smaa). Fall back to the plain single-shader dispatch
	// if the active renderer doesn't implement the relevant multi-pass path
	// (OpenGL/Metal) or if it couldn't (re)create its intermediate target(s)
	// this frame.
	bool drewMultiPass = false;
	if (usedFsr1)
	{
		const sint32 aaMode = GetConfig().antialiasing_mode;
		if (aaMode == CemuConfig::kAAFxaa && RendererOutputShader::s_fxaa_shader)
		{
			drewMultiPass = g_renderer->DrawBackbufferQuadTwoPass(textureView, shader, RendererOutputShader::s_fsr1_rcas_shader, RendererOutputShader::s_fxaa_shader,
				filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
		}
		else if (aaMode == CemuConfig::kAASmaa && RendererOutputShader::s_smaa_neighborhood_shader)
		{
			// s_smaa_edge_shader/s_smaa_blend_shader are indexed by smaa_quality
			// (kSmaaLow..kSmaaUltra) - all 4 presets are compiled upfront in
			// InitializeStatic, so this never needs a shader recompile.
			sint32 smaaQuality = GetConfig().smaa_quality;
			if (smaaQuality < 0 || smaaQuality >= RendererOutputShader::kSmaaQualityCount)
				smaaQuality = CemuConfig::kSmaaHigh;
			RendererOutputShader* edgeShader = RendererOutputShader::s_smaa_edge_shader[smaaQuality];
			RendererOutputShader* blendShader = RendererOutputShader::s_smaa_blend_shader[smaaQuality];
			if (edgeShader && blendShader)
			{
				drewMultiPass = g_renderer->DrawBackbufferQuadFsr1Smaa(textureView, shader, RendererOutputShader::s_fsr1_rcas_shader,
					edgeShader, blendShader, RendererOutputShader::s_smaa_neighborhood_shader,
					filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
			}
		}
		else if (aaMode == CemuConfig::kAATaa && RendererOutputShader::s_taa_resolve_shader && RendererOutputShader::s_fxaa_shader)
		{
			// Faro TAA: spatial pre-pass is user-selectable (FXAA vs SMAA,
			// CemuConfig::taa_spatial_aa) - SMAA's morphological edge search
			// needs a minimum-width contrast pattern to find an edge at all,
			// so it barely helps the very thin/sub-pixel aliasing common in
			// Wii U content (foliage, hair, distant geometry); FXAA's
			// directional blur catches more of that but is softer. Both
			// shader sets are always passed down; DrawBackbufferQuadFsr1Taa
			// picks which one actually runs based on the config value.
			// TAA stays its own distinct antialiasing_mode entry.
			sint32 smaaQuality = GetConfig().smaa_quality;
			if (smaaQuality < 0 || smaaQuality >= RendererOutputShader::kSmaaQualityCount)
				smaaQuality = CemuConfig::kSmaaHigh;
			drewMultiPass = g_renderer->DrawBackbufferQuadFsr1Taa(textureView, shader, RendererOutputShader::s_fsr1_rcas_shader,
				RendererOutputShader::s_taa_resolve_shader, RendererOutputShader::s_fxaa_shader,
				RendererOutputShader::s_smaa_edge_shader[smaaQuality], RendererOutputShader::s_smaa_blend_shader[smaaQuality], RendererOutputShader::s_smaa_neighborhood_shader,
				filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
		}
		else
		{
			drewMultiPass = g_renderer->DrawBackbufferQuadFsr1(textureView, shader, RendererOutputShader::s_fsr1_rcas_shader,
				filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
		}
	}
	else if (!drewMultiPass)
	{
		// Faro: AA without FSR1 - same 3 add-on modes as above, but using
		// whichever single-pass filter (copy/bicubic/hermite/nearest) was
		// actually selected as the upscale/downscale stage instead of FSR1's
		// EASU+RCAS pair. This is what makes FXAA/SMAA/TAA available at any
		// resolution (including when downscaling, which FSR1 never engages
		// for) instead of only when FSR1 is the active filter.
		const sint32 aaMode = GetConfig().antialiasing_mode;
		if (aaMode == CemuConfig::kAAFxaa && RendererOutputShader::s_fxaa_shader)
		{
			drewMultiPass = g_renderer->DrawBackbufferQuadFxaa(textureView, shader, RendererOutputShader::s_fxaa_shader,
				filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
		}
		else if (aaMode == CemuConfig::kAASmaa && RendererOutputShader::s_smaa_neighborhood_shader)
		{
			sint32 smaaQuality = GetConfig().smaa_quality;
			if (smaaQuality < 0 || smaaQuality >= RendererOutputShader::kSmaaQualityCount)
				smaaQuality = CemuConfig::kSmaaHigh;
			RendererOutputShader* edgeShader = RendererOutputShader::s_smaa_edge_shader[smaaQuality];
			RendererOutputShader* blendShader = RendererOutputShader::s_smaa_blend_shader[smaaQuality];
			if (edgeShader && blendShader)
			{
				drewMultiPass = g_renderer->DrawBackbufferQuadSmaa(textureView, shader, edgeShader, blendShader, RendererOutputShader::s_smaa_neighborhood_shader,
					filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
			}
		}
		else if (aaMode == CemuConfig::kAATaa && RendererOutputShader::s_taa_resolve_shader && RendererOutputShader::s_fxaa_shader)
		{
			// Faro: AA without FSR1, TAA case - see the usedFsr1 branch's
			// own comment on the FXAA/SMAA spatial-pre-pass choice.
			sint32 smaaQuality = GetConfig().smaa_quality;
			if (smaaQuality < 0 || smaaQuality >= RendererOutputShader::kSmaaQualityCount)
				smaaQuality = CemuConfig::kSmaaHigh;
			drewMultiPass = g_renderer->DrawBackbufferQuadTaa(textureView, shader, RendererOutputShader::s_taa_resolve_shader, RendererOutputShader::s_fxaa_shader,
				RendererOutputShader::s_smaa_edge_shader[smaaQuality], RendererOutputShader::s_smaa_blend_shader[smaaQuality], RendererOutputShader::s_smaa_neighborhood_shader,
				filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
		}
	}
	if (!drewMultiPass)
	{
		// Counted, because this is a silent per-frame behaviour change: the
		// frame gets drawn with no antialiasing and no temporal resolve at
		// all, which pops visibly against its neighbours. Any of the
		// multi-pass dispatches can land here just by failing to allocate a
		// target, and only some of those paths log anything.
		if (GetConfig().antialiasing_mode != CemuConfig::kAANone)
			g_renderer->NotifyAaFallbackFrame();
		g_renderer->DrawBackbufferQuad(textureView, shader, filter==LatteTextureView::MagFilter::kLinear, imageX, imageY, imageWidth, imageHeight, isPadView, clearBackground);
	}
	g_renderer->HandleScreenshotRequest(textureView, isPadView);
	if (!g_renderer->ImguiBegin(!isPadView))
		return;
	swkbd_render(!isPadView);
	nn::erreula::render(!isPadView);
	LatteOverlay_render(isPadView);
	g_renderer->ImguiEnd();
}

void LatteRenderTarget_itHLECopyColorBufferToScanBuffer(MPTR colorBufferPtr, uint32 colorBufferWidth, uint32 colorBufferHeight, uint32 colorBufferSliceIndex, uint32 colorBufferFormat, uint32 colorBufferPitch, Latte::E_HWTILEMODE colorBufferTilemode, uint32 colorBufferSwizzle, uint32 renderTarget)
{
	cemu_assert_debug(colorBufferSliceIndex == 0); // todo - support for non-zero slice
	LatteTextureView* texView = LatteTC_GetTextureSliceViewOrTryCreate(colorBufferPtr, MPTR_NULL, (Latte::E_GX2SURFFMT)colorBufferFormat, colorBufferTilemode, colorBufferWidth, colorBufferHeight, 1, colorBufferPitch, colorBufferSwizzle, 0, 0, true);
	if (!texView)
	{
		return;
	}

	auto getVPADScreenActive = [](size_t n) -> std::pair<bool, bool> {
		auto controller = InputManager::instance().get_vpad_controller(n);
		if (!controller)
			return {false,false};
		auto pressed = controller->is_screen_active();
		auto toggle = controller->is_screen_active_toggle();
		return {pressed && !toggle, pressed && toggle};
	};

	const bool tabPressed = WindowSystem::IsKeyDown(WindowSystem::PlatformKeyCodes::TAB);
	const bool ctrlPressed = WindowSystem::IsKeyDown(WindowSystem::PlatformKeyCodes::LCONTROL);
	const auto [vpad0Active, vpad0Toggle] = getVPADScreenActive(0);
	const auto [vpad1Active, vpad1Toggle] = getVPADScreenActive(1);

	const bool altScreenRequested = (!ctrlPressed && tabPressed) || vpad0Active || vpad1Active;
	const bool togglePressed = (ctrlPressed && tabPressed) || vpad0Toggle || vpad1Toggle;
	static bool togglePressedLast = false;

	bool& isDRCPrimary = LatteGPUState.isDRCPrimary;

	if(togglePressed && !togglePressedLast)
		isDRCPrimary = !isDRCPrimary;
	togglePressedLast = togglePressed;

	bool showDRC = swkbd_hasKeyboardInputHook() == false && (isDRCPrimary ^ altScreenRequested);

	if ((renderTarget & RENDER_TARGET_DRC) && g_renderer->IsPadWindowActive())
		LatteRenderTarget_copyToBackbuffer(texView, true);
	if (((renderTarget & RENDER_TARGET_TV) && !showDRC) || ((renderTarget & RENDER_TARGET_DRC) && showDRC))
		LatteRenderTarget_copyToBackbuffer(texView, false);
}



// returns the current size of the virtual viewport (not the same as effective size, which can be influenced by texture rules)
void LatteRenderTarget_GetCurrentVirtualViewportSize(sint32* viewportWidth, sint32* viewportHeight)
{
	*viewportWidth = sLatteRenderTargetState.currentGuestViewport.width;
	*viewportHeight = sLatteRenderTargetState.currentGuestViewport.height;
}

void LatteRenderTarget_updateViewport()
{
	float vpWidth = LatteGPUState.contextNew.PA_CL_VPORT_XSCALE.get_SCALE() / 0.5f;
	float vpX = LatteGPUState.contextNew.PA_CL_VPORT_XOFFSET.get_OFFSET() - LatteGPUState.contextNew.PA_CL_VPORT_XSCALE.get_SCALE();
	float vpHeight = LatteGPUState.contextNew.PA_CL_VPORT_YSCALE.get_SCALE() / -0.5f;
	float vpY = LatteGPUState.contextNew.PA_CL_VPORT_YOFFSET.get_OFFSET() + LatteGPUState.contextNew.PA_CL_VPORT_YSCALE.get_SCALE();

	bool halfZ = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_DX_CLIP_SPACE_DEF();

	// calculate near/far
	float farZ;
	float nearZ;
	float s = LatteGPUState.contextNew.PA_CL_VPORT_ZSCALE.get_SCALE();
	float b = LatteGPUState.contextNew.PA_CL_VPORT_ZOFFSET.get_OFFSET();
	if (halfZ == false)
	{
		farZ = s + b;
		nearZ = b - s;
	}
	else
	{
		farZ = s + b;
		nearZ = b;
	}

	sLatteRenderTargetState.currentGuestViewport.width = vpWidth;
	sLatteRenderTargetState.currentGuestViewport.height = vpHeight;
	if (sLatteRenderTargetState.renderTargetIsResized)
	{
		vpX *= ((float)sLatteRenderTargetState.currentEffectiveSize.width / (float)sLatteRenderTargetState.currentRenderSize.width);
		vpY *= ((float)sLatteRenderTargetState.currentEffectiveSize.height / (float)sLatteRenderTargetState.currentRenderSize.height);
		vpWidth *= ((float)sLatteRenderTargetState.currentEffectiveSize.width / (float)sLatteRenderTargetState.currentRenderSize.width);
		vpHeight *= ((float)sLatteRenderTargetState.currentEffectiveSize.height / (float)sLatteRenderTargetState.currentRenderSize.height);
	}
	g_renderer->renderTarget_setViewport(vpX, vpY, vpWidth, vpHeight, nearZ, farZ, halfZ);
}

void LatteRenderTarget_updateScissorBox()
{
	// update scissor box
	uint32 scissorX = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_TL.get_TL_X();
	uint32 scissorY = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_TL.get_TL_Y();
	uint32 scissorWidth = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_BR.get_BR_X() - scissorX;
	uint32 scissorHeight = LatteGPUState.contextNew.PA_SC_GENERIC_SCISSOR_BR.get_BR_Y() - scissorY;
	if( sLatteRenderTargetState.renderTargetIsResized )
	{
		scissorX = (sint32)((float)scissorX * ((float)sLatteRenderTargetState.currentEffectiveSize.width / (float)sLatteRenderTargetState.currentRenderSize.width));
		scissorY = (sint32)((float)scissorY * ((float)sLatteRenderTargetState.currentEffectiveSize.height / (float)sLatteRenderTargetState.currentRenderSize.height));
		scissorWidth = (sint32)((float)scissorWidth * ((float)sLatteRenderTargetState.currentEffectiveSize.width / (float)sLatteRenderTargetState.currentRenderSize.width));
		scissorHeight = (sint32)((float)scissorHeight * ((float)sLatteRenderTargetState.currentEffectiveSize.height / (float)sLatteRenderTargetState.currentRenderSize.height));
	}

	if( scissorX != prevScissorX || scissorY != prevScissorY || scissorWidth != prevScissorWidth || scissorHeight != prevScissorHeight )
	{
		g_renderer->renderTarget_setScissor(scissorX, scissorY, scissorWidth, scissorHeight);
		prevScissorX = scissorX;
		prevScissorY = scissorY;
		prevScissorWidth = scissorWidth;
		prevScissorHeight = scissorHeight;
	}
}

void LatteRenderTarget_unloadAll()
{
	if (g_emptyFBO)
	{
		LatteMRT::DeleteCachedFBO(g_emptyFBO);
		g_emptyFBO = nullptr;
	}
}
