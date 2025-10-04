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
// so I can copy and paste without having to keep altering.
using uint32 = uint32_t;
using int32 = int32_t;
using uint16 = uint16_t;
using int16 = int16_t;
using byte = int8_t;

namespace Btf
{
constexpr uint32 BTF_IDENT = (('F' << 24) + ('T' << 16) + ('I' << 8) + 'B');
constexpr uint32 BTF_FRAMEID = (('M' << 24) + ('A' << 16) + ('R' << 8) + 'F');
constexpr int16 BTF_VER_MAJOR = 1;
constexpr int16 BTF_VER_MINOR = 0;

constexpr uint32 Version(int16 major, int16 minor)
{
  return (uint32)(major * 100 + minor * 10);
}
constexpr uint32 HighestVersion()
{
  return Version(BTF_VER_MAJOR, BTF_VER_MINOR);
}

// maintaining the same limit of named textures.
// +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 (sequence)
// -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 (random)
// +a +b +c +d +e +f +g +h +i +j (alternates)
constexpr size_t MAX_TEXTURE_FRAMES = 10;
constexpr size_t MAX_ALTERNATE_TEX = 10;
constexpr const char* BITEXTURE_EXT = "btf";

constexpr size_t SHA1_BUFFER_SIZE = 20;
constexpr size_t MAX_TEXTURE_NAME = 64; // 32 can probably fit, but this is safer.

// Current known metadata types.
constexpr uint32 BTF_METAQ2 = (('A' << 24) + ('T' << 16) + ('M' << 8) + 'Q');  // QMTA
constexpr uint32 BTF_METASPR = (('T' << 24) + ('M' << 16) + ('P' << 8) + 'S'); // SPMT

enum ECompression
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
enum EFormat
{
  RGBA = 0,

  // FUTURE (TBD)
  RGB,
  ARGB,
};

enum EAnimType
{
  Anim_None = 0,
  Anim_Sequence,
  Anim_Random,
  Anim_Sprite,
};

#pragma pack(push, 1)
struct header_t
{
  uint32 ident;
  int16 ver_major;
  int16 ver_minor;
};

struct texinfo_t
{
  int32 width;
  int32 height;

  int16 compressiontype; // ECompression
  int16 format;          // EFormat
  int16 animType;        // EAnimType
  int16 frame_count;     // number of frames in texture

  int32 framedatasize;
  int32 framedataoffset;

  int32 metadatasize;
  int32 metadataoffset;
};
// frames... shouldn't need to change;
// but there are 36 bytes available to decrement from as necessary.
struct frame_t
{
  uint32_t ident;
  std::string sha1; // SHA1_BUFFER_SIZE;
  byte reserved[40];
};


struct metadata_t
{
  uint32 ident;
};

// metadata for quake2/vigil7
struct metadata_q2_t : public metadata_t
{
  // game surface flags;
  int32 surfaceflags;

  // game content flags
  int32 contents;

  // SURF_LIGHT value in Q2 (int32) ; changed to a float so I can re-purpose it.
  float value;

  // texture is emissive; alpha is mask
  int16 emissive;

  // surface type out of surfaces.txt
  char surfacetype[MAX_TEXTURE_NAME];

  // number of alternate texture names
  int16 alternate_count;
  // followed by alternate_count * char[btf::MAX_TEXTURE_NAME]
};

struct metadata_sprite_t : public metadata_t
{
  int32 orientation;
  int32 rendertype;
  // followed by frame_count * int32 (intervals)
};
#pragma pack(pop)

static_assert(sizeof(header_t) == 8, "header_t size");
static_assert(sizeof(texinfo_t) == 32, "texinfo_t size");
// static_assert(sizeof(frame_t) == 64, "frame_t should always be 64 bytes.");

static_assert(sizeof(metadata_q2_t) == 84, "metadata_q2_t size");
static_assert(sizeof(metadata_sprite_t) == 12, "metadata_sprite_t size");
} // namespace Btf

