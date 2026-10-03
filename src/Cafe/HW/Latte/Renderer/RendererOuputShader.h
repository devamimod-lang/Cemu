#pragma once

#include "Cafe/HW/Latte/Renderer/RendererShader.h"
#include "util/math/vector2.h"

#include "Cafe/HW/Latte/Core/LatteTexture.h"

class RendererOutputShader
{
public:
	struct OutputUniformVariables
	{
		Vector2f textureSrcResolution;
		Vector2f nativeResolution;
		Vector2f outputResolution;
		uint32 applySRGBEncoding;
		float targetGamma;
		float displayGamma;
		// Faro TAA: only read by s_taa_resolve_shader_source - every other output
		// shader ignores it (left at its zero-init default by
		// FillUniformBlockBuffer; the TAA dispatch method overrides it
		// afterward, same pattern as the applySRGBEncoding overrides already
		// used for the FSR1/SMAA intermediate passes). The jitter itself is
		// already baked into textureSrc's pixels (applied in the vertex shader
		// that rendered the game's own frame - see SET_POSITION), so the
		// resolve pass only needs to know whether history is usable yet.
		uint32 taaHistoryValid;
		// Faro GTAO: std140 pads us to offset 48 here (vec4 needs 16-align),
		// keep the explicit pad so the C++ layout matches the GLSL block.
		// x = intensity 0-1 (from faro_ssao_intensity), y = radius in view
		// units, z = quality preset, w = depth linearization far plane.
		// Zero-initialized by FillUniformBlockBuffer; the GTAO dispatch
		// overrides it afterward (same pattern as taaHistoryValid above).
		uint32 _padFaroGtao[2];
		float faroGtaoParams[4];
	};
	enum Shader
	{
		kCopy,
		kBicubic,
		kHermit,
		kFsr1,
		kFxaa,
		kSmaaEdge,
		kSmaaBlend,
		kSmaaNeighborhood,
	};
	RendererOutputShader(const std::string& vertex_source, const std::string& fragment_source);
	virtual ~RendererOutputShader() = default;

	OutputUniformVariables FillUniformBlockBuffer(const LatteTextureView& texture_view, const Vector2i& output_res, const bool padView) const;

	RendererShader* GetVertexShader() const
	{
		return m_vertex_shader.get();
	}

	RendererShader* GetFragmentShader() const
	{
		return m_fragment_shader.get();
	}

	static void InitializeStatic();
	static void ShutdownStatic();

	static RendererOutputShader* s_copy_shader;
	static RendererOutputShader* s_copy_shader_ud;

	static RendererOutputShader* s_bicubic_shader;
	static RendererOutputShader* s_bicubic_shader_ud;

	static RendererOutputShader* s_hermit_shader;
	static RendererOutputShader* s_hermit_shader_ud;

	// Not available under Metal - AMD's FSR1 reference has no Metal/MSL port and
	// Cemu's Metal output-shader path skips the shared GLSL preamble entirely
	// (see the constructor), so it would need its own from-scratch uniform buffer
	// wiring. Left unimplemented since this build only targets Windows.
	//
	// FSR1 is genuinely 2 passes (EASU upscale, then RCAS sharpen) - RCAS needs
	// to read already-upscaled neighbour pixels that don't exist yet during
	// EASU, so (unlike the other single-pass shaders here) it can't be fused
	// into one pass. Render-upside-down (see RenderUpsideDownEnabled) is
	// applied in EASU - the FIRST pass that draws a full-screen quad - same as
	// the old fused shader used to; every later pass (RCAS, and then FXAA/SMAA
	// on top) just re-samples that already-correctly-oriented intermediate
	// without flipping again, which is also why s_fxaa_shader/s_smaa_*_shader
	// below have no upside-down variant of their own.
	static RendererOutputShader* s_fsr1_easu_shader;
	static RendererOutputShader* s_fsr1_easu_shader_ud;
	static RendererOutputShader* s_fsr1_rcas_shader;

	// Real FXAA needs to read neighboring pixels of the ALREADY-upscaled image at
	// varying, dynamically-decided distances (its edge search walks outward) -
	// unlike FSR1 above, this genuinely can't be fused into DrawBackbufferQuad's
	// single pass over the pre-upscale source texture. Only ever bound and drawn
	// via Renderer::DrawBackbufferQuadTwoPass (Vulkan only, see that method's own
	// doc comment) - never through the plain single-shader dispatch in
	// LatteRenderTarget.cpp the other shaders here go through.
	static RendererOutputShader* s_fxaa_shader;

