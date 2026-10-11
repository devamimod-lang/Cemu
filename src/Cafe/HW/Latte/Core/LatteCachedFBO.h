#pragma once

#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "util/math/vector2.h"

class LatteCachedFBO
{
public:
	LatteCachedFBO(uint64 key);

	uint32 calculateNumColorBuffers()
	{
		uint32 n = 0;
		for (sint32 i = 0; i < 8; i++)
			if (colorBuffer[i].texture)
				n++;
		return n;
	}

	bool hasDepthBuffer()
	{
		return depthBuffer.texture;
	}

	std::vector<LatteTexture*>& GetTextures()
	{
		return m_referencedTextures;
	}

	virtual ~LatteCachedFBO() {};

private:
	void calculateEffectiveRenderAreaSize()
	{
		Vector2i rtEffectiveSize;
		rtEffectiveSize.x = 0;
		rtEffectiveSize.y = 0;
		sint32 numViews = 0;
		// derive extent from color buffers
		for (sint32 i = 0; i < 8; i++)
		{
			if(colorBuffer[i].texture == nullptr)
				continue;
			sint32 effectiveWidth, effectiveHeight;
			colorBuffer[i].texture->baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, colorBuffer[i].texture->firstMip);
			if (rtEffectiveSize.x == 0 && rtEffectiveSize.y == 0)
			{
				rtEffectiveSize.x = effectiveWidth;
				rtEffectiveSize.y = effectiveHeight;
			}
			if (effectiveWidth < rtEffectiveSize.x)
			{
				cemuLog_logDebug(LogType::Force, "Framebuffer has color texture with smaller effective width ({} -> {})", rtEffectiveSize.x, effectiveWidth);
				rtEffectiveSize.x = effectiveWidth;
			}
			if (effectiveHeight < rtEffectiveSize.y)
			{
				cemuLog_logDebug(LogType::Force, "Framebuffer has color texture with smaller effective height ({} -> {})", rtEffectiveSize.y, effectiveHeight);
				rtEffectiveSize.y = effectiveHeight;
			}
			numViews++;
		}
		// derive extent from depth buffer
		if (depthBuffer.texture)
		{
			sint32 effectiveWidth, effectiveHeight;
			depthBuffer.texture->baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, depthBuffer.texture->firstMip);
			if (rtEffectiveSize.x == 0 && rtEffectiveSize.y == 0)
			{
				rtEffectiveSize.x = effectiveWidth;
				rtEffectiveSize.y = effectiveHeight;
			}
			if (effectiveWidth < rtEffectiveSize.x)
			{
				cemuLog_logDebug(LogType::Force, "Framebuffer has depth texture with smaller effective width ({} -> {})", rtEffectiveSize.x, effectiveWidth);
				rtEffectiveSize.x = effectiveWidth;
			}
			if (effectiveHeight < rtEffectiveSize.y)
			{
				cemuLog_logDebug(LogType::Force, "Framebuffer has depth texture with smaller effective height ({} -> {})", rtEffectiveSize.y, effectiveHeight);
				rtEffectiveSize.y = effectiveHeight;
			}
			numViews++;
		}
		if (numViews == 0)
		{
			// empty FBO
			m_size = Vector2i(32, 32);
			return;
		}
		cemu_assert_debug(rtEffectiveSize.x != 0);
		cemu_assert_debug(rtEffectiveSize.y != 0);
		m_size = rtEffectiveSize;
	}

public:
	uint64 key;
	Vector2i m_size;

	struct
	{
		LatteTextureView* texture{};
	}colorBuffer[8]{};
	struct
	{
		LatteTextureView* texture{};
		bool hasStencil{};
	}depthBuffer{};
	uint32 drawBuffersMask{};

	std::vector<LatteTexture*> m_referencedTextures; // color and depth views combined
};

class LatteMRT
{
public:
	static void NotifyTextureDeletion(LatteTexture* texture);

	// GPU state
	static LatteTextureView* GetColorAttachmentTexture(uint32 index, bool createNew, bool checkForTextureChanges);
	static uint8 GetActiveColorBufferMask(const LatteDecompilerShader* pixelShader, const struct LatteContextRegister& lcr);
	static bool GetActiveDepthBufferMask(const struct LatteContextRegister& lcr);
	static Latte::E_GX2SURFFMT GetColorBufferFormat(const uint32 index, const LatteContextRegister& lcr);
	static Latte::E_GX2SURFFMT GetDepthBufferFormat(const struct LatteContextRegister& lcr);

