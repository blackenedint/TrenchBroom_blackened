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
constexpr uint32_t BTF_IDENT = (('F' << 24) + ('T' << 16) + ('I' << 8) + 'B');
constexpr uint32_t BTF_FRAMEID = (('M' << 24) + ('A' << 16) + ('R' << 8) + 'F');
constexpr int8_t BTF_VER_MAJOR = 1;
constexpr int8_t BTF_VER_MINOR = 0;

// maintaining the same limit of named textures.
// +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 (sequence)
// -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 (random)
// +a +b +c +d +e +f +g +h +i +j (alternates)
constexpr size_t MAX_TEXTURE_FRAMES = 10;
constexpr size_t MAX_ALTERNATE_TEX = 10;
constexpr const char* BITEXTURE_EXT = "btf";

constexpr size_t SHA1_BUFFER_SIZE = 20;
constexpr size_t MAX_TEXTURE_NAME = 64; // 32 can probably fit, but this is safer.

enum btf_compression
{
  None = 0,

  // FUTURE (TBD)
  DXT1,
  DXT5,
  BC4,
  BC5
};
// currently only RGBA is written
// but will figure this out later.
enum btf_format
{
  RGBA = 0,

  // FUTURE (TBD)
  RGB,
  ARGB,
};

enum btf_anim_type
{
  Anim_None = 0,
  Anim_Sequence,
  Anim_Random
};


// bitexture header; only identifier and version info.
struct header_t
{
  uint32_t ident;    // 4
  int16_t ver_major; // 2
  int16_t ver_minor; // 2
  //					//8
};

struct texinfo_t
{
  int32_t width;
  int32_t height;

  int16_t compressiontype;
  int16_t format;

  int16_t animType;
  int16_t frame_count;

  int32_t metadatasize;
  uint32_t metadatatype;
  //int8_t reserved[44]; // 44 bytes reserved.
};

// frames... shouldn't need to change;
// but there are 36 bytes available to decrement from as necessary.
struct frame_t
{
  uint32_t ident;
  std::string sha1; //SHA1_BUFFER_SIZE;
  //int8_t reserved[36];	//36 bytes reserved.
};

// metadata for quake2/vigil7
struct metadata_q2_t
{
  int32_t surfaceflags;
  int32_t contents;
  float value;
  int16_t emissive;
  int16_t alternate_count;
};
constexpr uint32_t BTF_METAQ2 = (('A' << 24) + ('T' << 16) + ('M' << 8) + 'Q'); // QMTA
} // namespace Btf

Result<mdl::Texture> readBtfTexture(Reader& reader)
{
  try
  {
    Btf::header_t hdr{};
    hdr.ident = reader.read<uint32_t, uint32_t>();
    if (hdr.ident != Btf::BTF_IDENT)
      return Error("unknown btf identifier: " + std::to_string(hdr.ident));

    hdr.ver_major = reader.read<int16_t, int16_t>();
    hdr.ver_minor = reader.read<int16_t, int16_t>();
    if (hdr.ver_major != Btf::BTF_VER_MAJOR && hdr.ver_minor != Btf::BTF_VER_MINOR)
      return Error(
        fmt::format("unsupported btf version: {}.{}", hdr.ver_major, hdr.ver_minor));

    Btf::texinfo_t tnfo{};
    tnfo.width = reader.read<int32_t, int32_t>();
    tnfo.height = reader.read<int32_t, int32_t>();
    tnfo.compressiontype = reader.read<int16_t, int16_t>();
    tnfo.format = reader.read<int16_t, int16_t>();
    tnfo.animType = reader.read<int16_t, int16_t>(); // 0 = none, 1 = sequence, 2 = random
    tnfo.frame_count = reader.read<int16_t, int16_t>();
    if (tnfo.frame_count <= 0)
      return Error(fmt::format("frames are missing? {}", tnfo.frame_count));

    tnfo.metadatasize = reader.read<int32_t, int32_t>();
    tnfo.metadatatype = reader.read<uint32_t, uint32_t>();
    // skip over the 44 reserved bytes.
    reader.readVec<int8_t, 44>();

    Btf::metadata_q2_t meta_q2{};
    if (tnfo.metadatasize > 0 && tnfo.metadatatype == Btf::BTF_METAQ2)
    {
      meta_q2.surfaceflags = reader.read<int32_t, int32_t>();
      meta_q2.contents = reader.read<int32_t, int32_t>();
      meta_q2.value = reader.readFloat<float>();
      meta_q2.emissive = reader.read<int16_t, int16_t>();
      meta_q2.alternate_count = reader.read<int16_t, int16_t>();

      // skip over the alternates; we don't need that information
      // future; display it somewhere for the sake of entity configuration?
      // like adding a "toggleable" display?
      for (int i = 0; i < meta_q2.alternate_count; i++)
        reader.readString(Btf::MAX_TEXTURE_NAME);
    }

    const int32_t flags = meta_q2.surfaceflags;
    const int32_t contents = meta_q2.contents;
    const int32_t lightvalue = static_cast<int32_t>(meta_q2.value);

    // read the first frame; that's all we're populating; don't need animation info at the

    Btf::frame_t frame{};
    frame.ident = reader.read<uint32_t, uint32_t>();
    if (frame.ident != Btf::BTF_FRAMEID)
      return Error("invalid frame data: " + std::to_string(frame.ident));

    // skip sha1 for now.
    frame.sha1 = reader.readString(Btf::SHA1_BUFFER_SIZE);
    // skip over the 36 reserved bytes.
    reader.readVec<int8_t, 36>();

    const size_t width = static_cast<size_t>(tnfo.width);
    const size_t height = static_cast<size_t>(tnfo.height);

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