	// SMAA (Subpixel Morphological Antialiasing) - real 3-pass port of the
	// public iryoku/smaa reference (luma edge detection, diagonal search and
	// corner detection). Like FXAA above, only ever driven via a dedicated
	// multi-pass Renderer method (Vulkan only), never through the
	// single-shader dispatch in LatteRenderTarget.cpp.
	// s_smaa_edge_shader reads the FSR1-output intermediate texture and writes
	// an edges mask; s_smaa_blend_shader reads that mask plus the two static
	// area/search lookup textures (see SMAALookupTextures.h) and writes blend
	// weights; s_smaa_neighborhood_shader reads the FSR1-output texture again
	// plus the blend weights and writes the final antialiased image (this one
	// doesn't vary per quality preset, hence the single pointer).
	//
	// s_smaa_edge_shader/s_smaa_blend_shader are indexed by
	// CemuConfig::SmaaQuality (kSmaaLow..kSmaaUltra) - all 4 presets' shaders
	// are compiled upfront in InitializeStatic so the player can switch
	// smaa_quality without needing a shader recompile. See
	// BuildSmaaEdgeShaderSource/BuildSmaaBlendShaderSource for the exact
	// per-preset constants (matches the public reference's own
	// SMAA_PRESET_LOW/MEDIUM/HIGH/ULTRA definitions exactly).
	static constexpr int kSmaaQualityCount = 4;
	static RendererOutputShader* s_smaa_edge_shader[kSmaaQualityCount];
	static RendererOutputShader* s_smaa_blend_shader[kSmaaQualityCount];
	static RendererOutputShader* s_smaa_neighborhood_shader;

	// Faro TAA: single resolve pass, reads textureSrc (this frame's already
	// upscaled+sharpened color, rendered with sub-pixel jitter baked into the
	// game's own vertex shaders) and textureSrc2 (previous frame's resolved
	// output - see VulkanRenderer::EnsureTaaHistoryTarget) and writes the
	// blended result to the real backbuffer. Only ever driven via
	// Renderer::DrawBackbufferQuadFsr1Taa (Vulkan only), never through the
	// single-shader dispatch in LatteRenderTarget.cpp.
	static RendererOutputShader* s_taa_resolve_shader;

	// Faro GTAO: horizon-based ambient occlusion (Jimenez et al. 2016
	// technique family, clean-room GLSL core - not a copy of any vendor
	// sample). Reads textureSrc (upscaled color) + textureDepth (native scene
	// depth, same UVs) and writes AO-multiplied color. Runs between RCAS and
	// the AA stage of the FSR1-based chains (TwoPass/Fsr1Smaa/Fsr1Taa),
	// ping-ponging the existing easu/fxaa intermediates so no new color
	// target is needed. 3 quality presets (sample counts), compiled upfront
	// like the SMAA presets so switching CemuConfig::faro_ssao_quality never
	// recompiles. Vulkan-only dispatch (same house precedent as the AA
	// multi-pass paths); other backends ignore it.
	static constexpr int kGtaoQualityCount = 3;
	static RendererOutputShader* s_gtao_shader[kGtaoQualityCount];

	// Builds the GTAO source for one quality preset by prepending the
	// preset-specific #define block to the shared algorithm body.
	static std::string BuildGtaoShaderSource(sint32 gtaoQuality);

	// Faro GI: reuses GTAO's exact same directional/step depth-sampling
	// kernel (see BuildGtaoShaderSource's own doc comment - same NaN-safe
	// depth clamping, relative depth threshold, distance fade lessons all
	// apply identically here), but instead of darkening on occlusion,
	// accumulates a weighted average of nearby SCREEN-SPACE color from
	// samples that are closer to the camera than the center pixel (same
	// directionality as GTAO's own occ term - a nearer surface could
	// plausibly bounce light onto this one) and adds it as a diffuse bounce
	// term. No G-buffer normals available (see FSR2_TAA_investigacion.md on
	// why), so this is a crude screen-space color-bleed approximation, not
	// physically based GI - single fixed sample count, no quality tiers.
	// DISABLED (see wantGi in VulkanRenderer.cpp): implicated in a hang/
	// crash under a downscaling+TAA combo - kept implemented but
	// hard-gated off rather than removed, since removing it once already
	// caused its own build breakage (an unrelated function got deleted by
	// accident along with it).
	static RendererOutputShader* s_gi_shader;

