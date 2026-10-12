#pragma once

#include "Cafe/HW/Latte/ISA/LatteReg.h"

// Anubis: real-time texture upscaling.
//
// Scales a game texture as it is loaded, so players get sharper textures with
// nothing to download. This is the same feature PPSSPP and Dolphin ship as
// "texture scaling".
//
// What it can and cannot do, stated plainly because it sets the ceiling:
// upscaling a 128x128 texture to 256x256 invents no new detail - the
// information was never in the file. What it DOES buy is a better
// reconstruction than the one the GPU performs for free. The hardware
// magnifies with a 2x2 bilinear tent at sample time; doing a wider, better
// filter once at load and handing the GPU denser data to tent over is visibly
// sharper on magnified surfaces. That is the whole of the gain. A downloadable
// pack upscaled offline by a neural network will always beat this, because a
// network can hallucinate plausible detail and a filter cannot - see
// LatteTextureReplacement.h for that route.
//
// Cost, which is not small: the work lands on the texture load path, and this
// emulator is CPU-bound (measured: one PPC core saturated at 98%, the GPU
// thread never waiting on the GPU). Expect hitches while a new area streams in.
//
// STAGE 1 handles uncompressed RGBA8 only. Compressed formats - which is most
// of a game's surface textures - need decode and re-encode around the scale
// step, and re-encode has to exist because an uncompressed 2x texture costs
// about 32x the bytes of the BC1 it came from. That is stage 2.

// Scale factor, 1 = disabled. Clamped to what the setting allows.
sint32 LatteTexScale_GetFactor();

// True when this texture should be scaled, and fills the scaled dimensions.
// Called at texture creation, before any slice is uploaded, because the host
// texture has to be created at the larger size.
bool LatteTexScale_ShouldScale(Latte::E_GX2SURFFMT format, bool isDepth, sint32 width, sint32 height, sint32& outWidth, sint32& outHeight);

// Produces the scaled pixels for one slice/mip. Returns nullptr when this
// texture is not eligible, which is the common case and stays cheap.
// The returned buffer is owned by this module and valid until the next call on
// the same thread.
const uint8* LatteTexScale_ScaleSlice(const void* srcPixels, Latte::E_GX2SURFFMT format, sint32 srcWidth, sint32 srcHeight, sint32 srcPitch, sint32& outWidth, sint32& outHeight, size_t& outSizeBytes);

void LatteTexScale_LogStats();
