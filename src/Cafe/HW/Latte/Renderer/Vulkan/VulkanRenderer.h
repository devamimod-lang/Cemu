#pragma once

#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureViewVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/CachedFBOVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/SwapchainInfoVk.h"
#include "util/math/vector2.h"
#include "util/helpers/Semaphore.h"
#include "util/containers/flat_hash_map.hpp"
#include "util/containers/robin_hood.h"

struct VkSupportedFormatInfo_t
{
	bool fmt_d24_unorm_s8_uint{};
	bool fmt_r4g4_unorm_pack{};
	bool fmt_r5g6b5_unorm_pack{};
	bool fmt_r4g4b4a4_unorm_pack{};
	bool fmt_a1r5g5b5_unorm_pack{};
};

struct VkDescriptorSetInfo
{
	VKRObjectDescriptorSet* m_vkObjDescriptorSet{};

	~VkDescriptorSetInfo();

	std::vector<LatteTextureViewVk*> list_referencedViews;
	std::vector<LatteTextureVk*> list_fboCandidates; // prefiltered list of textures which may need a barrier
	LatteConst::ShaderType shaderType{};
	uint64 stateHash{};
	class PipelineInfo* pipeline_info{};

	// tracking for allocated descriptors
	uint8 statsNumSamplerTextures{ 0 };
	uint8 statsNumDynUniformBuffers{ 0 };
	uint8 statsNumStorageBuffers{ 0 };
};

class VkException : public std::runtime_error
{
public:
	VkException(VkResult result, const std::string& message)
		: runtime_error(message), m_result(result)
	{}

	VkResult GetResult() const { return m_result; }

private:
	VkResult m_result;
};

namespace VulkanRendererConst
{
	static const inline int SHADER_STAGE_INDEX_VERTEX = static_cast<int>(LatteConst::ShaderType::Vertex);
	static const inline int SHADER_STAGE_INDEX_FRAGMENT = static_cast<int>(LatteConst::ShaderType::Pixel);
	static const inline int SHADER_STAGE_INDEX_GEOMETRY = static_cast<int>(LatteConst::ShaderType::Geometry);
	static const inline int SHADER_STAGE_INDEX_COUNT = 4;
};

// the order doesnt really matter but the types should cover range 0-2 since we use them as an array index
static_assert(static_cast<int>(LatteConst::ShaderType::Vertex) == 0);
static_assert(static_cast<int>(LatteConst::ShaderType::Pixel) == 1);
static_assert(static_cast<int>(LatteConst::ShaderType::Geometry) == 2);

class PipelineInfo
{
public:
	PipelineInfo(uint64 minimalStateHash, uint64 pipelineHash, struct LatteFetchShader* fetchShader, LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader);
	PipelineInfo(const PipelineInfo& info) = delete;
	~PipelineInfo();

	bool operator==(const PipelineInfo& pipeline_info) const
	{
		return true;
	}


	template<typename T>
	struct direct_hash
	{
		size_t operator()(const uint64& k) const noexcept
		{
			return k;
		}
	};
	using DescriptorSetCache = ska::flat_hash_map<uint64, VkDescriptorSetInfo*, direct_hash<uint64>>;

	FORCE_INLINE DescriptorSetCache& GetDescriptorSetCache(LatteConst::ShaderType shaderType)
	{
		cemu_assert_debug(shaderType == LatteConst::ShaderType::Vertex || shaderType == LatteConst::ShaderType::Pixel || shaderType == LatteConst::ShaderType::Geometry);
		return ds_cache[static_cast<size_t>(shaderType)];
	}

	// std::unordered_map<uint64, VkDescriptorSetInfo*> 3.16% (total CPU time)
	// robin_hood::unordered_flat_map<uint64, VkDescriptorSetInfo*> descriptor set cache; ~1.80%
	// ska::bytell_hash_map<uint64, VkDescriptorSetInfo*, direct_hash<uint64>> descriptor set cache; -> 1.91%
	DescriptorSetCache ds_cache[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT]; // 1.71%

	VKRObjectPipeline* m_vkrObjPipeline;

	LatteDecompilerShader* vertexShader = nullptr;
	LatteDecompilerShader* geometryShader = nullptr;
	LatteDecompilerShader* pixelShader = nullptr;
	LatteFetchShader* fetchShader = nullptr;
	Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE primitiveMode{};

	RendererShaderVk* vertexShaderVk = nullptr;
	RendererShaderVk* geometryShaderVk = nullptr;
	RendererShaderVk* pixelShaderVk = nullptr;

	uint64 minimalStateHash;
	uint64 stateHash;

	bool usesBlendConstants{ false };
	bool usesDepthBias{ false };

	struct
	{
		bool hasUniformVar[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
		bool hasUniformBuffers[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
		std::vector<uint8> list_uniformBuffers[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
	}dynamicOffsetInfo{};

	// primitive rects emulation
	RendererShaderVk* rectEmulationGS = nullptr;

	// hack - accurate barrier needed for this pipeline
	bool neverSkipAccurateBarrier{false};
};

namespace WindowSystem
{
	struct WindowHandleInfo;
};

class VulkanRenderer : public Renderer
{
	friend class LatteQueryObjectVk;
	friend class LatteTextureReadbackInfoVk;
	friend class PipelineCompiler;

	using VSync = SwapchainInfoVk::VSync;

	static const inline int UNIFORMVAR_RINGBUFFER_SIZE = 1024 * 1024 * 16; // 16MB

	static const inline int TEXTURE_READBACK_SIZE = 32 * 1024 * 1024; // 32 MB

	static const inline int OCCLUSION_QUERY_POOL_SIZE = 1024;

public:

	// memory management
	std::unique_ptr<VKRMemoryManager> memoryManager;
	VKRMemoryManager* GetMemoryManager() const { return memoryManager.get(); };

	VkSupportedFormatInfo_t m_supportedFormatInfo;

	typedef struct
	{
		// Vulkan image info
		VkFormat vkImageFormat;
		VkImageAspectFlags vkImageAspect;
		bool isCompressed;
		bool isAlternateFormat; // true if the host pixel format doesn't 1:1 match the emulated format

		// texture decoder info
		TextureDecoder* decoder;

		sint32 texelCountX;
		sint32 texelCountY;
	}FormatInfoVK;

	struct DeviceInfo
	{
		DeviceInfo(const std::string name, std::span<uint8, VK_UUID_SIZE> uuid)
			: name(name)
		{
			std::copy(uuid.begin(), uuid.end(), this->uuid.begin());
		}

		std::string name;
		std::array<uint8, VK_UUID_SIZE> uuid;
	};

	static std::vector<DeviceInfo> GetDevices();
	VulkanRenderer();
	virtual ~VulkanRenderer();

	static VulkanRenderer* GetInstance();

	void UnrecoverableError(const char* errMsg) const;

	void GetDeviceFeatures();
	void DetermineVendor();
	void InitializeSurface(const Vector2i& size, bool mainWindow);

	const std::unique_ptr<SwapchainInfoVk>& GetChainInfoPtr(bool mainWindow) const;
	SwapchainInfoVk& GetChainInfo(bool mainWindow) const;

	void StopUsingPadAndWait();
	bool IsPadWindowActive() override;

	void HandleScreenshotRequest(LatteTextureView* texView, bool padView) override;

	void QueryMemoryInfo();
	void QueryAvailableFormats();

#if BOOST_OS_WINDOWS
	static VkSurfaceKHR CreateWinSurface(VkInstance instance, HWND hwindow);
#endif
#if BOOST_OS_LINUX || BOOST_OS_BSD
	static VkSurfaceKHR CreateXlibSurface(VkInstance instance, Display* dpy, Window window);
    static VkSurfaceKHR CreateXcbSurface(VkInstance instance, xcb_connection_t* connection, xcb_window_t window);
	#ifdef HAS_WAYLAND
	static VkSurfaceKHR CreateWaylandSurface(VkInstance instance, wl_display* display, wl_surface* surface);
	#endif
#endif

	static VkSurfaceKHR CreateFramebufferSurface(VkInstance instance, struct WindowSystem::WindowHandleInfo& windowInfo);

	void AppendOverlayDebugInfo() override;

	void ImguiInit();
	VkInstance GetVkInstance() const { return m_instance; }
	VkDevice GetLogicalDevice() const { return m_logicalDevice; }
	VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }

	VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }

