#include "Cafe/HW/Latte/Core/LatteTextureScaling.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "config/CemuConfig.h"

namespace
{
	// Below this, a texture is almost never a surface. In a dump of Breath of
	// the Wild, 1295 of 3975 base textures were 64x64 or smaller: colour
	// lookup tables, gradient ramps, masks and UI pieces. Scaling a LUT is not
	// merely wasteful, it CORRUPTS rendering - interpolating between table
	// entries produces values the table does not contain. This guard is a
	// correctness requirement, not a performance tuning knob, which is why it
	// is a constant and not a setting.
	constexpr sint32 kMinSideToScale = 128;

	// Per-thread scratch, so the texture load path does not allocate per
	// texture. Textures arrive on the Latte thread today, but this is written
	// thread_local because moving the work onto a worker is the known next step
	// once the CPU cost shows up in practice.
	thread_local std::vector<uint8> t_scratch;
	// Separate buffers: the decoded source and the encoded result cannot share
	// storage with the scaler's destination.
	thread_local std::vector<uint8> t_decoded;
	thread_local std::vector<uint8> t_encoded;

	uint32 s_scaledCount = 0;
	uint32 s_skippedCount = 0;
	uint32 s_statsFrames = 0;

	bool IsUncompressedRGBA(Latte::E_GX2SURFFMT format)
	{
		return format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM ||
			format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB;
	}

	// BC1 and BC3 only. Those two cover the overwhelming majority of a game's
	// surface textures - in a Breath of the Wild dump, BC1_UNORM, BC1_SRGB and
	// BC3_SRGB together were the top three formats by a wide margin. BC4/BC5
	// are single and two channel formats used for masks and normal maps, where
	// re-encoding through an RGB path would corrupt the channel meaning, so
	// they are deliberately left alone.
	bool IsBC1(Latte::E_GX2SURFFMT format)
	{
		return format == Latte::E_GX2SURFFMT::BC1_UNORM || format == Latte::E_GX2SURFFMT::BC1_SRGB;
	}
	bool IsBC3(Latte::E_GX2SURFFMT format)
	{
		return format == Latte::E_GX2SURFFMT::BC3_UNORM || format == Latte::E_GX2SURFFMT::BC3_SRGB;
	}

	bool IsScalableFormat(Latte::E_GX2SURFFMT format)
	{
		return IsUncompressedRGBA(format) || IsBC1(format) || IsBC3(format);
	}

	// Catmull-Rom, evaluated per axis. Chosen over bilinear because bilinear is
	// exactly what the GPU already does for free at sample time - reproducing it
	// here would cost CPU for no change in the image. Catmull-Rom is a wider,
	// mildly sharpening reconstruction, which is the only reason doing this on
	// the CPU can beat leaving it to the hardware.
	inline float CatmullRom(float x)
	{
		x = fabsf(x);
		if (x < 1.0f)
			return 1.5f * x * x * x - 2.5f * x * x + 1.0f;
		if (x < 2.0f)
			return -0.5f * x * x * x + 2.5f * x * x - 4.0f * x + 2.0f;
		return 0.0f;
	}

	// Splits a row range across the idle cores and waits for all of it.
	//
	// Deliberately NOT "move the work to a worker thread". The upload needs the
	// scaled result, so handing the serial work to one other thread and
	// blocking on it only changes which thread waits - the stall is the same
	// length. What shortens it is doing the rows at the same time, and there is
	// headroom for that: the process was measured using 20 percent of 16
	// hardware threads while one PPC core sat at 98 percent.
	//
	// Threads are created per call rather than pooled. At ~50us of creation
	// against tens of milliseconds of work this does not show up, and a pool
	// would mean owning thread lifetime across emulator shutdown for no
	// measurable gain.
	template<typename TFunc>
	void AnubisParallelFor(sint32 count, TFunc&& body)
	{
		if (count <= 0)
			return;
		// One core is left for the emulator itself, which is the thing actually
		// bottlenecking the frame - taking every core would trade texture
		// stutter for frame stutter.
		static const sint32 maxThreads = std::max<sint32>(1, (sint32)std::thread::hardware_concurrency() - 1);
		const sint32 threadCount = std::min(maxThreads, count);
		if (threadCount <= 1)
		{
			for (sint32 i = 0; i < count; i++)
				body(i);
			return;
		}
		std::vector<std::thread> workers;
		workers.reserve(threadCount - 1);
		// Interleaved rather than chunked: block rows are not equally
		// expensive (a flat row encodes faster than a detailed one), so
		// striding keeps the threads from finishing at wildly different times.
		for (sint32 t = 1; t < threadCount; t++)
		{
			workers.emplace_back([&, t]() {
				for (sint32 i = t; i < count; i += threadCount)
					body(i);
			});
		}
		for (sint32 i = 0; i < count; i += threadCount)
			body(i);
		for (auto& w : workers)
			w.join();
	}

