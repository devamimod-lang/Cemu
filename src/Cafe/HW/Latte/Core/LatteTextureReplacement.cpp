#include "Cafe/HW/Latte/Core/LatteTextureReplacement.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/CafeSystem.h"
#include "config/CemuConfig.h"
#include "config/ActiveSettings.h"

#include <fstream>

namespace
{
	struct ReplacementEntry
	{
		fs::path path;
		// Filled on first use and kept: a pack is read once per session, and
		// re-reading from disk on every texture reload would stutter.
		LatteTexReplacementData data;
		bool loaded = false;
		bool loadFailed = false;
	};

	std::unordered_map<std::string, ReplacementEntry> s_entries;
	bool s_active = false;
	uint32 s_hits = 0;
	uint32 s_misses = 0;
	uint32 s_lastStatsFrame = 0;

	// Mirrors the dumper's own filename, minus the extension, so a dumped file
	// is already named correctly for use as a replacement.
	std::string MakeKey(MPTR physAddr, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, sint32 sliceIndex, sint32 mipIndex)
	{
		return fmt::format("{:08x}_fmt{:04x}_slice{:d}_mip{:02d}_{:d}x{:d}",
			physAddr, (uint32)format, sliceIndex, mipIndex, width, height);
	}

	// The dumper appends "_tm{tilemode}" as well, but the tile mode is a
	// property of how the game stored the texture, not of the image - a
	// replacement is just pixels. Stripping it means one file can serve a
	// texture whose tiling differs between runs, which does happen.
	std::string StripTileMode(const std::string& stem)
	{
		const size_t pos = stem.rfind("_tm");
		if (pos == std::string::npos)
			return stem;
		return stem.substr(0, pos);
	}

	bool LoadTga(const fs::path& path, LatteTexReplacementData& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		uint8 header[18];
		f.read((char*)header, sizeof(header));
		if (!f)
			return false;
		// Only the shape the project's own tga_write_rgba produces: type 2
		// (uncompressed true-colour), 32bpp. Anything else is rejected rather
		// than guessed at - a silently misread texture is worse than a miss.
		const uint8 imageType = header[2];
		const uint8 bpp = header[16];
		if (imageType != 2 || bpp != 32)
			return false;
		const sint32 width = (sint32)(header[12] | (header[13] << 8));
		const sint32 height = (sint32)(header[14] | (header[15] << 8));
		if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
			return false;
		const uint8 idLength = header[0];
		if (idLength)
			f.seekg(idLength, std::ios::cur);

		const size_t pixelCount = (size_t)width * (size_t)height;
		std::vector<uint8> raw(pixelCount * 4);
		f.read((char*)raw.data(), raw.size());
		if ((size_t)f.gcount() != raw.size())
			return false;

		out.width = width;
		out.height = height;
		out.isCompressed = false;
		out.compressedFourCC = 0;
		out.data.resize(raw.size());

		// TGA stores BGRA, and bottom-up unless bit 5 of the descriptor is set.
		const bool topDown = (header[17] & 0x20) != 0;
		for (sint32 y = 0; y < height; y++)
		{
			const sint32 srcRow = topDown ? y : (height - 1 - y);
			const uint8* src = raw.data() + (size_t)srcRow * width * 4;
			uint8* dst = out.data.data() + (size_t)y * width * 4;
			for (sint32 x = 0; x < width; x++)
			{
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = src[3];
				src += 4;
				dst += 4;
			}
		}
		return true;
	}

	bool LoadDds(const fs::path& path, LatteTexReplacementData& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		uint32 magic = 0;
		f.read((char*)&magic, 4);
		if (!f || magic != 0x20534444) // "DDS "
			return false;
		uint8 header[124];
		f.read((char*)header, sizeof(header));
		if (!f)
			return false;
		auto rd32 = [&](size_t off) -> uint32 {
			return (uint32)header[off] | ((uint32)header[off + 1] << 8) |
				((uint32)header[off + 2] << 16) | ((uint32)header[off + 3] << 24);
		};
		// Offsets are relative to the start of DDS_HEADER (after the magic).
		const uint32 height = rd32(8);
		const uint32 width = rd32(12);
		const uint32 pfFlags = rd32(76);
		const uint32 fourCC = rd32(80);
		if (width == 0 || height == 0 || width > 16384 || height > 16384)
			return false;
		constexpr uint32 DDPF_FOURCC = 0x4;
		if ((pfFlags & DDPF_FOURCC) == 0)
			return false; // uncompressed DDS not handled - use TGA for that
		// DX10 header carries the real format in an extra 20 bytes.
		if (fourCC == 0x30315844) // "DX10"
			f.seekg(20, std::ios::cur);

		// The rest of the file is the mip 0 block data (and any mips after it,
		// which are ignored - see the mip note in LatteTexReplace_Get).
		out.width = (sint32)width;
		out.height = (sint32)height;
		out.isCompressed = true;
		out.compressedFourCC = fourCC;
		const std::streampos dataStart = f.tellg();
		f.seekg(0, std::ios::end);
		const size_t remaining = (size_t)(f.tellg() - dataStart);
		if (remaining == 0)
			return false;
		f.seekg(dataStart);
		out.data.resize(remaining);
		f.read((char*)out.data.data(), out.data.size());
		return f.gcount() > 0;
	}
}