	void WaitDeviceIdle() const { vkDeviceWaitIdle(m_logicalDevice); }

	void Initialize() override;
	void Shutdown() override;

	void SwapBuffers(bool swapTV = true, bool swapDRC = true) override;

	void Flush(bool waitIdle = false) override;
	void NotifyLatteCommandProcessorIdle() override;
	void SurfaceSync(Latte::E_COHER_CNTL coher, MPTR address, uint32 size) override;

	uint64 GenUniqueId(); // return unique id (uses incrementing counter)

	void DrawEmptyFrame(bool mainWindow) override;

	void InitFirstCommandBuffer();
	void ProcessFinishedCommandBuffers();
	void WaitForNextFinishedCommandBuffer();
	void SubmitCommandBuffer(VkSemaphore signalSemaphore = VK_NULL_HANDLE, VkSemaphore waitSemaphore = VK_NULL_HANDLE);
	void RequestSubmitSoon();
	void RequestSubmitOnIdle();

	// command buffer synchronization
	uint64 GetCurrentCommandBufferId() const;
	bool HasCommandBufferFinished(uint64 commandBufferId) const;
	void WaitCommandBufferFinished(uint64 commandBufferId);

	// resource destruction queue
	void ReleaseDestructibleObject(VKRDestructibleObject* destructibleObject);
	void ProcessDestructionQueue();

	FSpinlock m_spinlockDestructionQueue;
	std::vector<VKRDestructibleObject*> m_destructionQueue;

	void PipelineCacheSaveThread(size_t cache_size);

	void ClearColorbuffer(bool padView) override;
	void ClearColorImageRaw(VkImage image, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout inputLayout, VkImageLayout outputLayout);
	void ClearColorImage(LatteTextureVk* vkTexture, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout outputLayout);

	void DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	// Real RCAS needs to read already-upscaled neighbour pixels that don't
	// exist yet during EASU, so FSR1 is always at least 2 passes: easuShader
	// renders into m_fsr1EasuIntermediate (see EnsureFsr1EasuIntermediateTarget),
	// then rcasShader reads that and writes the sharpened result into the real
	// swapchain/pad framebuffer. Returns false (caller should fall back to a
	// plain DrawBackbufferQuad(shader=easuShader) call, which skips RCAS
	// entirely) if the intermediate target couldn't be (re)created this frame.
	bool DrawBackbufferQuadFsr1(LatteTextureView* texView, RendererOutputShader* easuShader, RendererOutputShader* rcasShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight,
												bool padView, bool clearBackground) override;
	// FXAA needs a further pass on top of FSR1's own EASU+RCAS passes (see
	// DrawBackbufferQuadFsr1 above), so it can't be fused into that dispatch
	// either. Runs EASU->m_fsr1EasuIntermediate, RCAS->m_fxaaIntermediate, then
	// secondShader (FXAA) from m_fxaaIntermediate into the real swapchain/pad
	// framebuffer. Returns false (caller should fall back to a plain
	// DrawBackbufferQuad(shader=easuShader) call) if either intermediate
	// target couldn't be (re)created for this frame's size.
	bool DrawBackbufferQuadTwoPass(LatteTextureView* texView, RendererOutputShader* easuShader, RendererOutputShader* rcasShader, RendererOutputShader* secondShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight,
												bool padView, bool clearBackground) override;
	void CreateDescriptorPool();
	VkDescriptorSet backbufferBlit_createDescriptorSet(VkDescriptorSetLayout descriptor_set_layout, LatteTextureViewVk* texViewVk, bool useLinearTexFilter);
	// Same as above but for a plain VkImageView/VkSampler not backed by a
	// LatteTextureViewVk - used to bind the FXAA/RCAS intermediate target,
	// which is a raw internal render target rather than an emulated GX2 texture.
	VkDescriptorSet backbufferBlit_createDescriptorSetRaw(VkDescriptorSetLayout descriptor_set_layout, VkImageView imageView, VkSampler sampler);
	// (Re)creates m_fsr1EasuIntermediate* (EASU's raw upscaled-but-unsharpened
	// output - RCAS's own input) to match the requested size/format if needed.
	// Same render-pass-compatibility trick as EnsureFxaaIntermediateTarget
	// below (see its own doc comment).
	bool EnsureFsr1EasuIntermediateTarget(VkFormat format, uint32 width, uint32 height);
	void DestroyFsr1EasuIntermediateTarget();
	// (Re)creates m_fxaaIntermediate* (RCAS's output - the fully upscaled AND
	// sharpened image, which FXAA/SMAA read as their own "FSR1 output" input)
	// to match the requested size/format if needed. The render pass is built
	// to be compatible (same format/sample count/attachment layout) with
	// chainInfo.m_swapchainRenderPass so the already-cached pipeline from
	// backbufferBlit_createGraphicsPipeline can be reused directly for any
	// pass that writes here, per Vulkan's render pass compatibility rules -
	// no second pipeline-creation path needed.
	bool EnsureFxaaIntermediateTarget(VkFormat format, uint32 width, uint32 height);
	void DestroyFxaaIntermediateTarget();

	// SMAA's 3-pass pipeline, chained after FSR1's own EASU+RCAS passes (see
	// SMAALookupTextures.h and RendererOuputShader.cpp's
	// s_smaa_edge/blend/neighborhood_shader_source for the shaders). Same
	// fallback contract as DrawBackbufferQuadTwoPass: returns false (caller
	// falls back to a plain DrawBackbufferQuad(shader=easuShader) call) if
	// the intermediate targets couldn't be (re)created for this frame's size.
	bool DrawBackbufferQuadFsr1Smaa(LatteTextureView* texView, RendererOutputShader* easuShader, RendererOutputShader* rcasShader,
												RendererOutputShader* edgeShader, RendererOutputShader* blendShader, RendererOutputShader* neighborhoodShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight,
												bool padView, bool clearBackground) override;
	// Uploads SMAALookupTextures.h's embedded AreaTex/SearchTex byte arrays
	// into 2 static Vulkan textures, once, during Initialize(). These never
	// change afterwards and are reused as filler in every descriptor set
	// allocated against m_swapchainDescriptorSetLayout that doesn't have a
	// more specific use for bindings 2/3 (see backbufferBlit_createDescriptorSet
	// and backbufferBlit_createDescriptorSetRaw), so no shader ever binds an
	// unwritten descriptor even when it doesn't use SMAA.
	void CreateSmaaStaticTextures();
	void DestroySmaaStaticTextures();
	// (Re)creates m_smaaEdges*/m_smaaBlend* (and, via EnsureFxaaIntermediateTarget,
	// the shared FSR1-output target) to match the requested size/format.
	bool EnsureSmaaIntermediateTargets(VkFormat format, uint32 width, uint32 height);
	void DestroySmaaIntermediateTargets();

