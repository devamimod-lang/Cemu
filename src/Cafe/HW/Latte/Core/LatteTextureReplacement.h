#pragma once

#include "Cafe/HW/Latte/ISA/LatteReg.h"

// Anubis: replacement textures loaded from disk, so a game texture can be
// swapped for a higher resolution version produced offline.
//
// Why offline rather than at runtime: upscaling a 512x512 texture to 1024x1024
// with any filter adds no information - the detail was never there. Real gains
// come from a neural upscaler run ahead of time, which invents plausible
// detail an interpolating filter cannot. The emulator's job is only to
// recognise the texture and load the better version.
//
// The lookup key is deliberately the SAME key the existing texture dumper
// writes into its filenames (Debug -> Dump -> Textures), so the workflow has no
// rename step: dump, upscale the files, drop them in, done.
//
//   {physAddr:08x}_fmt{format:04x}_slice{N}_mip{NN}_{W}x{H}_tm{NN}
//
// That key includes the physical address, which is not a content identity - if
// the game loads an asset to a different address the entry misses. That is a
// known limitation of matching the dump scheme, and it is why
// LatteTexReplace_LogStats reports the hit rate: whether addresses are stable
// enough in practice is an empirical question, not something to assume either
// way. A content hash would be robust, but the hash this codebase already has
// is a cheap partial one built for change detection (32 samples on compressed
// formats), so it cannot serve as an identity, and computing a full one per
// texture load would add CPU work to a workload already measured as CPU-bound.

struct LatteTexReplacementData
{
	sint32 width{};
	sint32 height{};
	// Decoded RGBA8 when the source was a TGA, or raw block data when it was a
	// DDS holding a compressed format.
	std::vector<uint8> data;
	// VK_FORMAT_UNDEFINED equivalent is expressed as "not compressed": when
	// this is false, data is RGBA8 and the renderer uploads it uncompressed.
	bool isCompressed{};
	// Only meaningful when isCompressed - the DXGI/FourCC format read from the
	// DDS header, so the renderer can pick a matching host format.
	uint32 compressedFourCC{};
};

// Scans the pack directory for the current title and builds the index. Cheap to
// call when replacement is disabled (returns immediately).
void LatteTexReplace_Init();
void LatteTexReplace_Shutdown();

// True when an entry exists for this texture's slice 0 / mip 0. Used at texture
// creation time to decide whether to override the host resolution, which has to
// happen before any slice is uploaded.
bool LatteTexReplace_HasEntry(MPTR physAddr, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, sint32& outWidth, sint32& outHeight);

// Loads the actual pixels for one slice/mip. Returns nullptr when there is no
// entry, which is the common case and must stay cheap.
const LatteTexReplacementData* LatteTexReplace_Get(MPTR physAddr, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, sint32 sliceIndex, sint32 mipIndex);

// Periodic hit/miss report. Exists because a replacement pack that silently
// matches nothing looks exactly like one that is working but subtle, and the
// address-based key makes that a real possibility.
void LatteTexReplace_LogStats();
