/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ReadBtfTexture.h"

#include "Ensure.h"
#include "io/MaterialUtils.h"
#include "io/Reader.h"
#include "io/ReaderException.h"
#include "mdl/Palette.h"

#include <fmt/format.h>

#include <cassert>

namespace tb::io
{
namespace Btf
{
constexpr uint32_t BITEXTURE_MAGIC = (('F' << 24) + ('T' << 16) + ('I' << 8) + 'B');
constexpr uint32_t BITEXTURE_RGBA = (('A' << 24) + ('B' << 16) + ('G' << 8) + 'R');
constexpr int8_t BITEX_VER_MAJOR = 1;
constexpr int8_t BITEX_VER_MINOR = 0;
constexpr int32_t MAX_TEXTURE_NAME = 64; // 32 can probably fit, but this is safer.

// maintaining the same limit of named textures.
// +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 (sequence)
// -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 (random)
// +a +b +c +d +e +f +g +h +i +j (alternates)
constexpr int32_t MAX_TEXTURE_FRAMES = 10;
constexpr int32_t MAX_ALTERNATE_TEX = 10;
constexpr const char* BITEXTURE_EXT = ".btf";
} // namespace Btf

Result<mdl::Texture> readBtfTexture(Reader& reader)
{
  try
  {
    const uint32_t ident = reader.read<uint32_t, uint32_t>();
    if (ident != Btf::BITEXTURE_MAGIC)
      return Error("unknown btf identifier: " + std::to_string(ident));

    int16_t vmajor = reader.read<int16_t, int16_t>();
    int16_t vminor = reader.read<int16_t, int16_t>();
    if (vmajor != Btf::BITEX_VER_MAJOR && vminor != Btf::BITEX_VER_MINOR)
      return Error(fmt::format("unsupported btf version: {}.{}", vmajor, vminor));

	// this should not be int32. it only needs to be int16, or even a single byte.
    /* int16_t compressiontype = */reader.read<int16_t, int16_t>();
	/* int16_t anim_type = */reader.read<int16_t, int16_t>(); //0 = none, 1 = sequence, 2 = random

    size_t width = reader.readSize<int32_t>();
    size_t height = reader.readSize<int32_t>();

    const int32_t flags = reader.read<int32_t, int32_t>();
    const int32_t contents = reader.read<int32_t, int32_t>();
    const int32_t lightvalue = reader.read<int32_t, int32_t>();
    
    /* bool emissive =*/reader.read<int16_t, int16_t>();
    /* int16_t pad1 = */reader.read<int16_t, int16_t>();

    int16_t alternate_count = reader.read<int16_t, int16_t>();
    int16_t frame_count = reader.read<int16_t, int16_t>();
    if (frame_count <= 0)
      return Error(fmt::format("frames are missing? {}", frame_count));

    // skip over alternate's if they exist.
    for (int i = 0; i < alternate_count; i++)
      reader.readString(Btf::MAX_TEXTURE_NAME);

    // read the first frame; that's all we're populating; don't need animation info at the
    // moment.
    const uint32_t rgba_tag = reader.read<uint32_t, uint32_t>();
    if (rgba_tag != Btf::BITEXTURE_RGBA)
      return Error("invalid frame data: " + std::to_string(rgba_tag));
    
    const size_t numMips = 1;
    auto buffers = mdl::TextureBufferList{numMips};
    mdl::setMipBufferSize(buffers, numMips, width, height, GL_RGBA);
    reader.read(buffers[0].data(), buffers[0].size());

	static auto averageColor = Color{};
	auto embeddedDefaults = mdl::Q2EmbeddedDefaults{flags, contents, lightvalue};
    return mdl::Texture{
      width,
      height,
      averageColor,
      GL_RGBA,
      mdl::TextureMask::Off,
      std::move(embeddedDefaults),
      std::move(buffers)};
  }
  catch (const Exception& e)
  {
    return Error{e.what()};
  }
}
} // namespace tb::io