	// Faro TAA
	bool DrawBackbufferQuadFsr1Taa(LatteTextureView* texView, RendererOutputShader* easuShader, RendererOutputShader* rcasShader,
												RendererOutputShader* resolveShader, RendererOutputShader* fxaaShader,
												RendererOutputShader* smaaEdgeShader, RendererOutputShader* smaaBlendShader, RendererOutputShader* smaaNeighborhoodShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY,
												sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	// Faro: AA without FSR1 - see Renderer.h's own doc comment on these three.
	bool DrawBackbufferQuadFxaa(LatteTextureView* texView, RendererOutputShader* upscaleShader, RendererOutputShader* fxaaShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY,
												sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	bool DrawBackbufferQuadSmaa(LatteTextureView* texView, RendererOutputShader* upscaleShader,
												RendererOutputShader* edgeShader, RendererOutputShader* blendShader, RendererOutputShader* neighborhoodShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY,
												sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	bool DrawBackbufferQuadTaa(LatteTextureView* texView, RendererOutputShader* upscaleShader,
												RendererOutputShader* resolveShader, RendererOutputShader* fxaaShader,
												RendererOutputShader* smaaEdgeShader, RendererOutputShader* smaaBlendShader, RendererOutputShader* smaaNeighborhoodShader,
												bool useLinearTexFilter, sint32 imageX, sint32 imageY,
												sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground) override;
	// (Re)creates m_taaHistory* to match the requested size/format, WITHOUT
	// resetting m_taaHistoryValid unless the size/format actually changed (the
	// whole point of this target is to survive across frames - unlike every
	// other Ensure*IntermediateTarget here, which get fully recreated content
	// every single draw). No render pass/framebuffer needed - this is never
	// rendered into directly, only written via vkCmdCopyImage from
	// m_taaNativeResolveImage at the end of DrawBackbufferQuadFsr1Taa, and
	// read as a plain sampled texture at the start of the next frame's
	// resolve pass. Sized at the GAME's native/source resolution, not the
	// display output resolution - see DrawBackbufferQuadFsr1Taa's own comment
	// on why TAA resolves before FSR1's upscale, not after it.
	bool EnsureTaaHistoryTarget(VkFormat format, uint32 width, uint32 height);
	void DestroyTaaHistoryTarget();
	// (Re)creates the native-resolution target that the TAA resolve pass
	// renders into (current frame's jittered image blended with history) -
	// this is what EASU then reads as ITS input, instead of the game's raw
	// texture directly. Sized at native/source resolution like
	// EnsureTaaHistoryTarget above, not output resolution.
	bool EnsureTaaNativeResolveTarget(VkFormat format, uint32 width, uint32 height);
	void DestroyTaaNativeResolveTarget();
	// (Re)creates m_taaFxaaImage (the shared spatial pre-pass output) and,
	// only if width/height/format actually changed, m_taaSmaaEdges*/
	// m_taaSmaaBlend* too - see those members' own comments. Sized at
	// native/source resolution. The SMAA-only targets are created
	// unconditionally alongside m_taaFxaaImage (cheap, native res) so
	// switching CemuConfig::taa_spatial_aa live never needs a resize-triggered
	// recreation of just one half of this group.
	bool EnsureTaaSpatialNativeTargets(VkFormat format, uint32 width, uint32 height);
	void DestroyTaaSpatialNativeTargets();

	robin_hood::unordered_flat_map<uint64, robin_hood::unordered_flat_map<uint64, PipelineInfo*> > m_pipeline_info_cache; // using robin_hood::unordered_flat_map is twice as fast (1-2% overall CPU time reduction)
	void draw_debugPipelineHashState();
	PipelineInfo* draw_getCachedPipeline();

	// pipeline state hash
	static uint64 draw_calculateMinimalGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteContextRegister& lcr);
	static uint64 draw_calculateGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const VKRObjectRenderPass* renderPassObj, const LatteContextRegister& lcr);

	// rendertarget
	void renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ = false) override;
	void renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight) override;

	LatteCachedFBO* rendertarget_createCachedFBO(uint64 key) override;
	void rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo) override;
	void rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo) override;

	// texture functions
	void* texture_acquireTextureUploadBuffer(uint32 size) override;
	void texture_releaseTextureUploadBuffer(uint8* mem) override;

	TextureDecoder* texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height) override;

	void texture_clearSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex) override;
	void texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a) override;
	void texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue) override;

	void texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize) override;

	LatteTexture* texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth) override;

	void texture_setLatteTexture(LatteTextureView* textureView, uint32 textureUnit) override;

	void texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY, sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth) override;
	LatteTextureReadbackInfo* texture_createReadback(LatteTextureView* textureView) override;

	// surface copy
	void surfaceCopy_copySurfaceWithFormatConversion(LatteTexture* sourceTexture, sint32 srcMip, sint32 srcSlice, LatteTexture* destinationTexture, sint32 dstMip, sint32 dstSlice, sint32 width, sint32 height) override;
	void surfaceCopy_notifyTextureRelease(LatteTextureVk* hostTexture);

	private:
	void surfaceCopy_viaDrawcall(LatteTextureVk* srcTextureVk, sint32 texSrcMip, sint32 texSrcSlice, LatteTextureVk* dstTextureVk, sint32 texDstMip, sint32 texDstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight);

	void surfaceCopy_cleanup();

private:
	uint64 copySurface_getPipelineStateHash(struct VkCopySurfaceState_t& state);
	struct CopySurfacePipelineInfo* copySurface_getCachedPipeline(struct VkCopySurfaceState_t& state);
	struct CopySurfacePipelineInfo* copySurface_getOrCreateGraphicsPipeline(struct VkCopySurfaceState_t& state);
	VKRObjectTextureView* surfaceCopy_createImageView(LatteTextureVk* textureVk, uint32 sliceIndex, uint32 mipIndex);
	VKRObjectFramebuffer* surfaceCopy_getOrCreateFramebuffer(struct VkCopySurfaceState_t& state, struct CopySurfacePipelineInfo* pipelineInfo);
	VKRObjectDescriptorSet* surfaceCopy_getOrCreateDescriptorSet(struct VkCopySurfaceState_t& state, struct CopySurfacePipelineInfo* pipelineInfo);

	VKRObjectRenderPass* copySurface_createRenderpass(struct VkCopySurfaceState_t& state);

	std::unordered_map<uint64, struct CopySurfacePipelineInfo*> m_copySurfacePipelineCache;

public:
	// renderer interface
	void bufferCache_init(const sint32 bufferSize) override;
	void bufferCache_upload(uint8* buffer, sint32 size, uint32 bufferOffset) override;
	void bufferCache_copy(uint32 srcOffset, uint32 dstOffset, uint32 size) override;

	void buffer_bindVertexBuffer(uint32 bufferIndex, uint32 buffer, uint32 size) override;
	void buffer_bindVertexStrideWorkaroundBuffer(VkBuffer fixedBuffer, uint32 offset, uint32 bufferIndex, uint32 size);
	std::pair<VkBuffer, uint32> buffer_genStrideWorkaroundVertexBuffer(MPTR buffer, uint32 size, uint32 oldStride);
	void buffer_bindUniformBuffer(LatteConst::ShaderType shaderType, uint32 bufferIndex, uint32 offset, uint32 size) override;

	RendererShader* shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader) override;

	IndexAllocation indexData_reserveIndexMemory(uint32 size) override;
	void indexData_releaseIndexMemory(IndexAllocation& allocation) override;
	void indexData_uploadIndexMemory(IndexAllocation& allocation) override;

	// externally callable
	void GetTextureFormatInfoVK(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, sint32 width, sint32 height, FormatInfoVK* formatInfoOut);
	void unregisterGraphicsPipeline(PipelineInfo* pipelineInfo);