void LatteTexReplace_Init()
{
	s_entries.clear();
	s_hits = 0;
	s_misses = 0;
	s_active = false;
	if (!GetConfig().texture_replacement_enabled)
		return;

	// Per-title, because texture keys are only meaningful for the game they
	// were dumped from.
	const uint64 titleId = CafeSystem::GetForegroundTitleId();
	const fs::path packPath = ActiveSettings::GetUserDataPath("textures/{:016x}", titleId);
	std::error_code ec;
	if (!fs::is_directory(packPath, ec))
	{
		cemuLog_log(LogType::Force, "Anubis textures: replacement enabled but no pack at {}", _pathToUtf8(packPath));
		return;
	}

	for (const auto& it : fs::directory_iterator(packPath, ec))
	{
		if (ec || !it.is_regular_file(ec))
			continue;
		const fs::path& p = it.path();
		std::string ext = _pathToUtf8(p.extension());
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
		if (ext != ".tga" && ext != ".dds")
			continue;
		const std::string key = StripTileMode(_pathToUtf8(p.stem()));
		// First file wins on a duplicate key. Reporting it rather than
		// silently picking one, because a pack with both a .tga and a .dds for
		// the same texture is a mistake the author wants to know about.
		auto [entryIt, inserted] = s_entries.try_emplace(key, ReplacementEntry{p});
		if (!inserted)
			cemuLog_log(LogType::Force, "Anubis textures: duplicate entry for {}, keeping {}", key, _pathToUtf8(entryIt->second.path.filename()));
	}

	s_active = !s_entries.empty();
	cemuLog_log(LogType::Force, "Anubis textures: indexed {} replacement file(s) from {}", s_entries.size(), _pathToUtf8(packPath));
}

void LatteTexReplace_Shutdown()
{
	s_entries.clear();
	s_active = false;
}

bool LatteTexReplace_HasEntry(MPTR physAddr, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, sint32& outWidth, sint32& outHeight)
{
	if (!s_active)
		return false;
	const LatteTexReplacementData* data = LatteTexReplace_Get(physAddr, format, width, height, 0, 0);
	if (!data)
		return false;
	outWidth = data->width;
	outHeight = data->height;
	return true;
}

const LatteTexReplacementData* LatteTexReplace_Get(MPTR physAddr, Latte::E_GX2SURFFMT format, sint32 width, sint32 height, sint32 sliceIndex, sint32 mipIndex)
{
	if (!s_active)
		return nullptr;
	// Mips above 0 are not served. A replacement is a single higher-resolution
	// image; its mip chain has to be generated rather than read, and doing that
	// is a separate step from getting the base level on screen. Until then the
	// caller keeps the game's own mips, which is why this returns nullptr here
	// instead of pretending.
	if (mipIndex != 0)
		return nullptr;

	const std::string key = MakeKey(physAddr, format, width, height, sliceIndex, mipIndex);
	auto it = s_entries.find(key);
	if (it == s_entries.end())
	{
		s_misses++;
		return nullptr;
	}
	ReplacementEntry& entry = it->second;
	if (entry.loadFailed)
		return nullptr;
	if (!entry.loaded)
	{
		entry.loaded = true;
		std::string ext = _pathToUtf8(entry.path.extension());
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
		const bool ok = (ext == ".dds") ? LoadDds(entry.path, entry.data) : LoadTga(entry.path, entry.data);
		if (!ok)
		{
			entry.loadFailed = true;
			entry.data = {};
			cemuLog_log(LogType::Force, "Anubis textures: failed to read {}", _pathToUtf8(entry.path.filename()));
			return nullptr;
		}
	}
	s_hits++;
	return &entry.data;
}

void LatteTexReplace_LogStats()
{
	if (!s_active)
		return;
	// Once every 600 frames, matching the other Anubis counters.
	if (++s_lastStatsFrame < 600)
		return;
	s_lastStatsFrame = 0;
	cemuLog_log(LogType::Force, "Anubis textures: {} replacement hit(s), {} miss(es) since last report", s_hits, s_misses);
	s_hits = 0;
	s_misses = 0;
}