	static std::string BuildGiShaderSource();

	// Faro GI denoise: the directional/step kernel's per-pixel random
	// rotation (see s_gi_shader_body's own comment) is noisy by design -
	// same reason every real-time SSAO/SSGI implementation (XeGTAO
	// included) requires a denoise pass afterward, confirmed via testing:
	// the raw result showed clearly visible directional streaking rather
	// than a smooth bounce. Depth-aware (bilateral) box blur over
	// textureSrc=GI's own raw output, so it smooths within a surface
	// without bleeding across true depth discontinuities.
	static RendererOutputShader* s_gi_blur_shader;

	static std::string BuildGiBlurShaderSource();

	// Faro SSR: screen-space reflections. Reconstructs a depth-derived
	// normal and assumed-FOV pseudo-view-space positions (McGuire & Mara's
	// "Efficient GPU Screen-Space Ray Tracing" and Godot's own SSR shader
	// both fundamentally need a real camera projection matrix + G-buffer
	// normals, neither available here). Marches along the reflected ray in
	// pseudo-view-space, projecting each step back to screen UV via the
	// SAME assumed FOV, and tests against the real depth buffer - a
	// simplified, single-hit-test substitute for Godot's proper DDA/
	// perspective-correct screen-space traversal. No roughness/metallic
	// buffer available, so v1 has no cone-tracing blur - sharp mirror-like
	// reflections only, single fixed sample count, no quality tiers.
	static RendererOutputShader* s_ssr_shader;

	static std::string BuildSsrShaderSource();

	// Faro SSR: temporal smoothing for the reflective-surface gate consumed
	// by s_ssr_shader above (see its own comment on why the raw per-frame
	// curvature test pops on animated/grazing-angle surfaces). Computes the
	// SAME depth-derived curvature/flatness test, then blends it against
	// last frame's own blended result at the same screen UV - no motion
	// vectors/reprojection, same accepted simplification as Faro TAA's own
	// history blend (see s_taa_resolve_shader_body's doc comment). Runs as
	// its own tiny pass into a Faro-owned scratch target (see
	// VulkanRenderer::RunFaroSsrPass/EnsureSsrFlatnessScratchTarget) BEFORE
	// the main SSR pass, which then just samples the result instead of
	// recomputing curvature raw.
	static RendererOutputShader* s_ssr_flatness_shader;

	static std::string BuildSsrFlatnessShaderSource();

	static std::string GetOpenGlVertexSource(bool render_upside_down);
	static std::string GetVulkanVertexSource(bool render_upside_down);
	static std::string GetMetalVertexSource(bool render_upside_down);

	static std::string PrependFragmentPreamble(const std::string& shaderSrc);

	// Builds the full edge/blend-calc shader source for one of the 4 SMAA
	// quality presets (CemuConfig::SmaaQuality) by prepending the
	// preset-specific #define block to the shared, quality-independent
	// algorithm body.
	static std::string BuildSmaaEdgeShaderSource(sint32 smaaQuality);
	static std::string BuildSmaaBlendShaderSource(sint32 smaaQuality);

protected:
	std::unique_ptr<RendererShader> m_vertex_shader;
	std::unique_ptr<RendererShader> m_fragment_shader;


private:
	static const std::string s_copy_shader_source;
	static const std::string s_bicubic_shader_source;
	static const std::string s_hermite_shader_source;
	static const std::string s_fsr1_easu_shader_source;
	static const std::string s_fsr1_rcas_shader_source;
	static const std::string s_fxaa_shader_source;
	// Quality-independent bodies - see BuildSmaaEdgeShaderSource/
	// BuildSmaaBlendShaderSource, which prepend the preset-specific #define
	// block for each of the 4 SMAA quality presets.
	static const std::string s_smaa_edge_shader_body;
	static const std::string s_smaa_blend_shader_body;
	static const std::string s_smaa_neighborhood_shader_source;
	static const std::string s_taa_resolve_shader_source;

	static const std::string s_bicubic_shader_source_vk;
	static const std::string s_hermite_shader_source_vk;

	static const std::string s_copy_shader_source_mtl;
	static const std::string s_bicubic_shader_source_mtl;
	static const std::string s_hermite_shader_source_mtl;
};