private:
	struct VkRendererState
	{
		VkRendererState() = default;
		VkRendererState(const VkRendererState&) = delete;
		VkRendererState(VkRendererState&&) noexcept = delete;

		// textures
		LatteTextureViewVk* boundTexture[128]{};

		// rendertarget
		CachedFBOVk* activeFBO{}; // the FBO active for the emulated GPU

		// command buffer
		VkCommandBuffer currentCommandBuffer{};

		// pipeline
		VkPipeline currentPipeline{ VK_NULL_HANDLE };

		// renderpass
		CachedFBOVk* activeRenderpassFBO{}; // the FBO of the currently active Vulkan renderpass

		// drawcall state
		PipelineInfo* activePipelineInfo{ nullptr };
		VkDescriptorSetInfo* activeVertexDS{ nullptr };
		VkDescriptorSetInfo* activePixelDS{ nullptr };
		VkDescriptorSetInfo* activeGeometryDS{ nullptr };
		bool descriptorSetsChanged{ false };
		CachedFBOVk::RendertargetSelfDependencyMask m_curRenderpassSelfDependencyInfo{};
		VkImageAspectFlags feedbackLoopImageAspect{0xFFFFFFFF};
		// viewport and scissor box
		VkViewport currentViewport{};
		VkRect2D currentScissorRect{};

		// vertex bindings
		struct
		{
			uint32 offset;
		}currentVertexBinding[LATTE_MAX_VERTEX_BUFFERS]{};

		// index buffer
		Renderer::INDEX_TYPE activeIndexType{};
		uint32 activeIndexBufferIndex{};
		uint32 activeIndexBufferOffset{};

		// polygon offset
		uint32 prevPolygonFrontOffsetU32{ 0xFFFFFFFF };
		uint32 prevPolygonFrontScaleU32{ 0xFFFFFFFF };
		uint32 prevPolygonFrontClampU32{ 0xFFFFFFFF };

		void resetCommandBufferState()
		{
			prevPolygonFrontOffsetU32 = 0xFFFFFFFF;
			prevPolygonFrontScaleU32 = 0xFFFFFFFF;
			prevPolygonFrontClampU32 = 0xFFFFFFFF;
			currentPipeline = VK_NULL_HANDLE;
			for (auto& itr : currentVertexBinding)
			{
				itr.offset = 0xFFFFFFFF;
			}
			activeIndexType = Renderer::INDEX_TYPE::NONE;
			activeIndexBufferIndex = std::numeric_limits<uint32>::max();
			activeIndexBufferOffset = std::numeric_limits<uint32>::max();
			feedbackLoopImageAspect = 0xFFFFFFFF;
		}

		// invalidation / flushing
		uint64 currentFlushIndex{0};
		bool colorBufferSyncPending{false}; // guest color-buffer sync since the previous draw; survives command-buffer resets
		bool requestFlush{ false }; // flush after every draw operation. The renderpass dependencies dont handle dependencies across multiple drawcalls inside a single renderpass

		// draw sequence
		bool drawSequenceSkip; // if true, skip draw_execute()
	}m_state;

	std::unique_ptr<SwapchainInfoVk> m_mainSwapchainInfo{}, m_padSwapchainInfo{};
	std::atomic_flag m_destroyPadSwapchainNextAcquire{};
	bool IsSwapchainInfoValid(bool mainWindow) const;

	VkRenderPass m_imguiRenderPass = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool;

  public:
	struct QueueFamilyIndices
	{
		int32_t graphicsFamily = -1;
		int32_t presentFamily = -1;

		bool IsComplete() const	{ return graphicsFamily >= 0 && presentFamily >= 0;	}
	};
	static QueueFamilyIndices FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device);

  private:

	struct FeatureControl
	{
		struct
		{
			// if using new optional extensions add to CheckDeviceExtensionSupport and CreateDeviceCreateInfo
			bool tooling_info = false; // VK_EXT_tooling_info
			bool depth_range_unrestricted = false;
			bool nv_fill_rectangle = false; // NV_fill_rectangle
			bool pipeline_feedback = false;
			bool pipeline_creation_cache_control = false; // VK_EXT_pipeline_creation_cache_control
			bool custom_border_color = false; // VK_EXT_custom_border_color
			bool custom_border_color_without_format = false; // VK_EXT_custom_border_color (specifically customBorderColorWithoutFormat)
			bool cubic_filter = false; // VK_EXT_FILTER_CUBIC_EXTENSION_NAME
			bool driver_properties = false; // VK_KHR_driver_properties
			bool external_memory_host = false; // VK_EXT_external_memory_host
			bool synchronization2 = false; // VK_KHR_synchronization2
			bool dynamic_rendering = false; // VK_KHR_dynamic_rendering
			bool shader_float_controls = false; // VK_KHR_shader_float_controls
			bool present_wait = false; // VK_KHR_present_wait
			bool depth_clip_enable = false; // VK_EXT_depth_clip_enable
			bool pipeline_robustness = false; // VK_EXT_pipeline_robustness
			bool attachment_feedback_loop_layout = false; // VK_EXT_attachment_feedback_loop_layout
			bool attachment_feedback_loop_dynamic_state = false; // VK_EXT_attachment_feedback_loop_dynamic_state (this is forced to false if VK_EXT_attachment_feedback_loop_layout is not supported)
		}deviceExtensions;

		struct
		{
			bool shaderRoundingModeRTEFloat32{ false };
		}shaderFloatControls; // from VK_KHR_shader_float_controls

		struct
		{
			bool debug_utils = false; // VK_EXT_DEBUG_UTILS
		}instanceExtensions;

		struct
		{
			uint32 minUniformBufferOffsetAlignment = 256;
			uint32 nonCoherentAtomSize = 256;
			// calculated
			uint32 calcUniformBufferAlignmentM1{};
		}limits;

		bool usingDebugMarkerTool{ false }; // validation layer or other tool capable of handling debug markers is used
		bool usingTracingTool{ false }; // frame debugger or other API replaying tool is used
		bool disableMultithreadedCompilation{ false }; // for old nvidia drivers

	}m_featureControl{};
	static bool CheckDeviceExtensionSupport(const VkPhysicalDevice device, FeatureControl& info);
	static std::vector<const char*> CheckInstanceExtensionSupport(FeatureControl& info);

	bool UpdateSwapchainProperties(bool mainWindow);
	void SwapBuffer(bool mainWindow);

	VkDescriptorSetLayout m_swapchainDescriptorSetLayout;

	VkQueue m_graphicsQueue, m_presentQueue;

	// swapchain

	std::vector<VkDeviceQueueCreateInfo> CreateQueueCreateInfos(const std::set<int>& uniqueQueueFamilies) const;
	VkDeviceCreateInfo CreateDeviceCreateInfo(const std::vector<VkDeviceQueueCreateInfo>& queueCreateInfos, const VkPhysicalDeviceFeatures& deviceFeatures, const void* deviceExtensionStructs, std::vector<const char*>& used_extensions) const;
	static bool IsDeviceSuitable(VkSurfaceKHR surface, const VkPhysicalDevice& device);

	void CreateCommandPool();
	void CreateCommandBuffers();

	void swapchain_createDescriptorSetLayout();

	// shader

	bool IsAsyncPipelineAllowed(uint32 numIndices);

	uint64 GetDescriptorSetStateHash(LatteDecompilerShader* shader);

	// imgui
	bool ImguiBegin(bool mainWindow) override;
	void ImguiEnd() override;
	ImTextureID GenerateTexture(const std::vector<uint8>& data, const Vector2i& size) override;
	void DeleteTexture(ImTextureID id) override;
	void DeleteFontTextures() override;
	bool BeginFrame(bool mainWindow) override;

	bool UseTFViaSSBO() const override
	{
		return true;
	}

	// drawcall emulation
	PipelineInfo* draw_createGraphicsPipeline(uint32 indexCount);
	PipelineInfo* draw_getOrCreateGraphicsPipeline(uint32 indexCount);

	void draw_updateVkBlendConstants();
	void draw_updateDepthBias(bool forceUpdate);

	void draw_setRenderPass();
	void draw_endRenderPass();

	void draw_beginSequence() override;
	void draw_execute(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext) override;
	void draw_endSequence() override;

	void draw_updateVertexBuffersDirectAccess();
	void draw_updateUniformBuffersDirectAccess(LatteDecompilerShader* shader, const uint32 uniformBufferRegOffset, LatteConst::ShaderType shaderType);

	void draw_prepareDynamicOffsetsForDescriptorSet(uint32 shaderStageIndex, uint32* dynamicOffsets, sint32& numDynOffsets, const PipelineInfo* pipeline_info);
	VkDescriptorSetInfo* draw_getOrCreateDescriptorSet(PipelineInfo* pipeline_info, LatteDecompilerShader* shader);
	void draw_prepareDescriptorSets(PipelineInfo* pipeline_info, VkDescriptorSetInfo*& vertexDS, VkDescriptorSetInfo*& pixelDS, VkDescriptorSetInfo*& geometryDS);
	void draw_handleSpecialState5();

	// draw synchronization helper
	void sync_inputTexturesChanged(bool withinFeedbackLoopRenderPass = false);
	void sync_RenderPassLoadTextures(CachedFBOVk* fboVk);
	void sync_RenderPassStoreTextures(CachedFBOVk* fboVk);

	// command buffer
	VkCommandBuffer getCurrentCommandBuffer() const { return m_state.currentCommandBuffer; }

	// uniform
	uint32 uniformData_uploadUniformDataBufferGetOffset(std::span<uint8, std::dynamic_extent> data);
	void uniformData_updateUniformVars(uint32 shaderStageIndex, LatteDecompilerShader* shader, float* __restrict uniformBuf);
	void uniformData_updateUniformVarsIncremental(uint32 shaderStageIndex, LatteDecompilerShader* shader, uint8& stageUniformModifiedMask, float* __restrict uniformBuf, bool aluConstDirty, uint32 uniformBufferDirtyMask);

	// misc
	void CreatePipelineCache();
	VkPipelineShaderStageCreateInfo CreatePipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule& module, const char* entryName) const;
	VkPipeline backbufferBlit_createGraphicsPipeline(VkDescriptorSetLayout descriptorLayout, bool padView, RendererOutputShader* shader);
	bool AcquireNextSwapchainImage(bool mainWindow);
	void RecreateSwapchain(bool mainWindow, bool skipCreate = false);

	// streamout
	void streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize) override;
	void streamout_begin() override;
	void bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size) override;
	void streamout_rendererFinishDrawcall() override;

	// occlusion queries
	LatteQueryObject* occlusionQuery_create() override;
	void occlusionQuery_destroy(LatteQueryObject* queryObj) override;
	void occlusionQuery_flush() override;
	void occlusionQuery_updateState() override;
	void occlusionQuery_notifyEndCommandBuffer();
	void occlusionQuery_notifyBeginCommandBuffer();