tb::Result<tb::mdl::Texture> readBtfTexture(
  Reader& reader, bool bVerticalFlip /*= false*/)
{
  try
  {
    Btf::header_t hdr{};
    hdr.ident = reader.read<uint32, uint32>();
    if (hdr.ident != Btf::BTF_IDENT)
      return Error("unknown btf identifier: " + std::to_string(hdr.ident));

    hdr.ver_major = reader.read<int16, int16>();
    hdr.ver_minor = reader.read<int16, int16>();
    if (Btf::Version(hdr.ver_major, hdr.ver_minor) > Btf::HighestVersion())
      return Error(
        fmt::format("unsupported btf version: {}.{}", hdr.ver_major, hdr.ver_minor));

    Btf::texinfo_t tnfo{};
    tnfo.width = reader.read<int32, int32>();
    tnfo.height = reader.read<int32, int32>();
    tnfo.compressiontype = reader.read<int16, int16>();
    tnfo.format = reader.read<int16, int16>();
    tnfo.animType = reader.read<int16, int16>(); // 0 = none, 1 = sequence, 2 = random
    tnfo.frame_count = reader.read<int16, int16>();
    if (tnfo.frame_count <= 0)
      return Error(fmt::format("frames are missing? {}", tnfo.frame_count));


    // use these in trenchbroom instead.
    size_t framedatasize = reader.readSize<int32>();
    size_t framedataoffset = reader.readSize<int32>();

    size_t metadatasize = reader.readSize<int32>();
    size_t metadataoffset = reader.readSize<int32>();

    if (framedatasize == 0)
      return Error("no framedata");


    // meh!
    Btf::metadata_q2_t meta_q2{};
    Btf::metadata_sprite_t meta_spr{};
    if (metadatasize > 0)
    {
      // read the metadata first; even though it's at the end.
      reader.seekFromBegin(metadataoffset);

      // read metadata type.
      uint32 metadatatype = reader.read<uint32, uint32>();
      switch (metadatatype)
      {
      case Btf::BTF_METAQ2: {
        meta_q2.surfaceflags = reader.read<int32_t, int32_t>();
        meta_q2.contents = reader.read<int32_t, int32_t>();
        meta_q2.value = reader.readFloat<float>();
        // we don't need anything other than those 3 in trenchbroom; so just stop here.
        // meta_q2.emissive = reader.read<int16_t, int16_t>();
        // meta_q2.alternate_count = reader.read<int16_t, int16_t>();
        //  ignore the alternates; we don't need that information
      }
      break;
      case Btf::BTF_METASPR: {
        meta_spr.orientation = reader.read<int32_t, int32_t>();
        meta_spr.rendertype = reader.read<int32_t, int32_t>();
        // skip the frame intervals; we won't be animating sprites in TB (for now)
      }
      break;
      }
    }


    const int32_t flags = meta_q2.surfaceflags;
    const int32_t contents = meta_q2.contents;
    const int32_t lightvalue = static_cast<int32_t>(meta_q2.value);

    // read the first frame; that's all we're populating; don't need animation info at
    // the moment.

    reader.seekFromBegin(framedataoffset);

    Btf::frame_t frame{};
    frame.ident = reader.read<uint32, uint32>();
    if (frame.ident != Btf::BTF_FRAMEID)
      return Error("invalid frame data: " + std::to_string(frame.ident));

    // skip sha1 for now.
    frame.sha1 = reader.readString(Btf::SHA1_BUFFER_SIZE);

    // skip over the 40 reserved bytes.
    reader.readVec<byte, 40>();

    const size_t width = static_cast<size_t>(tnfo.width);
    const size_t height = static_cast<size_t>(tnfo.height);

    const size_t numMips = 1;
    auto buffers = mdl::TextureBufferList{numMips};
    mdl::setMipBufferSize(buffers, numMips, width, height, GL_RGBA);
    reader.read(buffers[0].data(), buffers[0].size());

	// flip vertically if required (usually only models)
    if (bVerticalFlip)
    {
      const size_t rowBytes = static_cast<size_t>(width) * 4; // RGBA
      const size_t h = static_cast<size_t>(height);
      auto* data = static_cast<std::uint8_t*>(buffers[0].data());

      //std::vector<std::uint8_t> tmp(rowBytes);
      //for (size_t y = 0; y < h / 2; ++y)
      //{
      //  auto* rowTop = data + y * rowBytes;
      //  auto* rowBot = data + (h - 1 - y) * rowBytes;
	  //
      //  // swap rows
      //  std::memcpy(tmp.data(), rowTop, rowBytes);
      //  std::memcpy(rowTop, rowBot, rowBytes);
      //  std::memcpy(rowBot, tmp.data(), rowBytes);
      //}

	  // non-alloc --slightly-- slower version.
      for (size_t y = 0; y < h / 2; ++y)
      {
        auto* rowTop = data + y * rowBytes;
        auto* rowBot = data + (h - 1 - y) * rowBytes;
        for (size_t i = 0; i < rowBytes; ++i)
          std::swap(rowTop[i], rowBot[i]);
      }
    }

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
