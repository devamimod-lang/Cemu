#pragma once

#include "ConfigValue.h"
#include "XMLConfig.h"

enum class NetworkService;

struct GameEntry
{
	GameEntry() = default;

	std::wstring rpx_file;
	std::wstring legacy_name;
	std::string product_code;
	std::string company_code;

	std::string custom_name;

	sint32 icon = -1;
	sint32 icon_small = -1;
	sint32 legacy_version = 0;
	sint32 legacy_update_version = -1;
	sint32 legacy_dlc_version = -1;
	uint64 title_id = 0;
	uint32 legacy_region = 0;

	std::wstring save_folder;
	std::wstring update_folder;
	std::wstring dlc_folder;

	uint64 legacy_time_played = 0;
	uint64 legacy_last_played = 0;

	bool favorite = false;

	bool is_update = false;
};

struct GraphicPackEntry
{
	GraphicPackEntry(std::wstring_view filename,std::string_view category, std::string_view preset)
		: filename(filename)
	{
		presets[std::string{category}] = preset;
	}

	GraphicPackEntry(std::wstring_view filename, std::string_view preset)
		: filename(filename)
	{
		presets[""] = preset;
	}

	GraphicPackEntry(std::wstring filename)
		: filename(std::move(filename))
	{}

	struct Preset
	{
		std::string name;
		std::string category;
	};
	std::unordered_map<std::string, std::string> presets; // category, active_preset

	std::wstring filename;
	bool enabled = true;
};

enum GraphicAPI
{
	kOpenGL = 0,
	kVulkan,
	kMetal,
	COUNT
};

#if defined(ENABLE_VULKAN)
constexpr GraphicAPI kDefaultGraphicsAPI = kVulkan;
#elif defined(ENABLE_METAL)
constexpr GraphicAPI kDefaultGraphicsAPI = kMetal;
#elif defined(ENABLE_OPENGL)
constexpr GraphicAPI kDefaultGraphicsAPI = kOpenGL;
#endif

enum AudioChannels
{
	kMono = 0,
	kStereo,
	kSurround,
};

enum UpscalingFilter
{
	kLinearFilter,
	kBicubicFilter,
	kBicubicHermiteFilter,
	kNearestNeighborFilter,
	kFsr1Filter,
	// Anubis: ratio-aware Lanczos2. Only offered as a DOWNSCALE option, which
	// is the case the other filters get wrong - they all read a fixed number
	// of source texels, so above a ratio of about 2 they skip most of the
	// rendered image. See s_lanczos_shader_source.
	//
	// Note this is enum value 5 while it sits at index 4 in the downscale
	// radio box, because kFsr1Filter (4) is never offered for downscaling.
	// GeneralSettings2 converts between the two explicitly rather than
	// relying on index == value.
	kLanczosFilter,
};

enum FullscreenScaling
{
	kKeepAspectRatio,
	kStretch,
};

enum class ScreenPosition
{
	kDisabled = 0,
	kTopLeft,
	kTopCenter,
	kTopRight,
	kBottomLeft,
	kBottomCenter,
	kBottomRight,
};

enum class PrecompiledShaderOption
{
	Auto,
	Enable,
	Disable,
};
ENABLE_ENUM_ITERATORS(PrecompiledShaderOption, PrecompiledShaderOption::Auto, PrecompiledShaderOption::Disable);

enum class AccurateShaderMulOption
{
	False = 0, // always use standard multiplication
	True = 1 // fully emulate non-ieee MUL special cases (0*anything = 0)
};
ENABLE_ENUM_ITERATORS(AccurateShaderMulOption, AccurateShaderMulOption::False, AccurateShaderMulOption::True);

enum class MetalBufferCacheMode
{
    Auto,
    DevicePrivate,
    DeviceShared,
    Host,
};
ENABLE_ENUM_ITERATORS(MetalBufferCacheMode, MetalBufferCacheMode::Auto, MetalBufferCacheMode::Host);

enum class PositionInvariance
{
    Auto,
    False,
    True,
};
ENABLE_ENUM_ITERATORS(PositionInvariance, PositionInvariance::False, PositionInvariance::True);