private:
	std::vector<const char*> m_layerNames;
	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice  m_logicalDevice = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_debugCallback = nullptr;
	volatile bool m_destructionRequested = false;

	QueueFamilyIndices m_indices{};

	Semaphore m_pipeline_cache_semaphore;
	std::shared_mutex m_pipeline_cache_save_mutex;
	std::thread m_pipeline_cache_save_thread;
	VkPipelineCache m_pipeline_cache{ nullptr };
	std::unordered_map<uint64, VkPipeline> m_backbufferBlitPipelineCache;
	std::unordered_map<uint64, VkDescriptorSet> m_backbufferBlitDescriptorSetCache;
	VkPipelineLayout m_pipelineLayout{nullptr};

	// FSR1 EASU intermediate render target (see DrawBackbufferQuadFsr1) - holds
	// EASU's raw upscaled-but-unsharpened output before RCAS reads it back.
	// Lazily (re)created by EnsureFsr1EasuIntermediateTarget when the
	// requested size or format changes (e.g. window resize).
	VkImage m_fsr1EasuIntermediateImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_fsr1EasuIntermediateAllocation = nullptr;
	VkImageView m_fsr1EasuIntermediateView = VK_NULL_HANDLE;
	VkSampler m_fsr1EasuIntermediateSampler = VK_NULL_HANDLE;
	VkRenderPass m_fsr1EasuIntermediateRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_fsr1EasuIntermediateFramebuffer = VK_NULL_HANDLE;
	VkDescriptorSet m_fsr1EasuIntermediateDescriptorSet = VK_NULL_HANDLE;
	VkExtent2D m_fsr1EasuIntermediateExtent{};
	VkFormat m_fsr1EasuIntermediateFormat = VK_FORMAT_UNDEFINED;

	// FXAA/RCAS intermediate render target (see DrawBackbufferQuadFsr1/
	// DrawBackbufferQuadTwoPass) - holds RCAS's output (the fully upscaled AND
	// sharpened image) which FXAA/SMAA read as their own "FSR1 output" input,
	// or which gets presented directly when no further antialiasing pass is
	// active. Lazily (re)created by EnsureFxaaIntermediateTarget when the
	// requested size or format changes (e.g. window resize).
	VkImage m_fxaaIntermediateImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_fxaaIntermediateAllocation = nullptr;
	VkImageView m_fxaaIntermediateView = VK_NULL_HANDLE;
	VkSampler m_fxaaIntermediateSampler = VK_NULL_HANDLE;
	VkRenderPass m_fxaaIntermediateRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_fxaaIntermediateFramebuffer = VK_NULL_HANDLE;
	VkDescriptorSet m_fxaaIntermediateDescriptorSet = VK_NULL_HANDLE;
	VkExtent2D m_fxaaIntermediateExtent{};
	VkFormat m_fxaaIntermediateFormat = VK_FORMAT_UNDEFINED;

	// SMAA's 2 static lookup textures (see SMAALookupTextures.h) - created
	// once by CreateSmaaStaticTextures() during Initialize() and never
	// resized/recreated afterwards, unlike the per-frame-sized targets below.
	VkImage m_smaaAreaTexImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_smaaAreaTexAllocation = nullptr;
	VkImageView m_smaaAreaTexView = VK_NULL_HANDLE;
	VkSampler m_smaaAreaTexSampler = VK_NULL_HANDLE;
	VkImage m_smaaSearchTexImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_smaaSearchTexAllocation = nullptr;
	VkImageView m_smaaSearchTexView = VK_NULL_HANDLE;
	VkSampler m_smaaSearchTexSampler = VK_NULL_HANDLE;

	// SMAA's 2 per-frame-sized intermediate targets (edges mask from pass 1,
	// blend weights from pass 2 - see DrawBackbufferQuadFsr1Smaa). Lazily
	// (re)created by EnsureSmaaIntermediateTargets alongside
	// m_fxaaIntermediate* (which SMAA reuses as its FSR1-output/pass-1-input
	// target, same as the FXAA two-pass path does). The edges target uses
	// LOAD_OP_CLEAR since the edge-detection shader uses `discard` on
	// non-edge pixels; the blend weights target uses DONT_CARE since every
	// pixel is written unconditionally.
	VkImage m_smaaEdgesImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_smaaEdgesAllocation = nullptr;
	VkImageView m_smaaEdgesView = VK_NULL_HANDLE;
	VkSampler m_smaaEdgesSampler = VK_NULL_HANDLE;
	VkRenderPass m_smaaEdgesRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_smaaEdgesFramebuffer = VK_NULL_HANDLE;
	VkImage m_smaaBlendImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_smaaBlendAllocation = nullptr;
	VkImageView m_smaaBlendView = VK_NULL_HANDLE;
	VkSampler m_smaaBlendSampler = VK_NULL_HANDLE;
	VkRenderPass m_smaaBlendRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_smaaBlendFramebuffer = VK_NULL_HANDLE;
	VkExtent2D m_smaaIntermediateExtent{};
	VkFormat m_smaaIntermediateFormat = VK_FORMAT_UNDEFINED;
	// Descriptor set for the blend-weight-calculation pass (binding 0 =
	// m_smaaEdgesView, bindings 2/3 = the static area/search textures - see
	// backbufferBlit_createDescriptorSetRaw). The edge-detection pass reuses
	// m_fxaaIntermediateDescriptorSet directly (binding 0 = the same FSR1
	// output target FXAA's own pass 2 already binds; bindings 2/3 are the
	// same static filler, unused by the edge-detection shader).
	VkDescriptorSet m_smaaBlendCalcDescriptorSet = VK_NULL_HANDLE;
	// Descriptor set for the final neighborhood-blending pass (binding 0 =
	// the FSR1 output target, binding 2 = m_smaaBlendView, binding 3 = the
	// static search texture as unused filler).
	VkDescriptorSet m_smaaNeighborhoodDescriptorSet = VK_NULL_HANDLE;

	// Faro TAA: history texture (previous frame's resolved TAA output, at
	// native/source resolution - see DrawBackbufferQuadFsr1Taa's own comment
	// on why TAA now resolves before FSR1's upscale). Unlike every target
	// above, this one is NOT rendered into via a render pass - it's only ever
	// written by vkCmdCopyImage (see DrawBackbufferQuadFsr1Taa) and read as a
	// plain sampled texture, and it deliberately survives across frames
	// instead of being torn down/rebuilt every draw - see
	// EnsureTaaHistoryTarget's own doc comment.
	VkImage m_taaHistoryImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_taaHistoryAllocation = nullptr;
	VkImageView m_taaHistoryView = VK_NULL_HANDLE;
	VkSampler m_taaHistorySampler = VK_NULL_HANDLE;
	VkExtent2D m_taaHistoryExtent{};
	VkFormat m_taaHistoryFormat = VK_FORMAT_UNDEFINED;
	// Descriptor set for the resolve pass (binding 0 = the game's own native
	// texture, i.e. texViewVk in DrawBackbufferQuadFsr1Taa - rewritten via
	// vkUpdateDescriptorSets every single draw since which texture that is
	// can change frame to frame, exactly like backbufferBlit_createDescriptorSet's
	// per-view caching handles for every other pass; binding 2 =
	// m_taaHistoryView, static, rebuilt alongside the history image itself in
	// EnsureTaaHistoryTarget). Allocated once in EnsureTaaHistoryTarget.
	VkDescriptorSet m_taaResolveDescriptorSet = VK_NULL_HANDLE;
	// False on the first frame after (re)creating the history target above -
	// tells the resolve shader there's nothing meaningful to blend with yet
	// (see s_taa_resolve_shader_source's taaHistoryValid branch).
	bool m_taaHistoryValid = false;

	// Faro TAA: the resolve pass's OWN output (current frame blended with
	// history, at native/source resolution) - this is what EASU reads as its
	// input instead of the game's raw texture, so upscaling always operates
	// on an already-antialiased image (see DrawBackbufferQuadFsr1Taa's own
	// comment). Unlike m_taaHistoryImage above, this one IS rendered into via
	// a render pass every frame - same pattern as m_fsr1EasuIntermediate*.
	VkImage m_taaNativeResolveImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_taaNativeResolveAllocation = nullptr;
	VkImageView m_taaNativeResolveView = VK_NULL_HANDLE;
	VkSampler m_taaNativeResolveSampler = VK_NULL_HANDLE;
	VkRenderPass m_taaNativeResolveRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_taaNativeResolveFramebuffer = VK_NULL_HANDLE;
	VkExtent2D m_taaNativeResolveExtent{};
	VkFormat m_taaNativeResolveFormat = VK_FORMAT_UNDEFINED;
	// Descriptor set for EASU's pass (binding 0 = m_taaNativeResolveView) -
	// EASU's normal input when TAA is active, in place of the game's raw
	// texture. Rebuilt alongside the target itself in
	// EnsureTaaNativeResolveTarget, same pattern as
	// m_fsr1EasuIntermediateDescriptorSet.
	VkDescriptorSet m_taaNativeResolveDescriptorSet = VK_NULL_HANDLE;

	// Faro TAA: spatial pre-pass output, at native/source resolution - runs
	// BEFORE the temporal resolve above, on the game's raw texture, so the
	// resolve blends an already spatially-antialiased frame instead of raw
	// jaggies. Shared final target for EITHER spatial technique the user
	// picks (CemuConfig::taa_spatial_aa, see DrawBackbufferQuadFsr1Taa/
	// DrawBackbufferQuadTaa for the actual branch): FXAA writes here directly
	// in one pass (reads the raw game texture via the existing
	// backbufferBlit_createDescriptorSet cache, same as
	// DrawBackbufferQuadTwoPass's own pass 1); SMAA's 3rd pass
	// (neighborhood blend) writes here too, using m_taaSmaaEdges*/m_taaSmaaBlend*
	// below as its own native-res intermediates - SMAA's morphological edge
	// search needs a minimum-width contrast pattern to detect an edge at all,
	// so it preserves thin diagonal geometry (fences, power lines) FXAA's
	// blur alone would soften, at 3 passes' worth of extra GPU cost instead
	// of 1.
	VkImage m_taaFxaaImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_taaFxaaAllocation = nullptr;
	VkImageView m_taaFxaaView = VK_NULL_HANDLE;
	VkSampler m_taaFxaaSampler = VK_NULL_HANDLE;
	VkRenderPass m_taaFxaaRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_taaFxaaFramebuffer = VK_NULL_HANDLE;
	VkExtent2D m_taaFxaaExtent{};
	VkFormat m_taaFxaaFormat = VK_FORMAT_UNDEFINED;

	// Faro TAA: SMAA's own native-res intermediates when taa_spatial_aa ==
	// kTaaSpatialSmaa (see m_taaFxaaImage's own comment) - edges from pass 1,
	// blend weights from pass 2; pass 3 (neighborhood blend) writes into the
	// shared m_taaFxaaImage above instead of a 3rd dedicated target.
	// (Re)created by EnsureTaaSmaaNativeTargets alongside m_taaFxaaImage.
	VkImage m_taaSmaaEdgesImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_taaSmaaEdgesAllocation = nullptr;
	VkImageView m_taaSmaaEdgesView = VK_NULL_HANDLE;
	VkSampler m_taaSmaaEdgesSampler = VK_NULL_HANDLE;
	VkRenderPass m_taaSmaaEdgesRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_taaSmaaEdgesFramebuffer = VK_NULL_HANDLE;
	VkImage m_taaSmaaBlendImage = VK_NULL_HANDLE;
	VkImageMemAllocation* m_taaSmaaBlendAllocation = nullptr;
	VkImageView m_taaSmaaBlendView = VK_NULL_HANDLE;
	VkSampler m_taaSmaaBlendSampler = VK_NULL_HANDLE;
	VkRenderPass m_taaSmaaBlendRenderPass = VK_NULL_HANDLE;
	VkFramebuffer m_taaSmaaBlendFramebuffer = VK_NULL_HANDLE;
	// Blend-weight-calc pass descriptor set (binding 0 = m_taaSmaaEdgesView,
	// bindings 2/3 = the same static area/search textures
	// m_smaaBlendCalcDescriptorSet uses).
	VkDescriptorSet m_taaSmaaBlendCalcDescriptorSet = VK_NULL_HANDLE;
	// Neighborhood-blend pass descriptor set (binding 2 = m_taaSmaaBlendView,
	// binding 3 = the static search texture as unused filler) - binding 0
	// (the raw game texture) is rewritten every draw, same reason
	// m_taaResolveDescriptorSet's binding 0 already is.
	VkDescriptorSet m_taaSmaaNeighborhoodDescriptorSet = VK_NULL_HANDLE;

	VkCommandPool m_commandPool{ nullptr };

	// buffer to cache uniform vars
	VkBuffer m_uniformVarBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_uniformVarBufferMemory = VK_NULL_HANDLE;
	bool m_uniformVarBufferMemoryIsCoherent{false};
	uint8* m_uniformVarBufferPtr = nullptr;
	uint32 m_uniformVarBufferWriteIndex = 0;
	uint32 m_uniformVarBufferReadIndex = 0;

	// transform feedback ringbuffer
	VkBuffer m_xfbRingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_xfbRingBufferMemory = VK_NULL_HANDLE;

	// buffer cache (attributes, uniforms and streamout)
	VkBuffer m_bufferCache = VK_NULL_HANDLE;
	VkDeviceMemory m_bufferCacheMemory = VK_NULL_HANDLE;

	// texture readback
	VkBuffer m_textureReadbackBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_textureReadbackBufferMemory = VK_NULL_HANDLE;
	uint8* m_textureReadbackBufferPtr = nullptr;
	uint32 m_textureReadbackBufferWriteIndex = 0;

	// placeholder objects to simulate NULL buffers and textures
	struct NullTexture
	{
		VkImage image;
		VkImageView view;
		VkSampler sampler;
		VkImageMemAllocation* allocation;
	};

	NullTexture nullTexture1D{};
	NullTexture nullTexture2D{};

	void CreateNullTexture(NullTexture& nullTex, VkImageType imageType);
	void CreateNullObjects();
	void DeleteNullTexture(NullTexture& nullTex);
	void DeleteNullObjects();

	// if VK_EXT_external_memory_host is supported we can (optionally) import all of the Wii U memory into a Vulkan memory object
	// this allows us to skip any vertex/uniform caching logic and let the GPU directly read the memory from main RAM
	// Wii U memory imported into a buffer
	static constexpr bool m_useHostMemoryForCache{ false }; // currently disabled and made constexpr so the compiler eliminates the branches that will never be taken
	VkBuffer m_importedMem = VK_NULL_HANDLE;
	VkDeviceMemory m_importedMemMemory = VK_NULL_HANDLE;
	MPTR m_importedMemBaseAddress = 0;

	// command buffer, garbage collection, synchronization
	static constexpr uint32 kCommandBufferPoolSize = 128;

	size_t m_commandBufferIndex = 0; // current buffer being filled
	size_t m_commandBufferSyncIndex = 0; // latest buffer that finished execution (updated on submit)
	size_t m_commandBufferIDOfPrevFrame = 0;
	std::array<size_t, kCommandBufferPoolSize> m_cmdBufferUniformRingbufIndices {}; // read index in the uniform ringbuffer after the command buffer finishes
	std::array<VkFence, kCommandBufferPoolSize> m_cmdBufferFences;
	std::array<VkCommandBuffer, kCommandBufferPoolSize> m_commandBuffers;
	std::array<VkSemaphore, kCommandBufferPoolSize> m_commandBufferSemaphores;

	VkSemaphore GetLastSubmittedCmdBufferSemaphore()
	{
		return m_commandBufferSemaphores[(m_commandBufferIndex + m_commandBufferSemaphores.size() - 1) % m_commandBufferSemaphores.size()];
	}

	uint64 m_numSubmittedCmdBuffers{};
	uint64 m_countCommandBufferFinished{};

	uint32 m_recordedDrawcalls{}; // number of drawcalls recorded into current command buffer
	uint32 m_submitThreshold{}; // submit current buffer if recordedDrawcalls exceeds this number
	bool m_submitOnIdle{}; // submit current buffer if Latte command processor goes into idle state (no more commands or waiting for externally signaled condition)

	// drawcall handling
	void draw_execute_first(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext);
	void draw_execute_continued(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, const LatteDrawcallContext& drawcallContext);

	// tracking for dynamic offsets
	struct
	{
		uint32 uniformVarBufferOffset[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
		struct
		{
			uint32 uniformBufferOffset[LATTE_NUM_MAX_UNIFORM_BUFFERS];
		}shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_COUNT];
	}dynamicOffsetInfo{};

	// streamout
	struct
	{
		struct
		{
			bool enabled;
			uint32 ringBufferOffset;
		}buffer[LATTE_NUM_STREAMOUT_BUFFER];
		sint32 verticesPerInstance;
	}m_streamoutState{};

	struct
	{
		VkQueryPool queryPool{VK_NULL_HANDLE};
		sint32 currentQueryIndex{};
		std::vector<class LatteQueryObjectVk*> list_cachedQueries;
		std::vector<class LatteQueryObjectVk*> list_currentlyActiveQueries;
		uint64 m_lastCommandBuffer{};
		// query result buffer
		VkBuffer bufferQueryResults;
		VkDeviceMemory memoryQueryResults;
		uint64* ptrQueryResults;
		std::vector<uint16> list_availableQueryIndices;
	}m_occlusionQueries;

	// barrier

	enum SYNC_OP : uint32
	{
		/* name */						/* operations */
		HOST_WRITE			= 0x01,
		HOST_READ			= 0x02,

		// BUFFER_INDEX_READ (should be separated?)
		BUFFER_SHADER_READ	= 0x04,		// any form of shader read access
		BUFFER_SHADER_WRITE	= 0x08,		// any form of shader write access
		ANY_TRANSFER		= 0x10,		// previous transfer to/from buffer or image
		TRANSFER_READ		= 0x80,		// transfer from image/buffer
		TRANSFER_WRITE		= 0x100,	// transfer to image/buffer


		IMAGE_READ			= 0x20,
		IMAGE_WRITE			= 0x40,

	};

	template<uint32 TSyncOp>
	void barrier_calcStageAndMask(VkPipelineStageFlags& stages, VkAccessFlags& accessFlags)
	{
		stages = 0;
		accessFlags = 0;
		if constexpr ((TSyncOp & BUFFER_SHADER_READ) != 0)
		{
			// in theory: VK_ACCESS_INDEX_READ_BIT should be set here too but indices are currently separated
			stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			accessFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
		}

		if constexpr ((TSyncOp & BUFFER_SHADER_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			accessFlags |= VK_ACCESS_SHADER_WRITE_BIT;
		}

		if constexpr ((TSyncOp & ANY_TRANSFER) != 0)
		{
			//stages |= VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
			//accessFlags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
			stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			accessFlags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

			//accessFlags |= VK_ACCESS_MEMORY_READ_BIT;
			//accessFlags |= VK_ACCESS_MEMORY_WRITE_BIT;
		}

		if constexpr ((TSyncOp & TRANSFER_READ) != 0)
		{
			stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			accessFlags |= VK_ACCESS_TRANSFER_READ_BIT;

			//accessFlags |= VK_ACCESS_MEMORY_READ_BIT;
		}

		if constexpr ((TSyncOp & TRANSFER_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			accessFlags |= VK_ACCESS_TRANSFER_WRITE_BIT;

			//accessFlags |= VK_ACCESS_MEMORY_WRITE_BIT;
		}

		if constexpr ((TSyncOp & HOST_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_HOST_BIT;
			accessFlags |= VK_ACCESS_HOST_WRITE_BIT;
		}

		if constexpr ((TSyncOp & HOST_READ) != 0)
		{
			stages |= VK_PIPELINE_STAGE_HOST_BIT;
			accessFlags |= VK_ACCESS_HOST_READ_BIT;
		}

		if constexpr ((TSyncOp & IMAGE_READ) != 0)
		{
			stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			accessFlags |= VK_ACCESS_SHADER_READ_BIT;

			stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			accessFlags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

			stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			accessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		if constexpr ((TSyncOp & IMAGE_WRITE) != 0)
		{
			stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			accessFlags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			accessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
	}

	template<uint32 TSrcSyncOp, uint32 TDstSyncOp>
	void barrier_bufferRange(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
	{
		VkBufferMemoryBarrier bufMemBarrier{};
		bufMemBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufMemBarrier.pNext = nullptr;
		bufMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		VkPipelineStageFlags srcStages = 0;
		VkPipelineStageFlags dstStages = 0;

		bufMemBarrier.srcAccessMask = 0;
		bufMemBarrier.dstAccessMask = 0;

		barrier_calcStageAndMask<TSrcSyncOp>(srcStages, bufMemBarrier.srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOp>(dstStages, bufMemBarrier.dstAccessMask);

		bufMemBarrier.buffer = buffer;
		bufMemBarrier.offset = offset;
		bufMemBarrier.size = size;
		vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStages, dstStages, 0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr);
	}

	template<uint32 TSrcSyncOpA, uint32 TDstSyncOpA, uint32 TSrcSyncOpB, uint32 TDstSyncOpB>
	void barrier_bufferRange(VkBuffer bufferA, VkDeviceSize offsetA, VkDeviceSize sizeA,
							 VkBuffer bufferB, VkDeviceSize offsetB, VkDeviceSize sizeB)
	{
		VkPipelineStageFlags srcStagesA = 0;
		VkPipelineStageFlags dstStagesA = 0;
		VkPipelineStageFlags srcStagesB = 0;
		VkPipelineStageFlags dstStagesB = 0;

		VkBufferMemoryBarrier bufMemBarrier[2];

		bufMemBarrier[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufMemBarrier[0].pNext = nullptr;
		bufMemBarrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[0].srcAccessMask = 0;
		bufMemBarrier[0].dstAccessMask = 0;
		barrier_calcStageAndMask<TSrcSyncOpA>(srcStagesA, bufMemBarrier[0].srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOpA>(dstStagesA, bufMemBarrier[0].dstAccessMask);
		bufMemBarrier[0].buffer = bufferA;
		bufMemBarrier[0].offset = offsetA;
		bufMemBarrier[0].size = sizeA;

		bufMemBarrier[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufMemBarrier[1].pNext = nullptr;
		bufMemBarrier[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufMemBarrier[1].srcAccessMask = 0;
		bufMemBarrier[1].dstAccessMask = 0;
		barrier_calcStageAndMask<TSrcSyncOpB>(srcStagesB, bufMemBarrier[1].srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOpB>(dstStagesB, bufMemBarrier[1].dstAccessMask);
		bufMemBarrier[1].buffer = bufferB;
		bufMemBarrier[1].offset = offsetB;
		bufMemBarrier[1].size = sizeB;

		vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStagesA|srcStagesB, dstStagesA|dstStagesB, 0, 0, nullptr, 2, bufMemBarrier, 0, nullptr);
	}

	void barrier_sequentializeTransfer()
	{
		VkMemoryBarrier memBarrier{};
		memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memBarrier.pNext = nullptr;

		VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		memBarrier.dstAccessMask = 0;

		memBarrier.srcAccessMask |= (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
		memBarrier.dstAccessMask |= (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);

		vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStages, dstStages, 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
	}

	void barrier_sequentializeCommand()
	{
		VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStages, dstStages, 0, 0, nullptr, 0, nullptr, 0, nullptr);
	}

	template<uint32 TSrcSyncOp, uint32 TDstSyncOp>
	void barrier_image(VkImage imageVk, VkImageSubresourceRange& subresourceRange, VkImageLayout oldLayout, VkImageLayout newLayout)
	{
		VkPipelineStageFlags srcStages = 0;
		VkPipelineStageFlags dstStages = 0;

		VkImageMemoryBarrier imageMemBarrier{};
		imageMemBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemBarrier.srcAccessMask = 0;
		imageMemBarrier.dstAccessMask = 0;
		barrier_calcStageAndMask<TSrcSyncOp>(srcStages, imageMemBarrier.srcAccessMask);
		barrier_calcStageAndMask<TDstSyncOp>(dstStages, imageMemBarrier.dstAccessMask);
		imageMemBarrier.image = imageVk;
		imageMemBarrier.subresourceRange = subresourceRange;
		imageMemBarrier.oldLayout = oldLayout;
		imageMemBarrier.newLayout = newLayout;

		vkCmdPipelineBarrier(m_state.currentCommandBuffer,
							 srcStages, dstStages,
							 0,
							 0, NULL,
							 0, NULL,
							 1, &imageMemBarrier);
	}

	template<uint32 TSrcSyncOp, uint32 TDstSyncOp>
	void barrier_image(LatteTextureVk* vkTexture, VkImageSubresourceLayers& subresourceLayers, VkImageLayout newLayout)
	{
		VkImage imageVk = vkTexture->GetImageObj()->m_image;

		VkImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = subresourceLayers.aspectMask;
		subresourceRange.baseArrayLayer = subresourceLayers.baseArrayLayer;
		subresourceRange.layerCount = subresourceLayers.layerCount;
		subresourceRange.baseMipLevel = subresourceLayers.mipLevel;
		subresourceRange.levelCount = 1;

		barrier_image<TSrcSyncOp, TDstSyncOp>(imageVk, subresourceRange, vkTexture->GetImageLayout(subresourceRange), newLayout);

		vkTexture->SetImageLayout(subresourceRange, newLayout);
	}


public:
	bool GetDisableMultithreadedCompilation() const { return m_featureControl.disableMultithreadedCompilation; }
	bool HasSPRIVRoundingModeRTE32() const { return m_featureControl.shaderFloatControls.shaderRoundingModeRTEFloat32; }
	bool IsDebugMarkersEnabled() const { return m_featureControl.usingDebugMarkerTool; }
	bool IsTracingToolEnabled() const { return m_featureControl.usingTracingTool; }
	bool UseAttachmentFeedbackLoop() const { return m_featureControl.deviceExtensions.attachment_feedback_loop_dynamic_state; }

private:

	// debug
	void debug_genericBarrier();

	// shaders
	struct
	{
		RendererShaderVk* copySurface_vs{};
		RendererShaderVk* copySurface_psDepth2Color{};
		RendererShaderVk* copySurface_psColor2Depth{};
	}defaultShaders;


};