	// FBO state management
	static void SetColorAttachment(uint32 index, LatteTextureView* view);
	static void SetDepthAndStencilAttachment(LatteTextureView* view, bool hasStencil);
	static LatteTextureView* GetColorAttachment(uint32 index);
	static LatteTextureView* GetDepthAttachment();

	static void ApplyCurrentState();

	static bool UpdateCurrentFBO(); // update FBO with info from current GPU state

	// helper functions
	static void BindColorBufferOnly(LatteTextureView* view);
	static void BindDepthBufferOnly(LatteTextureView* view);

	static void GetCurrentFragCoordScale(float* coordScale);
	// Faro TAA: not really MRT-specific, colocated here purely to match
	// GetCurrentFragCoordScale's existing "per-draw uniform value getter" home.
	// See LatteRenderTarget.cpp's LatteRenderTarget_itHLESwapScanBuffer for where
	// the underlying Halton sequence actually advances (once per real frame).
	static void GetCurrentTaaJitter(float* jitter);
	static void GetVirtualViewportDimensions(sint32& width, sint32& height); // returns the width and height of the current GPU viewport (unaffected by graphic pack rules)

	// Faro Global Enhancements: Renderer::CaptureDepthForPostFX (called once
	// per frame, at backbuffer-copy time) originally read GetDepthAttachment()
	// directly - but by the time the scene reaches the final backbuffer copy,
	// the "currently bound" depth attachment is whatever was LAST bound (often
	// null, since UI/HUD compositing draws typically don't use a depth
	// buffer), essentially never the main 3D scene's own depth. Confirmed via
	// testing: zero "Faro depth capture: valid depth" log lines ever appeared
	// with the feature enabled. Fixed the same way as an earlier, since-removed
	// SSAO experiment in this codebase: remember every DISTINCT depth view
	// ever bound (persistent across frames, pruned only via
	// NotifyTextureDeletion when the texture is actually evicted), and at
	// capture time hand back whichever one matches the final output
	// resolution exactly (a shadow map or UI depth buffer essentially never
	// matches the main scene's render resolution).
	//
	// The selection itself is modeled on ReShade's built-in "generic depth"
	// add-on, which solves this exact problem - see the implementation's own
	// comment in LatteRenderTarget.cpp for what that buys over picking by
	// binding order alone.
	static void NotifyDepthAttachmentUsed(LatteTextureView* view);
	// Attributes one draw call's geometry to whichever depth view is
	// currently bound. Called from the renderer-agnostic draw dispatch (see
	// LatteCommandProcessor), so the stats are identical on Vulkan, OpenGL
	// and Metal.
	// A clear wipes the buffer, so the snapshot rule's per-segment workload
	// starts over - see the implementation.
	static void NotifyDepthBufferCleared(LatteTexture* texture);
	static void NotifyDepthDrawcall(uint32 vertexCount, uint32 instanceCount);
	// Rolls this frame's per-depth-buffer stats into "last frame" and starts
	// fresh. Called once per presented frame.
	static void AdvanceDepthStatsFrame();
	// `currentlySelected` enables hysteresis: the buffer already in use wins
	// ties and only loses to a clearly better candidate. Pass nullptr to get
	// a plain "best right now" answer.
	static LatteTextureView* FindSceneDepthViewForSize(sint32 width, sint32 height, LatteTextureView* currentlySelected = nullptr);
	// Last completed frame's recorded workload for a tracked view - for
	// logging which buffer got picked and why.
	static void GetSceneDepthViewStats(LatteTextureView* view, uint32& vertices, uint32& drawcalls);
	// Dumps every tracked depth buffer with its size and workload. Called
	// periodically, not per frame - see its own comment for what question it
	// is meant to answer.
	static void LogDepthCandidates(sint32 outputWidth, sint32 outputHeight, LatteTextureView* selected);
	// Faro Global Enhancements: lets Renderer::CaptureDepthForPostFX safely
	// extend a stale-but-still-alive depth view's validity across a brief
	// miss streak (see that method's own comment) without ever risking a
	// dangling pointer - returns false once the view has actually been
	// pruned (NotifyTextureDeletion), true otherwise, including in frames
	// where it simply didn't win FindSceneDepthViewForSize's MRU race.
	static bool IsDepthViewStillTracked(LatteTextureView* view);

	// todo - move this into FBO destructor (?)
	static void DeleteCachedFBO(LatteCachedFBO* cfbo);

private:
	static LatteCachedFBO* CreateCachedFBO(uint64 key);
};