enum class CPUMode
{
	SinglecoreInterpreter = 0,
	SinglecoreRecompiler = 1,
	DualcoreRecompiler = 2, // deprecated and not used anymore
	MulticoreRecompiler = 3,
	Auto = 4,
};
ENABLE_ENUM_ITERATORS(CPUMode, CPUMode::SinglecoreInterpreter, CPUMode::Auto);


enum class CPUModeLegacy
{
	SinglecoreInterpreter = 0,
	SinglecoreRecompiler = 1,
	DualcoreRecompiler = 2,
	TriplecoreRecompiler = 3,
	Auto = 4
};
ENABLE_ENUM_ITERATORS(CPUModeLegacy, CPUModeLegacy::SinglecoreInterpreter, CPUModeLegacy::Auto);

enum class CafeConsoleRegion
{
	JPN = 0x1,
	USA = 0x2,
	EUR = 0x4,
	AUS_DEPR = 0x8,
	CHN = 0x10,
	KOR = 0x20,
	TWN = 0x40,
	Auto = 0xFF,
};
ENABLE_BITMASK_OPERATORS(CafeConsoleRegion);

enum class CafeConsoleLanguage
{
	JA = 0,
	EN = 1,
	FR = 2,
	DE = 3,
	IT = 4,
	ES = 5,
	ZH = 6,
	KO = 7,
	NL = 8,
	PT = 9,
	RU = 10,
	TW = 11,
};
ENABLE_ENUM_ITERATORS(CafeConsoleLanguage, CafeConsoleLanguage::JA, CafeConsoleLanguage::TW);

#if BOOST_OS_WINDOWS
enum class CrashDump
{
	Disabled,
	Lite,
	Full
};
ENABLE_ENUM_ITERATORS(CrashDump, CrashDump::Disabled, CrashDump::Full);
#elif BOOST_OS_UNIX
enum class CrashDump
{
	Disabled,
	Enabled
};
ENABLE_ENUM_ITERATORS(CrashDump, CrashDump::Disabled, CrashDump::Enabled);
#endif