	inline const uint8* SampleClamped(const uint8* src, sint32 srcPitchBytes, sint32 w, sint32 h, sint32 x, sint32 y)
	{
		x = std::clamp(x, 0, w - 1);
		y = std::clamp(y, 0, h - 1);
		return src + (size_t)y * srcPitchBytes + (size_t)x * 4;
	}

	// ======================= BC decode / encode =========================
	//
	// Compressed textures cannot be scaled in place: the blocks have to be
	// decoded to pixels, scaled, and re-encoded. Re-encoding is not optional -
	// keeping the scaled result uncompressed would cost about 32x the bytes of
	// the BC1 it came from, which no amount of VRAM makes sensible. Encoding
	// back to the SAME format keeps the cost at exactly the square of the
	// scale factor.

	// Decoders already exist in LatteTextureLoader.cpp, operating on one 4x4
	// block and writing 16 RGBA texels as floats in 0..1. Declared here rather
	// than reimplemented - they handle the format's corner cases (BC1's
	// 3-colour-plus-transparent mode among them) and are already exercised by
	// the texture dumper.
	void DecodeBlocksToRGBA(const uint8* src, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, std::vector<uint8>& out);

	// --- BC1 ---
	//
	// 8 bytes per 4x4 block: two RGB565 endpoints then 2 bits of palette index
	// per texel. With endpoint0 > endpoint1 the palette is the two endpoints
	// plus two interpolated thirds, which is the opaque 4-colour mode used
	// here. The alternative 3-colour mode trades one palette entry for
	// transparency and is not useful when re-encoding opaque colour.
	inline uint16 PackRGB565(sint32 r, sint32 g, sint32 b)
	{
		r = std::clamp(r, 0, 255);
		g = std::clamp(g, 0, 255);
		b = std::clamp(b, 0, 255);
		return (uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}
	inline void UnpackRGB565(uint16 c, sint32& r, sint32& g, sint32& b)
	{
		// Replicating the high bits into the low ones, which is what the
		// hardware does - without it the decoded endpoints sit slightly dark
		// and every block drifts.
		const sint32 r5 = (c >> 11) & 0x1F;
		const sint32 g6 = (c >> 5) & 0x3F;
		const sint32 b5 = c & 0x1F;
		r = (r5 << 3) | (r5 >> 2);
		g = (g6 << 2) | (g6 >> 4);
		b = (b5 << 3) | (b5 >> 2);
	}

	// allowTransparency must be FALSE for BC3's colour block: there the alpha
	// lives in its own 8-byte block, and a colour block in 3-colour mode would
	// make the renderer treat index 3 as transparent on top of that - double
	// alpha, and the texture comes out with holes.
	void EncodeBC1Block(const uint8* rgba16, uint8* out, bool allowTransparency)
	{
		// Endpoints from the block's colour bounding box. This is the fast
		// path every practical encoder starts from: the two corners of the box
		// bracket the block's colours, so the interpolated palette spans them.
		// A full principal-axis fit would be marginally better on blocks whose
		// colours lie along a diagonal, at several times the cost - and this
		// runs on the texture load path of a CPU-bound emulator.
		sint32 minR = 255, minG = 255, minB = 255;
		sint32 maxR = 0, maxG = 0, maxB = 0;
		for (sint32 i = 0; i < 16; i++)
		{
			const sint32 r = rgba16[i * 4 + 0];
			const sint32 g = rgba16[i * 4 + 1];
			const sint32 b = rgba16[i * 4 + 2];
			minR = std::min(minR, r); maxR = std::max(maxR, r);
			minG = std::min(minG, g); maxG = std::max(maxG, g);
			minB = std::min(minB, b); maxB = std::max(maxB, b);
		}
		// Inset the box by 1/16th of its size. The endpoints are themselves
		// palette entries, so placing them exactly on the extremes wastes half
		// a quantization step at each end; pulling them in slightly measurably
		// reduces error on smooth gradients.
		const sint32 insetR = (maxR - minR) >> 4;
		const sint32 insetG = (maxG - minG) >> 4;
		const sint32 insetB = (maxB - minB) >> 4;
		minR += insetR; maxR -= insetR;
		minG += insetG; maxG -= insetG;
		minB += insetB; maxB -= insetB;

		// Does this block carry BC1's one-bit alpha?
		//
		// BC1 has two modes, chosen by the endpoint ordering. With c0 > c1 the
		// palette is 4 opaque colours; with c0 <= c1 it is 3 colours plus a
		// fully transparent entry. The first version of this encoder always
		// produced the opaque mode, which silently threw the transparency away -
		// that is what made waterfall water lose its transparency, smoke read as
		// solid blocks, and some textures come out black: every cut-out texel
		// became opaque colour instead of nothing.
		bool hasTransparent = false;
		if (allowTransparency)
		{
			for (sint32 i = 0; i < 16; i++)
			{
				if (rgba16[i * 4 + 3] < 128)
				{
					hasTransparent = true;
					break;
				}
			}
		}

		uint16 c0 = PackRGB565(maxR, maxG, maxB);
		uint16 c1 = PackRGB565(minR, minG, minB);
		const sint32 paletteSize = hasTransparent ? 3 : 4;
		if (hasTransparent)
		{
			// 3-colour + transparent mode needs c0 <= c1. Index 3 is then the
			// transparent entry.
			if (c0 > c1)
				std::swap(c0, c1);
		}
		else
		{
			// 4-colour mode requires c0 > c1. Equal endpoints mean a flat block
			// and either ordering yields the same single colour.
			if (c0 < c1)
				std::swap(c0, c1);
		}

		sint32 pr[4], pg[4], pb[4];
		UnpackRGB565(c0, pr[0], pg[0], pb[0]);
		UnpackRGB565(c1, pr[1], pg[1], pb[1]);
		if (hasTransparent)
		{
			// One interpolated entry at the midpoint, per the 3-colour mode.
			pr[2] = (pr[0] + pr[1]) / 2;
			pg[2] = (pg[0] + pg[1]) / 2;
			pb[2] = (pb[0] + pb[1]) / 2;
		}
		else
		{
			// Two entries at one third and two thirds between the endpoints.
			pr[2] = (2 * pr[0] + pr[1]) / 3;
			pg[2] = (2 * pg[0] + pg[1]) / 3;
			pb[2] = (2 * pb[0] + pb[1]) / 3;
			pr[3] = (pr[0] + 2 * pr[1]) / 3;
			pg[3] = (pg[0] + 2 * pg[1]) / 3;
			pb[3] = (pb[0] + 2 * pb[1]) / 3;
		}

		uint32 indices = 0;
		for (sint32 i = 0; i < 16; i++)
		{
			// In 3-colour mode a texel below the alpha threshold must take
			// index 3, the transparent entry - its colour is irrelevant.
			if (hasTransparent && rgba16[i * 4 + 3] < 128)
			{
				indices |= 3u << (i * 2);
				continue;
			}
			const sint32 r = rgba16[i * 4 + 0];
			const sint32 g = rgba16[i * 4 + 1];
			const sint32 b = rgba16[i * 4 + 2];
			sint32 best = 0;
			sint32 bestErr = INT32_MAX;
			for (sint32 p = 0; p < paletteSize; p++)
			{
				const sint32 dr = r - pr[p];
				const sint32 dg = g - pg[p];
				const sint32 db = b - pb[p];
				// Weighted by luma sensitivity rather than a plain euclidean
				// distance: an error in green is far more visible than the
				// same error in blue.
				const sint32 err = dr * dr * 3 + dg * dg * 6 + db * db;
				if (err < bestErr)
				{
					bestErr = err;
					best = p;
				}
			}
			indices |= ((uint32)best) << (i * 2);
		}

		*(uint16*)(out + 0) = c0;
		*(uint16*)(out + 2) = c1;
		*(uint32*)(out + 4) = indices;
	}

	// --- BC3 ---
	//
	// 16 bytes per block: an 8-byte alpha block followed by a BC1-style colour
	// block. The alpha block is two endpoints plus 3 bits of index per texel,
	// so alpha gets 8 levels where colour gets 4 - which is why BC3 is the
	// format games use when alpha carries real gradients.
	void EncodeBC3AlphaBlock(const uint8* rgba16, uint8* out)
	{
		sint32 minA = 255, maxA = 0;
		for (sint32 i = 0; i < 16; i++)
		{
			const sint32 a = rgba16[i * 4 + 3];
			minA = std::min(minA, a);
			maxA = std::max(maxA, a);
		}
		out[0] = (uint8)maxA;
		out[1] = (uint8)minA;
		// With a0 > a1 the palette is the two endpoints plus six interpolated
		// steps. Equal endpoints mean constant alpha, and every index then
		// resolves to the same value, so the indices can be left at zero.
		uint8 palette[8];
		if (maxA > minA)
		{
			palette[0] = (uint8)maxA;
			palette[1] = (uint8)minA;
			for (sint32 k = 1; k <= 6; k++)
				palette[1 + k] = (uint8)(((7 - k) * maxA + k * minA) / 7);
		}
		else
		{
			for (sint32 k = 0; k < 8; k++)
				palette[k] = (uint8)maxA;
		}

		uint64 indices = 0;
		for (sint32 i = 0; i < 16; i++)
		{
			const sint32 a = rgba16[i * 4 + 3];
			sint32 best = 0, bestErr = INT32_MAX;
			for (sint32 p = 0; p < 8; p++)
			{
				const sint32 err = abs(a - (sint32)palette[p]);
				if (err < bestErr)
				{
					bestErr = err;
					best = p;
				}
			}
			indices |= ((uint64)best) << (i * 3);
		}
		for (sint32 i = 0; i < 6; i++)
			out[2 + i] = (uint8)((indices >> (i * 8)) & 0xFF);
	}

	void EncodeRGBAToBC(const uint8* rgba, sint32 width, sint32 height, Latte::E_GX2SURFFMT format, std::vector<uint8>& out)
	{
		const bool bc3 = IsBC3(format);
		const sint32 blocksX = (width + 3) / 4;
		const sint32 blocksY = (height + 3) / 4;
		const sint32 blockBytes = bc3 ? 16 : 8;
		out.resize((size_t)blocksX * blocksY * blockBytes);

		// Parallel across block ROWS. Moving this work to one other thread would
		// not help - the upload needs the result, so something still waits for it.
		// What helps is using the cores that are already idle: the process was
		// measured using 20 percent of 16 hardware threads, with one PPC core
		// saturated. Rows are independent, so there is no sharing to guard.
		AnubisParallelFor(blocksY, [&](sint32 by)
		{
			uint8 blockPixels[16 * 4];
			for (sint32 bx = 0; bx < blocksX; bx++)
			{
				// Gather the 4x4, clamping at the edges. Scaled dimensions are
				// a whole multiple of the source's, which is already a multiple
				// of 4 for a compressed texture, so this only ever clamps on a
				// malformed input.
				for (sint32 py = 0; py < 4; py++)
				{
					const sint32 sy = std::min(by * 4 + py, height - 1);
					for (sint32 px = 0; px < 4; px++)
					{
						const sint32 sx = std::min(bx * 4 + px, width - 1);
						const uint8* s = rgba + ((size_t)sy * width + sx) * 4;
						uint8* d = blockPixels + (py * 4 + px) * 4;
						d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
					}
				}
				uint8* dstBlock = out.data() + ((size_t)by * blocksX + bx) * blockBytes;
				if (bc3)
				{
					EncodeBC3AlphaBlock(blockPixels, dstBlock);
					EncodeBC1Block(blockPixels, dstBlock + 8, false);
				}
				else
				{
					EncodeBC1Block(blockPixels, dstBlock, true);
				}
			}
		});
	}
}

// Declared from LatteTextureLoader.cpp - see DecodeBlocksToRGBA's comment.
void decodeBC1Block(uint8* inputData, float* output4x4RGBA);
void decodeBC3Block_UNORM(uint8* inputData, float* imageRGBA);

namespace
{
	void DecodeBlocksToRGBA(const uint8* src, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, std::vector<uint8>& out)
	{
		const bool bc3 = IsBC3(format);
		const sint32 blocksX = (width + 3) / 4;
		const sint32 blocksY = (height + 3) / 4;
		const sint32 blockBytes = bc3 ? 16 : 8;
		out.resize((size_t)width * height * 4);

		float blockF[16 * 4];
		for (sint32 by = 0; by < blocksY; by++)
		{
			for (sint32 bx = 0; bx < blocksX; bx++)
			{
				uint8* srcBlock = (uint8*)(src + ((size_t)by * blocksX + bx) * blockBytes);
				if (bc3)
					decodeBC3Block_UNORM(srcBlock, blockF);
				else
					decodeBC1Block(srcBlock, blockF);
				for (sint32 py = 0; py < 4; py++)
				{
					const sint32 dy = by * 4 + py;
					if (dy >= height)
						break;
					for (sint32 px = 0; px < 4; px++)
					{
						const sint32 dx = bx * 4 + px;
						if (dx >= width)
							break;
						const float* s = blockF + (py * 4 + px) * 4;
						uint8* d = out.data() + ((size_t)dy * width + dx) * 4;
						for (sint32 c = 0; c < 4; c++)
							d[c] = (uint8)std::clamp(s[c] * 255.0f + 0.5f, 0.0f, 255.0f);
					}
				}
			}
		}
	}
}

sint32 LatteTexScale_GetFactor()
{
	const sint32 f = GetConfig().texture_scaling_factor;
	if (f < 2)
		return 1;
	return std::min(f, 4);
}

bool LatteTexScale_ShouldScale(Latte::E_GX2SURFFMT format, bool isDepth, sint32 mipLevels, Latte::E_HWTILEMODE tileMode, sint32 width, sint32 height, sint32& outWidth, sint32& outHeight)
{
	const sint32 factor = LatteTexScale_GetFactor();
	if (factor <= 1)
		return false;
	// Depth and stencil surfaces are read as data, not displayed. Scaling them
	// would break every effect that samples depth, SSAO and GI included.
	if (isDepth)
		return false;
	if (!IsScalableFormat(format))
		return false;
	if (width < kMinSideToScale || height < kMinSideToScale)
		return false;

	// Requires a real mip chain. This is the guard that keeps the in-game map
	// from vanishing on zoom: textures the GPU renders into are created
	// without mips, and overriding their resolution means the game's own draws
	// into them no longer match the allocation. Asset textures - terrain,
	// characters, materials, the ones worth scaling - ship with mips almost
	// without exception. In a Breath of the Wild dump 1460 of 2284 textures had
	// a chain and 824 did not, and the dynamic ones are in that second group.
	if (mipLevels <= 1)
		return false;

	// Linear-tiled textures get mirrored back to CPU RAM (see enableReadback in
	// LatteTexture): the game reads them, so the size it finds has to be the
	// size it wrote.
	if (tileMode == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
		return false;

	const sint32 scaledW = width * factor;
	const sint32 scaledH = height * factor;
	// Upper bound from the setting, so the same build works on an 8 GB card and
	// a 24 GB one without the budget being baked in. A texture whose scaled
	// size would exceed the cap is left at its original resolution rather than
	// scaled by a smaller factor - a mixed-factor texture set reads as
	// inconsistent sharpness across one surface.
	const sint32 cap = GetConfig().texture_scaling_max_size;
	if (cap > 0 && (scaledW > cap || scaledH > cap))
		return false;

	outWidth = scaledW;
	outHeight = scaledH;
	return true;
}

const uint8* LatteTexScale_ScaleSliceTo(const void* srcPixels, Latte::E_GX2SURFFMT format, sint32 srcWidth, sint32 srcHeight, sint32 srcPitch, sint32 dstWidth, sint32 dstHeight, size_t& outSizeBytes)
{
	if (!srcPixels || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0)
	{
		s_skippedCount++;
		return nullptr;
	}
	if (!IsScalableFormat(format))
	{
		s_skippedCount++;
		return nullptr;
	}
	// Compressed output is written as whole 4x4 blocks, so a target that is not
	// a multiple of 4 cannot be represented. Declining is correct here: the
	// caller falls back to leaving the mip as the game supplied it.
	const bool compressedOut = IsBC1(format) || IsBC3(format);
	if (compressedOut && ((dstWidth & 3) || (dstHeight & 3)))
	{
		s_skippedCount++;
		return nullptr;
	}
	const sint32 scaledW = dstWidth;
	const sint32 scaledH = dstHeight;

	const uint8* src = (const uint8*)srcPixels;
	sint32 srcPitchBytes;
	// Compressed input is decoded to RGBA first, and the scaler below then
	// works on that in exactly the same way as a natively uncompressed
	// texture. Decoding into its own buffer rather than reusing the scratch,
	// because the scratch is the scaler's destination.
	const bool compressed = IsBC1(format) || IsBC3(format);
	if (compressed)
	{
		DecodeBlocksToRGBA(src, format, srcWidth, srcHeight, t_decoded);
		src = t_decoded.data();
		srcPitchBytes = srcWidth * 4;
	}
	else
	{
		// srcPitch arrives in texels for uncompressed formats; the loader's own
		// upload uses it the same way.
		srcPitchBytes = srcPitch > 0 ? srcPitch * 4 : srcWidth * 4;
	}

	const size_t dstSize = (size_t)scaledW * (size_t)scaledH * 4;
	t_scratch.resize(dstSize);
	uint8* dst = t_scratch.data();

	const float invScaleX = (float)srcWidth / (float)scaledW;
	const float invScaleY = (float)srcHeight / (float)scaledH;

	// Same reasoning as the encode loop: destination rows are independent.
	AnubisParallelFor(scaledH, [&](sint32 dy)
	{
		// Centre of the destination pixel mapped back into source space.
		const float sy = ((float)dy + 0.5f) * invScaleY - 0.5f;
		const sint32 iy = (sint32)floorf(sy);
		const float fy = sy - (float)iy;
		float wy[4];
		for (sint32 k = 0; k < 4; k++)
			wy[k] = CatmullRom((float)(k - 1) - fy);

		uint8* dstRow = dst + (size_t)dy * scaledW * 4;
		for (sint32 dx = 0; dx < scaledW; dx++)
		{
			const float sx = ((float)dx + 0.5f) * invScaleX - 0.5f;
			const sint32 ix = (sint32)floorf(sx);
			const float fx = sx - (float)ix;
			float wx[4];
			for (sint32 k = 0; k < 4; k++)
				wx[k] = CatmullRom((float)(k - 1) - fx);

			float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			float wsum = 0.0f;
			for (sint32 ky = 0; ky < 4; ky++)
			{
				if (wy[ky] == 0.0f)
					continue;
				for (sint32 kx = 0; kx < 4; kx++)
				{
					const float w = wx[kx] * wy[ky];
					if (w == 0.0f)
						continue;
					const uint8* s = SampleClamped(src, srcPitchBytes, srcWidth, srcHeight, ix + kx - 1, iy + ky - 1);
					acc[0] += s[0] * w;
					acc[1] += s[1] * w;
					acc[2] += s[2] * w;
					acc[3] += s[3] * w;
					wsum += w;
				}
			}
			// Catmull-Rom weights are signed, so the result can overshoot the
			// 0-255 range on a hard edge. Clamping is what keeps that from
			// wrapping around into the opposite colour.
			uint8* o = dstRow + (size_t)dx * 4;
			const float inv = wsum != 0.0f ? 1.0f / wsum : 0.0f;
			for (sint32 c = 0; c < 4; c++)
				o[c] = (uint8)std::clamp(acc[c] * inv + 0.5f, 0.0f, 255.0f);
		}
	});

	s_scaledCount++;

	// Re-encode to the format the texture started in. Without this step the
	// result would go up as RGBA8 at the scaled size - about 32x the bytes of
	// the BC1 it came from - and the VRAM cost would make the feature
	// unusable at any factor. Encoding back keeps it at exactly factor^2.
	if (compressed)
	{
		EncodeRGBAToBC(dst, scaledW, scaledH, format, t_encoded);
		outSizeBytes = t_encoded.size();

		// Round-trip check on the first few textures, then never again.
		//
		// Added because the counters said 12345 textures were being scaled
		// "successfully" while the ground rendered black - so the question was
		// never whether the path ran, it was whether the bytes it produced mean
		// what the GPU will read. Decoding the result back and comparing it to
		// what went in answers that directly, instead of inferring an encoder
		// bug from what the screen looks like.
		//
		// A mean error in the low single digits is normal for BC. Anything
		// above about 40 means the encoder is wrong, and an error near 128
		// with a near-zero alpha mean means it is emitting transparent blocks.
		// Only textures whose input actually varies are worth measuring.
		//
		// The first run of this reported a mean error of exactly 0.0 six times,
		// which looked like a perfect encoder and is impossible - BC is lossy.
		// What it actually meant was that the input was flat: at startup the
		// game has not written its textures yet, so Cemu loads whatever is in
		// that memory, and encoding a uniform block round-trips exactly. The
		// measurement was taken too early to say anything.
		static uint32 s_verified = 0;
		bool inputVaries = false;
		if (s_verified < 6)
		{
			const size_t probeCount = (size_t)scaledW * scaledH;
			const uint8 first = dst[0];
			for (size_t i = 1; i < probeCount; i += 97) // sparse probe, enough to spot a flat buffer
			{
				if (dst[i * 4] != first)
				{
					inputVaries = true;
					break;
				}
			}
		}
		if (s_verified < 6 && inputVaries)
		{
			s_verified++;
			std::vector<uint8> roundTrip;
			DecodeBlocksToRGBA(t_encoded.data(), format, scaledW, scaledH, roundTrip);
			uint64 errSum = 0, alphaSum = 0;
			const size_t count = (size_t)scaledW * scaledH;
			for (size_t i = 0; i < count; i++)
			{
				for (sint32 c = 0; c < 3; c++)
					errSum += (uint64)abs((sint32)roundTrip[i * 4 + c] - (sint32)dst[i * 4 + c]);
				alphaSum += roundTrip[i * 4 + 3];
			}
			cemuLog_log(LogType::Force,
				"Anubis texscale verify #{}: fmt={:04x} {}x{} -> {}x{} | mean RGB error {:.1f} | mean alpha out {:.1f} (in {:.1f})",
				s_verified, (uint32)format, srcWidth, srcHeight, scaledW, scaledH,
				(double)errSum / (double)(count * 3), (double)alphaSum / (double)count,
				[&]{ uint64 a = 0; for (size_t i = 0; i < count; i++) a += dst[i * 4 + 3]; return (double)a / (double)count; }());
		}
		return t_encoded.data();
	}
	outSizeBytes = dstSize;
	return dst;
}

void LatteTexScale_LogStats()
{
	if (LatteTexScale_GetFactor() <= 1)
		return;
	if (++s_statsFrames < 600)
		return;
	s_statsFrames = 0;
	if (s_scaledCount == 0 && s_skippedCount == 0)
		return;
	cemuLog_log(LogType::Force, "Anubis texture scaling: {} scaled, {} left at original size since last report",
		s_scaledCount, s_skippedCount);
	s_scaledCount = 0;
	s_skippedCount = 0;
}