template <>
struct fmt::formatter<PrecompiledShaderOption> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const PrecompiledShaderOption c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case PrecompiledShaderOption::Auto: name = "auto"; break;
		case PrecompiledShaderOption::Enable: name = "true"; break;
		case PrecompiledShaderOption::Disable: name = "false"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<AccurateShaderMulOption> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const AccurateShaderMulOption c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case AccurateShaderMulOption::True: name = "true"; break;
		case AccurateShaderMulOption::False: name = "false"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<MetalBufferCacheMode> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const MetalBufferCacheMode c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case MetalBufferCacheMode::Auto: name = "auto"; break;
		case MetalBufferCacheMode::DevicePrivate: name = "device private"; break;
		case MetalBufferCacheMode::DeviceShared: name = "device shared"; break;
		case MetalBufferCacheMode::Host: name = "host"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<PositionInvariance> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const PositionInvariance c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case PositionInvariance::Auto: name = "auto"; break;
		case PositionInvariance::False: name = "false"; break;
		case PositionInvariance::True: name = "true"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CPUMode> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CPUMode c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case CPUMode::SinglecoreInterpreter: name = "Single-core interpreter"; break;
		case CPUMode::SinglecoreRecompiler: name = "Single-core recompiler"; break;
		case CPUMode::DualcoreRecompiler: name = "Dual-core recompiler"; break;
		case CPUMode::MulticoreRecompiler: name = "Multi-core recompiler"; break;
		case CPUMode::Auto: name = "Auto"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CPUModeLegacy> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CPUModeLegacy c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case CPUModeLegacy::SinglecoreInterpreter: name = "Singlecore-Interpreter"; break;
		case CPUModeLegacy::SinglecoreRecompiler: name = "Singlecore-Recompiler"; break;
		case CPUModeLegacy::DualcoreRecompiler: name = "Dualcore-Recompiler"; break;
		case CPUModeLegacy::TriplecoreRecompiler: name = "Triplecore-Recompiler"; break;
		case CPUModeLegacy::Auto: name = "Auto"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CafeConsoleRegion> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CafeConsoleRegion v, FormatContext &ctx) const {
		string_view name;
		switch (v)
		{
		case CafeConsoleRegion::JPN: name = TR_NOOP("Japan"); break;
		case CafeConsoleRegion::USA: name = TR_NOOP("USA"); break;
		case CafeConsoleRegion::EUR: name = TR_NOOP("Europe"); break;
		case CafeConsoleRegion::AUS_DEPR: name = TR_NOOP("Australia"); break;
		case CafeConsoleRegion::CHN: name = TR_NOOP("China"); break;
		case CafeConsoleRegion::KOR: name = TR_NOOP("Korea"); break;
		case CafeConsoleRegion::TWN: name = TR_NOOP("Taiwan"); break;
		case CafeConsoleRegion::Auto: name = TR_NOOP("Auto"); break;
		default: name = TR_NOOP("many"); break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CafeConsoleLanguage> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CafeConsoleLanguage v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CafeConsoleLanguage::JA: name = "Japanese"; break;
		case CafeConsoleLanguage::EN: name = "English"; break;
		case CafeConsoleLanguage::FR: name = "French"; break;
		case CafeConsoleLanguage::DE: name = "German"; break;
		case CafeConsoleLanguage::IT: name = "Italian"; break;
		case CafeConsoleLanguage::ES: name = "Spanish"; break;
		case CafeConsoleLanguage::ZH: name = "Chinese"; break;
		case CafeConsoleLanguage::KO: name = "Korean"; break;
		case CafeConsoleLanguage::NL: name = "Dutch"; break;
		case CafeConsoleLanguage::PT: name = "Portugese"; break;
		case CafeConsoleLanguage::RU: name = "Russian"; break;
		case CafeConsoleLanguage::TW: name = "Taiwanese"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};

#if BOOST_OS_WINDOWS
template <>
struct fmt::formatter<CrashDump> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CrashDump v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CrashDump::Disabled: name = "Disabled"; break;
		case CrashDump::Lite: name = "Lite"; break;
		case CrashDump::Full: name = "Full"; break;
		default: name = "unknown"; break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
#elif BOOST_OS_UNIX
template <>
struct fmt::formatter<CrashDump> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CrashDump v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CrashDump::Disabled: name = "Disabled"; break;
		case CrashDump::Enabled: name = "Enabled"; break;
		default: name = "unknown"; break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
#endif


struct CemuConfig
{
	CemuConfig()
	{

	};

	CemuConfig(const CemuConfig&) = delete;

	// sets mlc path, updates permanent config value, saves config
	void SetMLCPath(fs::path path, bool save = true);

	ConfigValue<uint64> log_flag{ 0 };
	ConfigValue<bool> advanced_ppc_logging{ false };

	ConfigValue<bool> permanent_storage{ true };

	ConfigValue<std::string> mlc_path{};
	ConfigValue<std::string> proxy_server{};

	// temporary workaround because feature crashes on macOS
#if BOOST_OS_MACOS
#define DISABLE_SCREENSAVER_DEFAULT false
#else
#define DISABLE_SCREENSAVER_DEFAULT true
#endif
	ConfigValue<bool> disable_screensaver{DISABLE_SCREENSAVER_DEFAULT};
#undef DISABLE_SCREENSAVER_DEFAULT
	ConfigValue<bool> play_boot_sound{false};

	std::vector<std::string> game_paths;
	std::mutex game_cache_entries_mutex;
	std::vector<GameEntry> game_cache_entries;

	// optimized access
	std::set<uint64> game_cache_favorites; // per titleId

	struct _path_hash {
		std::size_t operator()(const fs::path& path) const {
			return fs::hash_value(path);
		}
	};
	// <filename, <category, preset>>
	std::unordered_map<fs::path, std::unordered_map<std::string, std::string>, _path_hash> graphic_pack_entries;

	ConfigValueBounds<CPUMode> cpu_mode{ CPUMode::Auto };
	ConfigValueBounds<CafeConsoleLanguage> console_language{ CafeConsoleLanguage::EN };

	// graphics
	ConfigValue<GraphicAPI> graphic_api{ kDefaultGraphicsAPI };
	std::array<uint8, 16> legacy_graphic_device_uuid{}; // placeholder option for backwards compatibility with settings from 2.6 and before (renamed to "vkDevice")
	std::array<uint8, 16> vk_graphic_device_uuid;
	uint64 mtl_graphic_device_uuid{ 0 };
	ConfigValue<int> vsync{ 0 }; // 0 = off, 1+ = depending on render backend
	ConfigValue<bool> gx2drawdone_sync { true };
	ConfigValue<bool> render_upside_down{ false };
	ConfigValue<bool> async_compile{ true };
#ifdef ENABLE_METAL
	ConfigValue<bool> force_mesh_shaders{ false };
#endif

	// Gamma
	ConfigValue<bool> overrideAppGammaPreference{ false };
	ConfigValue<float> overrideGammaValue{ 2.2f };
	ConfigValue<float> userDisplayGamma { 2.2f }; // 0 = sRGB, >0 gamma

	ConfigValue<bool> vk_accurate_barriers{ true };

	struct
	{
		ScreenPosition position = ScreenPosition::kDisabled;
		uint32 text_color = 0xFFFFFFFF;
		sint32 text_scale = 100;
		bool fps = true;
		bool drawcalls = false;
		bool cpu_usage = false;
		bool cpu_per_core_usage = false;
		bool ram_usage = false;
		bool vram_usage = false;
		bool debug = false;
	} overlay{};

	struct
	{
		ScreenPosition position = ScreenPosition::kTopLeft;
		uint32 text_color = 0xFFFFFFFF;
		sint32 text_scale = 100;
		bool controller_profiles = true;
		bool controller_battery = false;
		bool shader_compiling = true;
		bool friends = true;
	} notification{};

	ConfigValue<sint32> upscale_filter{kBicubicFilter};
	ConfigValue<sint32> downscale_filter{kLinearFilter};
	ConfigValue<sint32> fullscreen_scaling{kKeepAspectRatio};
	// Both FXAA and SMAA need one or more extra passes over the already-
	// upscaled image (see Renderer::DrawBackbufferQuadTwoPass/
	// DrawBackbufferQuadFsr1Smaa), so they're only offered as an add-on to the
	// FSR 1.0 upscale filter rather than standalone filters of their own.
	// Vulkan only (LatteRenderTarget.cpp falls back to FSR1-only if the active
	// renderer doesn't support the multi-pass path). SMAA (morphological, 3
	// passes: edge detection -> blending weights -> neighborhood blend) gives
	// better preservation of thin diagonal geometry (fences, power lines) than
	// FXAA's single contrast-based pass, at a real extra GPU cost.
	// Faro TAA: the sub-pixel camera jitter that a "real" TAA would use is
	// disabled (see LatteMRT::GetCurrentTaaJitter's own comment - the resolve
	// pass has no reprojection, so jittering the camera without undoing that
	// jitter afterward just shimmers). What kAATaa actually does today is a
	// motion-adaptive temporal blend (ghost-driven AABB clip in YCoCg, see
	// s_taa_resolve_shader_source) fed by a spatial pre-pass (FXAA or SMAA,
	// see taa_spatial_aa below) - available at any upscale/downscale filter,
	// same as FXAA/SMAA on their own (see DrawBackbufferQuadFxaa/Smaa/Taa vs
	// the *Fsr1* variants in LatteRenderTarget.cpp).
	enum AntialiasingMode
	{
		kAANone = 0,
		kAAFxaa = 1,
		kAASmaa = 2,
		kAATaa = 3,
	};
	ConfigValue<sint32> antialiasing_mode{kAANone};

	// Only meaningful when antialiasing_mode == kAASmaa - selects which of the
	// 4 static quality presets from the public SMAA reference
	// (https://github.com/iryoku/smaa) to use. Higher presets search further
	// for edges and enable diagonal/corner detection, at a real extra GPU
	// cost - see RendererOuputShader.cpp's BuildSmaaEdgeShaderSource/
	// BuildSmaaBlendShaderSource for the exact per-preset constants, and
	// VulkanRenderer::InitializeStatic, which compiles all 4 presets' shaders
	// upfront so switching this doesn't need a shader recompile.
	enum SmaaQuality
	{
		kSmaaLow = 0,
		kSmaaMedium = 1,
		kSmaaHigh = 2,
		kSmaaUltra = 3,
	};
	ConfigValue<sint32> smaa_quality{kSmaaHigh};

	// Faro TAA: which spatial filter runs as TAA's own pre-pass, before the
	// temporal resolve (see antialiasing_mode's own comment). Only meaningful
	// when antialiasing_mode == kAATaa. FXAA is cheaper but SMAA's
	// morphological search preserves thin diagonal geometry (fences, power
	// lines) better - see DrawBackbufferQuadTaa/DrawBackbufferQuadFsr1Taa,
	// which read this to pick which pass(es) to run.
	enum TaaSpatialAA
	{
		kTaaSpatialFxaa = 0,
		kTaaSpatialSmaa = 1,
	};
	ConfigValue<sint32> taa_spatial_aa{kTaaSpatialFxaa};

	// Faro FSR1: RCAS sharpness, in AMD's own "stops" scale from
	// FsrRcasCon (ffx_fsr1.h): 0.0 = maximum sharpness, and each whole step
	// up halves it (sharpness is applied as exp2(-stops)). Stored in
	// hundredths of a stop so it can live in an integer ConfigValue and still
	// be adjusted finely - the useful range is roughly 0.0 to 2.0 stops.
	// Previously a hard-coded 0.2 inside the shader with no way to change it,
	// which is not a sensible single answer: how sharp FSR1 should be depends
	// on the upscale ratio (more aggressive upscales need more sharpening to
	// stop looking soft) and on the display, so this is exactly the kind of
	// value that has to be adjustable per machine rather than chosen once.
	static constexpr sint32 kFsrRcasSharpnessMin = 0;   // 0.00 stops - sharpest
	static constexpr sint32 kFsrRcasSharpnessMax = 200; // 2.00 stops - softest
	ConfigValue<sint32> fsr_rcas_sharpness{20};         // 0.20 stops, the previous hard-coded value

	// Faro FSR1: enable RCAS's own optional noise-detection path
	// (FSR_RCAS_DENOISE in ffx_fsr1.h). It computes a normalized highpass of
	// the local neighbourhood and scales the sharpening lobe by it, so
	// sharpening concentrates on real edges instead of amplifying film grain
	// and dithering. AMD leaves it off by default and suggests applying grain
	// after RCAS instead - but an emulator cannot reorder a game's own
	// post-processing, and Wii U titles frequently ship dithered/grainy
	// output, so being able to turn it on matters here in a way it does not
	// in a native integration. Costs a handful of extra ALU ops in the RCAS
	// pass.
	ConfigValue<bool> fsr_rcas_denoise{false};

	// Faro: let GTAO/SSR read a private copy of the scene depth instead of the
	// game's live buffer (see Renderer::CaptureSceneDepthSnapshot). Off by
	// default because the copy is recorded mid-frame from the framebuffer-bind
	// path; ReShade ships its own equivalent (preserve_depth_buffers) off by
	// default for the same kind of reason.
	ConfigValue<bool> faro_depth_snapshot{false};
	// Scene colour snapshot. Same opt-in reasoning as the depth one: the copy
	// is recorded mid-frame from the framebuffer-bind path, the most intrusive
	// place this renderer touches.
	ConfigValue<bool> faro_colour_snapshot{false};

	// Faro: output an intermediate buffer of the AO chain instead of the
	// shaded image, to localise instability by eye. CPU counters can prove a
	// pass ran and that the depth selection held; they cannot show whether the
	// depth DATA is steady, and that is the gap this closes.
	enum FaroDebugView
	{
		kFaroDebugOff = 0,
		kFaroDebugRawDepth = 1,
		kFaroDebugRawAo = 2,
		kFaroDebugConfidence = 3,
		kFaroDebugMarchRadius = 4,
		kFaroDebugDenoisedAo = 5,
		// Fraction of RCAS's maximum sharpening lobe actually in play. Black
		// means RCAS found no local contrast to sharpen, so the sharpness
		// slider cannot do anything no matter where it is set.
		kFaroDebugRcasLobe = 6,
	};
	ConfigValue<sint32> faro_debug_view{kFaroDebugOff};

	// Faro: force anisotropic filtering globally, overriding whatever level the
	// game's own sampler requested. Only applied when a graphic pack hasn't
	// already set its own per-texture override (LatteTexture.h's
	// overwriteInfo.anisotropicLevel always wins over this - see
	// VulkanRendererCore.cpp/OpenGLRendererCore.cpp's sampler creation).
	// Encoded the same way as anisotropicLevel: N means 1<<N (1=2x, 2=4x,
	// 3=8x, 4=16x). Skipped on samplers using NEAREST min/mag filtering
	// (pixel-art textures rendered with point filtering on purpose) - forcing
	// anisotropy there looks wrong, not better (same lesson Dolphin learned
	// the hard way, see dolphin-emu@83d3055).
	enum ForceAnisotropicLevel
	{
		kForceAnisoOff = 0,
		kForceAniso2x = 1,
		kForceAniso4x = 2,
		kForceAniso8x = 3,
		kForceAniso16x = 4,
	};
	ConfigValue<sint32> force_anisotropic_level{kForceAnisoOff};

	// Faro Global Enhancements: master switch plus per-effect toggles that
	// apply to every game (no per-title graphic pack needed). Render code
	// reads these at output-chain build time; all default OFF so existing
	// behavior is unchanged. Intensities are 0-100 sliders in the UI.
	// SSAO quality selects the XeGTAO pass preset (0=Low, 1=Medium, 2=High).
	// FaroShadowScale is reserved for Phase 3 (global shadowmap multiplier
	// 1-4x); the render path ignores it until then.
	ConfigValue<bool> faro_postfx_enabled{false};
	ConfigValue<bool> faro_ssao_enabled{false};
	ConfigValue<sint32> faro_ssao_intensity{60};
	ConfigValue<sint32> faro_ssao_quality{1};
	// GI disabled: kept implemented but force-gated off in code (see
	// wantGi in VulkanRenderer.cpp) after it was implicated in a hang/
	// crash under a downscaling+TAA combo - reverted from a full code
	// removal (which itself introduced a build breakage) back to just
	// gating it off, per user request.
	ConfigValue<bool> faro_gi_enabled{false};
	ConfigValue<sint32> faro_gi_intensity{50};

	// Anubis: load higher-resolution replacement textures from disk - see
	// LatteTextureReplacement.h for the workflow and the key it matches on.
	// Off by default: it reads files on the texture load path, and a pack built
	// from a dump of a different run can miss entirely.
	ConfigValue<bool> texture_replacement_enabled{false};

	// Anubis: real-time texture upscaling - 1 (off), 2, 3 or 4.
	// See LatteTextureScaling.h for what this can and cannot deliver.
	ConfigValue<sint32> texture_scaling_factor{1};
	// Largest scaled dimension allowed, in texels. 0 = no limit. Exists so one
	// build suits an 8 GB card and a 24 GB one: scaling multiplies texture VRAM
	// by the factor squared, so the sensible ceiling depends on the GPU, not on
	// the developer's machine.
	ConfigValue<sint32> texture_scaling_max_size{2048};
	// Anubis: VRAM ceiling for upscaled texture copies, in MB.
	//
	// A copy is RGBA8 because the GPU cannot render straight to a
	// block-compressed format, so a 1024x1024 BC1 at 2x becomes 16 MB against
	// the original 512 KB. Without a ceiling this exhausts any card; with one,
	// the textures actually in use get upscaled and the rest keep their
	// original resolution. The right value depends on the GPU, not on the
	// developer's machine, which is why it is a setting.
	// 0 = Auto: derived from the device-local VRAM at runtime. A hand-picked
	// default was wrong - at 4x, 1024 MB held about sixteen large textures and
	// refused thousands. What fits depends on the card, so the card decides.
	ConfigValue<sint32> texture_scaling_vram_budget_mb{0};
	// Anime4K Restore CNN before upscaling. Four convolution passes at the
	// texture's native resolution, which is what the network was trained for
	// and far cheaper than running it on the enlarged image.
	ConfigValue<bool> texture_scaling_restore_cnn{false};
	// Which upscaler builds the enlarged copy.
	//   0 = Catmull-Rom filter (measured as not worth its cost - kept because it
	//       is the cheap fallback and works at any factor)
	//   1 = FSRCNN-small x2, trained to upscale; falls back to 0 at any other
	//       factor, since the weights and sub-pixel layer are x2-specific
	ConfigValue<sint32> texture_scaling_upscaler{0};
	// Bitmask ambient occlusion, from the same visibility bit field the GI
	// builds (Therrien et al. 2023). Independent of the SSAO on purpose: both
	// can be enabled, they are different estimators, and the horizon-based one
	// cannot let light pass behind thin surfaces while this one can.
	ConfigValue<bool> faro_bitmask_ao_enabled{false};
	ConfigValue<sint32> faro_bitmask_ao_intensity{60};
	// Far distance in linearized depth units where this AO reaches zero; the
	// fade starts at half of it. 400 rather than the GI's 120, because the GI's
	// fade is what confined the occlusion to whatever was nearest - structures
	// generally sit further out than a character.
	ConfigValue<sint32> faro_bitmask_ao_distance{400};
	// Gather radius multiplier, as a percentage. 100 leaves the GI radius exactly
	// as it is; higher reaches the larger recesses that give a building its
	// shading, at the cost of a coarser march for the same step count.
	ConfigValue<sint32> faro_bitmask_ao_radius{100};
	ConfigValue<sint32> faro_shadow_scale{1};

	// audio
	sint32 audio_api = 0;
	sint32 audio_delay = 2;
	AudioChannels tv_channels = kStereo, pad_channels = kStereo, input_channels = kMono;
	sint32 tv_volume = 50, pad_volume = 0, input_volume = 50, portal_volume = 50;
	std::wstring tv_device{ L"default" }, pad_device, input_device, portal_device;

	// account
	struct
	{
		ConfigValueBounds<uint32> m_persistent_id{0x80000001, 0x80000001, 0xFFFFFFFF};
		ConfigValue<bool> legacy_online_enabled{false};
		ConfigValue<int> legacy_active_service{0};
		std::unordered_map<uint32, NetworkService> service_select; // per-account service index. Key is persistentId
	}account{};

	// input
	struct
	{
		ConfigValue<std::string> host{"127.0.0.1"};
		ConfigValue<uint16> port{ 26760 };
	}dsu_client{};

	// debug
	ConfigValueBounds<CrashDump> crash_dump{ CrashDump::Disabled };
	ConfigValue<uint16> gdb_port{ 1337 };
#ifdef ENABLE_METAL
	ConfigValue<std::string> gpu_capture_dir{ "" };
	ConfigValue<bool> framebuffer_fetch{ true };
#endif

	XMLConfigParser Load(XMLConfigParser& parser);
	XMLConfigParser Save(XMLConfigParser& parser);

	bool IsGameListFavorite(uint64 titleId);
	void SetGameListFavorite(uint64 titleId, bool isFavorite);
	bool GetGameListCustomName(uint64 titleId, std::string& customName);
	void SetGameListCustomName(uint64 titleId, std::string customName);

	NetworkService GetAccountNetworkService(uint32 persistentId);
	void SetAccountSelectedService(uint32 persistentId, NetworkService serviceIndex);

	// emulated usb devices
	struct
	{
		ConfigValue<bool> emulate_skylander_portal{false};
		ConfigValue<bool> emulate_infinity_base{false};
		ConfigValue<bool> emulate_dimensions_toypad{false};
	}emulated_usb_devices{};

	static int AudioChannelsToNChannels(AudioChannels kStereo)
	{
		switch (kStereo)
		{
		case 0:
			return 1; // will mix mono sound on both output channels
		case 2:
			return 6;
		default: // stereo
			return 2;
		}
	}

  private:
	GameEntry* GetGameEntryByTitleId(uint64 titleId);
	GameEntry* CreateGameEntry(uint64 titleId);
};

typedef XMLDataConfig<CemuConfig> XMLCemuConfig_t;

inline XMLCemuConfig_t& GetConfigHandle()
{
	static XMLCemuConfig_t config;
	return config;
}

inline CemuConfig& GetConfig()
{
	return GetConfigHandle().data();
}
