/*
 Copyright (C) 2010 Kristian Duske
 Btf Copyright (C) 2025 Blackened Interactive, Inc

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

#include "mdl/LoadBtfTexture.h"

#include "Color.h"
#include "fs/Reader.h"
#include "fs/ReaderException.h"
#include "mdl/MaterialUtils.h"

#include "kd/contracts.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

namespace tb::mdl
{
// so I can copy and paste without having to keep altering;
// changing to defines
// removed: int16/int32; the main project has upgraded to using proper types.
#ifndef byte
#define byte uint8_t
#endif

namespace Btf
{
constexpr uint32_t BTF_IDENT = (('F' << 24) + ('T' << 16) + ('I' << 8) + 'B');
constexpr uint32_t BTF_FRAMEID = (('M' << 24) + ('A' << 16) + ('R' << 8) + 'F');
constexpr int16_t BTF_VER_MAJOR = 1;
constexpr int16_t BTF_VER_MINOR = 2;

constexpr uint32_t Version(int16_t major, int16_t minor)
{
  return static_cast<uint32_t>(major * 100 + minor * 10);
}
constexpr uint32_t HighestVersion()
{
  return Version(BTF_VER_MAJOR, BTF_VER_MINOR);
}

constexpr uint32_t VersionReflectivity()
{
  return Version(1, 1);
}

constexpr uint32_t VersionFrameDataSize()
{
  return Version(1, 2);
}
// maintaining the same limit of named textures.
// +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 (sequence)
// -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 (random)
// +a +b +c +d +e +f +g +h +i +j (alternates)

// error: unused variable 'MAX_TEXTURE_FRAMES' [-Werror,-Wunused-const-variable]
// error : unused variable 'MAX_ALTERNATE_TEX'[-Werror, -Wunused - const - variable]
// error: unused variable 'BITEXTURE_EXT' [-Werror,-Wunused-const-variable]
// constexpr size_t MAX_TEXTURE_FRAMES = 10;
// constexpr size_t MAX_ALTERNATE_TEX = 10;
// constexpr const char* BITEXTURE_EXT = "btf";

constexpr size_t SHA1_BUFFER_SIZE = 20;
constexpr size_t MAX_TEXTURE_NAME = 64; // 32 can probably fit, but this is safer.

// Current known metadata types.
constexpr uint32_t BTF_METAQ2 = (('A' << 24) + ('T' << 16) + ('M' << 8) + 'Q');  // QMTA
constexpr uint32_t BTF_METASPR = (('T' << 24) + ('M' << 16) + ('P' << 8) + 'S'); // SPMT

enum ECompression
{
  None = 0,
  BC4,
  BC5,
  BC7,
  DXT1,
  DXT5
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
  uint32_t ident;
  int16_t ver_major;
  int16_t ver_minor;
};

struct texinfo_t
{
  int32_t width;
  int32_t height;

  int16_t compressiontype; // ECompression
  int16_t btf_flags;           // EFlags
  int16_t animType;        // EAnimType
  int16_t frame_count;     // number of frames in texture

  int32_t framedatasize;
  int32_t framedataoffset;

  int32_t metadatasize;
  int32_t metadataoffset;
};

// frames... shouldn't need to change often.
// ver 1.0; reserved[40]
// ver 1.1; reserved[28]
// ver 1.2; framedatasize; reserved[24]
struct frame_t
{
  uint32_t ident;
  std::string sha1; // SHA1_BUFFER_SIZE;
  float reflectivity[3];
  int32_t framedatasize;
  byte reserved[24];
};


struct metadata_t
{
  uint32_t ident;
};

// metadata for quake2/vigil7
struct metadata_q2_t : public metadata_t
{
  // game surface flags;
  int32_t surfaceflags;

  // game content flags
  int32_t contents;

  // SURF_LIGHT value in Q2 (int32) ; changed to a float so I can re-purpose it.
  float value;

  // texture is emissive; alpha is mask
  int16_t emissive;

  // surface type out of surfaces.txt
  char surfacetype[MAX_TEXTURE_NAME];

  // number of alternate texture names
  int16_t alternate_count;
  // followed by alternate_count * char[btf::MAX_TEXTURE_NAME]
};

struct metadata_sprite_t : public metadata_t
{
  int32_t orientation;
  int32_t rendertype;
  // followed by frame_count * int32 (intervals)
};
#pragma pack(pop)

static_assert(sizeof(header_t) == 8, "header_t size");
static_assert(sizeof(texinfo_t) == 32, "texinfo_t size");
// static_assert(sizeof(frame_t) == 64, "frame_t should always be 64 bytes.");

static_assert(sizeof(metadata_q2_t) == 84, "metadata_q2_t size");
static_assert(sizeof(metadata_sprite_t) == 12, "metadata_sprite_t size");
} // namespace Btf

namespace
{
using u8 = std::uint8_t;

static inline std::uint16_t readLE16(const u8* p)
{
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8));
}

static inline std::uint32_t readLE24(const u8* p)
{
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16);
}

static inline std::uint32_t readLE32(const u8* p)
{
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

static inline bool isBlockCompressionType(const Btf::ECompression compressionType)
{
  switch (compressionType)
  {
  case Btf::ECompression::DXT1:
  case Btf::ECompression::DXT5:
  case Btf::ECompression::BC4:
  case Btf::ECompression::BC5:
  case Btf::ECompression::BC7:
    return true;
  default:
    return false;
  }
}

static inline int compressionBlockBytes(const Btf::ECompression compressionType)
{
  switch (compressionType)
  {
  case Btf::ECompression::DXT1:
  case Btf::ECompression::BC4:
    return 8;
  case Btf::ECompression::DXT5:
  case Btf::ECompression::BC5:
  case Btf::ECompression::BC7:
    return 16;
  default:
    return 0;
  }
}

// all other types are deprecated (never went past 4 channels, staying 4 channels)
static inline std::size_t formatRowPitch(const int width)
{
  return static_cast<std::size_t>(width) * 4u;
}

static inline std::size_t frameBytes(
  const Btf::ECompression compressionType, const int width, const int height)
{
  if (width <= 0 || height <= 0)
  {
    return 0u;
  }

  if (isBlockCompressionType(compressionType))
  {
    const int blockBytes = compressionBlockBytes(compressionType);
    if (blockBytes <= 0)
    {
      return 0u;
    }

    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    return static_cast<std::size_t>(blocksX) * static_cast<std::size_t>(blocksY)
           * static_cast<std::size_t>(blockBytes);
  }

  return formatRowPitch(width) * static_cast<std::size_t>(height);
}

static inline void decodeRGB565(std::uint16_t c, u8& r, u8& g, u8& b)
{
  const std::uint32_t rv = (c >> 11) & 31u;
  const std::uint32_t gv = (c >> 5) & 63u;
  const std::uint32_t bv = c & 31u;
  r = static_cast<u8>((rv * 255u + 15u) / 31u);
  g = static_cast<u8>((gv * 255u + 31u) / 63u);
  b = static_cast<u8>((bv * 255u + 15u) / 31u);
}

static void decodeDXTColorBlock(
  const u8* block, std::array<std::array<u8, 4>, 4>& palette)
{
  const std::uint16_t c0 = readLE16(block + 0);
  const std::uint16_t c1 = readLE16(block + 2);

  decodeRGB565(c0, palette[0][0], palette[0][1], palette[0][2]);
  decodeRGB565(c1, palette[1][0], palette[1][1], palette[1][2]);
  palette[0][3] = 255;
  palette[1][3] = 255;

  if (c0 > c1)
  {
    for (std::size_t c = 0; c < 3; ++c)
    {
      const auto p0 = static_cast<unsigned>(palette[0][c]);
      const auto p1 = static_cast<unsigned>(palette[1][c]);

      palette[2][c] = static_cast<u8>((2u * p0 + p1) / 3u);
      palette[3][c] = static_cast<u8>((p0 + 2u * p1) / 3u);
    }

    palette[2][3] = 255;
    palette[3][3] = 255;
  }
  else
  {
    for (std::size_t c = 0; c < 3; ++c)
    {
      const auto p0 = static_cast<unsigned>(palette[0][c]);
      const auto p1 = static_cast<unsigned>(palette[1][c]);

      palette[2][c] = static_cast<u8>((p0 + p1) / 2u);
    }

    palette[2][3] = 255;
    palette[3][0] = 0;
    palette[3][1] = 0;
    palette[3][2] = 0;
    palette[3][3] = 0;
  }
}

static void decodeBC4Block(const u8* block, u8 outValues[16])
{
  const u8 r0 = block[0];
  const u8 r1 = block[1];

  u8 table[8] = {};
  table[0] = r0;
  table[1] = r1;
  if (r0 > r1)
  {
    table[2] = static_cast<u8>((6 * r0 + 1 * r1) / 7);
    table[3] = static_cast<u8>((5 * r0 + 2 * r1) / 7);
    table[4] = static_cast<u8>((4 * r0 + 3 * r1) / 7);
    table[5] = static_cast<u8>((3 * r0 + 4 * r1) / 7);
    table[6] = static_cast<u8>((2 * r0 + 5 * r1) / 7);
    table[7] = static_cast<u8>((1 * r0 + 6 * r1) / 7);
  }
  else
  {
    table[2] = static_cast<u8>((4 * r0 + 1 * r1) / 5);
    table[3] = static_cast<u8>((3 * r0 + 2 * r1) / 5);
    table[4] = static_cast<u8>((2 * r0 + 3 * r1) / 5);
    table[5] = static_cast<u8>((1 * r0 + 4 * r1) / 5);
    table[6] = 0;
    table[7] = 255;
  }

  std::uint64_t idxBits = 0;
  idxBits |= static_cast<std::uint64_t>(readLE24(block + 2));
  idxBits |= static_cast<std::uint64_t>(readLE24(block + 5)) << 24;

  for (std::size_t i = 0; i < 16; ++i)
  {
    const auto shift = static_cast<unsigned>(i * 3u);
    const auto idx = static_cast<std::size_t>((idxBits >> shift) & 0x7ull);

    outValues[i] = table[idx];
  }
}

static bool decodeFrameToRGBA(
  const u8* payload,
  const std::size_t payloadSize,
  const int width,
  const int height,
  const Btf::ECompression compressionType,
  std::vector<u8>& outRGBA)
{
  if (width <= 0 || height <= 0)
  {
    return false;
  }

  const std::size_t rgbaSize =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
  outRGBA.resize(rgbaSize);

  if (compressionType == Btf::ECompression::None)
  {
    const std::size_t rawSize = frameBytes(compressionType, width, height);
    if (rawSize == 0 || payloadSize < rawSize)
    {
      return false;
    }

    // const std::size_t pixelCount =
    //   static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    //     switch (format)
    //     {
    //     case Btf::EFormat::RGBA:
    std::memcpy(outRGBA.data(), payload, rgbaSize);
    return true;
    //     case Btf::EFormat::RGB:
    //       for (std::size_t i = 0; i < pixelCount; ++i)
    //       {
    //         const std::size_t dst = i * 4u;
    //         const std::size_t src = i * 3u;
    //
    //         outRGBA[dst + 0u] = payload[src + 0u];
    //         outRGBA[dst + 1u] = payload[src + 1u];
    //         outRGBA[dst + 2u] = payload[src + 2u];
    //         outRGBA[dst + 3u] = 255;
    //       }
    //       return true;
    //     case Btf::EFormat::ARGB:
    //       for (std::size_t i = 0; i < pixelCount; ++i)
    //       {
    //         const std::size_t dst = i * 4u;
    //         const std::size_t src = i * 4u;
    //
    //         outRGBA[dst + 0u] = payload[src + 1u];
    //         outRGBA[dst + 1u] = payload[src + 2u];
    //         outRGBA[dst + 2u] = payload[src + 3u];
    //         outRGBA[dst + 3u] = payload[src + 0u];
    //       }
    //       return true;
    //     default:
    //       return false;
    //     }
  }

  if (compressionType == Btf::ECompression::BC7)
  {
    return false;
  }

  const int blockBytes = compressionBlockBytes(compressionType);
  if (blockBytes <= 0)
  {
    return false;
  }

  const int blocksX = (width + 3) / 4;
  const int blocksY = (height + 3) / 4;
  const std::size_t required = static_cast<std::size_t>(blocksX)
                               * static_cast<std::size_t>(blocksY)
                               * static_cast<std::size_t>(blockBytes);
  if (payloadSize < required)
  {
    return false;
  }

  std::array<std::array<u8, 4>, 4> palette{};
  u8 bc4R[16] = {};
  u8 bc4G[16] = {};

  for (int by = 0; by < blocksY; ++by)
  {
    for (int bx = 0; bx < blocksX; ++bx)
    {
      const std::size_t blockIndex =
        static_cast<std::size_t>(by) * static_cast<std::size_t>(blocksX)
        + static_cast<std::size_t>(bx);
      const u8* block = payload + blockIndex * static_cast<std::size_t>(blockBytes);

      if (compressionType == Btf::ECompression::DXT1)
      {
        decodeDXTColorBlock(block, palette);
        const std::uint32_t idxBits = readLE32(block + 4);

        for (int py = 0; py < 4; ++py)
        {
          for (int px = 0; px < 4; ++px)
          {
            const int x = bx * 4 + px;
            const int y = by * 4 + py;
            if (x >= width || y >= height)
            {
              continue;
            }

            const auto pi = static_cast<std::size_t>(py * 4 + px);
            const auto idx = static_cast<std::size_t>((idxBits >> (pi * 2)) & 0x3u);
            const std::size_t dst =
              (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
               + static_cast<std::size_t>(x))
              * 4u;
            outRGBA[dst + 0] = palette[idx][0];
            outRGBA[dst + 1] = palette[idx][1];
            outRGBA[dst + 2] = palette[idx][2];
            outRGBA[dst + 3] = palette[idx][3];
          }
        }
      }
      else if (compressionType == Btf::ECompression::DXT5)
      {
        decodeBC4Block(block, bc4R);
        decodeDXTColorBlock(block + 8, palette);
        const std::uint32_t idxBits = readLE32(block + 12);

        for (int py = 0; py < 4; ++py)
        {
          for (int px = 0; px < 4; ++px)
          {
            const int x = bx * 4 + px;
            const int y = by * 4 + py;
            if (x >= width || y >= height)
            {
              continue;
            }

            const auto pi = static_cast<std::size_t>(py * 4 + px);
            const auto idx = static_cast<std::size_t>((idxBits >> (pi * 2)) & 0x3u);
            const std::size_t dst =
              (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
               + static_cast<std::size_t>(x))
              * 4u;
            outRGBA[dst + 0] = palette[idx][0];
            outRGBA[dst + 1] = palette[idx][1];
            outRGBA[dst + 2] = palette[idx][2];
            outRGBA[dst + 3] = bc4R[pi];
          }
        }
      }
      else if (compressionType == Btf::ECompression::BC4)
      {
        decodeBC4Block(block, bc4R);
        for (int py = 0; py < 4; ++py)
        {
          for (int px = 0; px < 4; ++px)
          {
            const int x = bx * 4 + px;
            const int y = by * 4 + py;
            if (x >= width || y >= height)
            {
              continue;
            }

            const int pi = py * 4 + px;
            const auto v = bc4R[pi];
            const std::size_t dst =
              (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
               + static_cast<std::size_t>(x))
              * 4u;
            outRGBA[dst + 0] = v;
            outRGBA[dst + 1] = v;
            outRGBA[dst + 2] = v;
            outRGBA[dst + 3] = 255;
          }
        }
      }
      else if (compressionType == Btf::ECompression::BC5)
      {
        decodeBC4Block(block + 0, bc4R);
        decodeBC4Block(block + 8, bc4G);
        for (int py = 0; py < 4; ++py)
        {
          for (int px = 0; px < 4; ++px)
          {
            const int x = bx * 4 + px;
            const int y = by * 4 + py;
            if (x >= width || y >= height)
            {
              continue;
            }

            const int pi = py * 4 + px;
            const std::size_t dst =
              (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
               + static_cast<std::size_t>(x))
              * 4u;
            outRGBA[dst + 0] = bc4R[pi];
            outRGBA[dst + 1] = bc4G[pi];
            outRGBA[dst + 2] = 0;
            outRGBA[dst + 3] = 255;
          }
        }
      }
      else
      {
        return false;
      }
    }
  }

  return true;
}
} // namespace

// LoadFreeImageTexture.cpp
Color getAverageColor(const gl::TextureBuffer& buffer, GLenum format);
Result<gl::Texture> loadBtfTexture(fs::Reader& reader, bool bVerticalFlip /*= false*/)
{
  try
  {
    Btf::header_t hdr{};
    hdr.ident = reader.read<uint32_t, uint32_t>();
    if (hdr.ident != Btf::BTF_IDENT)
      return Error("unknown btf identifier: " + std::to_string(hdr.ident));

    hdr.ver_major = reader.read<int16_t, int16_t>();
    hdr.ver_minor = reader.read<int16_t, int16_t>();
    if (Btf::Version(hdr.ver_major, hdr.ver_minor) > Btf::HighestVersion())
      return Error(
        fmt::format("unsupported btf version: {}.{}", hdr.ver_major, hdr.ver_minor));

    Btf::texinfo_t tnfo{};
    tnfo.width = reader.read<int32_t, int32_t>();
    tnfo.height = reader.read<int32_t, int32_t>();
    tnfo.compressiontype = reader.read<int16_t, int16_t>();
    tnfo.btf_flags = reader.read<int16_t, int16_t>();
    tnfo.animType = reader.read<int16_t, int16_t>(); // 0 = none, 1 = sequence, 2 = random
    tnfo.frame_count = reader.read<int16_t, int16_t>();
    if (tnfo.frame_count <= 0)
      return Error(fmt::format("frames are missing? {}", tnfo.frame_count));


    // use these in trenchbroom instead.
    size_t framedatasize = reader.readSize<int32_t>();
    size_t framedataoffset = reader.readSize<int32_t>();

    size_t metadatasize = reader.readSize<int32_t>();
    size_t metadataoffset = reader.readSize<int32_t>();

    if (framedatasize == 0)
      return Error("no framedata");


    // meh!
    Btf::metadata_q2_t meta_q2{};
    // ignore sprite meta for now as well, until it's actually needed.
    // Btf::metadata_sprite_t meta_spr{};
    if (metadatasize > 0)
    {
      // read the metadata first; even though it's at the end.
      reader.seekFromBegin(metadataoffset);

      // read metadata type.
      uint32_t metadatatype = reader.read<uint32_t, uint32_t>();
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
        /*meta_spr.orientation = */ reader.read<int32_t, int32_t>();
        /*meta_spr.rendertype = */ reader.read<int32_t, int32_t>();
        // skip the frame intervals; we won't be animating sprites in TB (for now)
      }
      break;
      }
    }


    const int32_t surfFlags = meta_q2.surfaceflags;
    const int32_t contents = meta_q2.contents;
    const int32_t lightvalue = static_cast<int32_t>(meta_q2.value);

    // read the first frame; that's all we're populating; don't need animation info at
    // the moment.

    reader.seekFromBegin(framedataoffset);

    Btf::frame_t frame{};
    frame.ident = reader.read<uint32_t, uint32_t>();
    if (frame.ident != Btf::BTF_FRAMEID)
      return Error("invalid frame data: " + std::to_string(frame.ident));

    // read sha1 (is actually calculated since 1.1)
    frame.sha1 = reader.readString(Btf::SHA1_BUFFER_SIZE);

    // note; we could use the reflectivity for average color
    // since reflectivity is average color; just.. based on a gamma value.
    if (Btf::Version(hdr.ver_major, hdr.ver_minor) >= Btf::VersionFrameDataSize())
    {
      // reflectivity
      reader.readVec<float, 3>();

      frame.framedatasize = reader.read<int32_t, int32_t>();
      reader.readVec<byte, 20>(); // remaining 20 bytes
    }
    else if (Btf::Version(hdr.ver_major, hdr.ver_minor) >= Btf::VersionReflectivity())
    {
      reader.readVec<float, 3>();
      reader.readVec<byte, 28>();
    }
    else

      // skip over the 40 reserved bytes.
      reader.readVec<byte, 40>();

    const size_t width = static_cast<size_t>(tnfo.width);
    const size_t height = static_cast<size_t>(tnfo.height);

    const size_t numMips = 1;
    auto buffers = gl::TextureBufferList{numMips};
    setMipBufferSize(buffers, numMips, width, height, GL_RGBA);

    const auto compressionType = static_cast<Btf::ECompression>(tnfo.compressiontype);

    size_t framePayloadSize = frameBytes(compressionType, tnfo.width, tnfo.height);
    if (framePayloadSize == 0)
    {
      return Error(fmt::format(
        "unsupported BTF frame payload format (compression={})", tnfo.compressiontype));
    }

    if (Btf::Version(hdr.ver_major, hdr.ver_minor) >= Btf::VersionFrameDataSize())
    {
      if (frame.framedatasize <= 0)
      {
        return Error(fmt::format(
          "invalid frame payload size in frame header: {}", frame.framedatasize));
      }
      framePayloadSize = static_cast<size_t>(frame.framedatasize);
    }

    auto encodedFrame = std::vector<u8>(framePayloadSize);
    reader.read(encodedFrame.data(), encodedFrame.size());

    std::vector<u8> decodedRGBA;
    if (
      !decodeFrameToRGBA(
        encodedFrame.data(),
        encodedFrame.size(),
        tnfo.width,
        tnfo.height,
        compressionType,
        decodedRGBA)
      || decodedRGBA.size() != buffers[0].size())
    {
      return Error(
        fmt::format("failed to decode BTF frame (compression={})", tnfo.compressiontype));
    }

    std::memcpy(buffers[0].data(), decodedRGBA.data(), decodedRGBA.size());

    // flip vertically if required (usually only models)
    if (bVerticalFlip)
    {
      const size_t rowBytes = static_cast<size_t>(width) * 4; // RGBA
      const size_t h = static_cast<size_t>(height);
      auto* data = static_cast<std::uint8_t*>(buffers[0].data());

      // std::vector<std::uint8_t> tmp(rowBytes);
      // for (size_t y = 0; y < h / 2; ++y)
      //{
      //   auto* rowTop = data + y * rowBytes;
      //   auto* rowBot = data + (h - 1 - y) * rowBytes;
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

    // static auto averageColor = Color{};
    const auto averageColor = getAverageColor(buffers[0], GL_BGRA);
    auto embeddedDefaults = gl::Q2EmbeddedDefaults{surfFlags, contents, lightvalue};
    return gl::Texture{
      width,
      height,
      averageColor,
      GL_RGBA,
      gl::TextureMask::Off,
      std::move(embeddedDefaults),
      std::move(buffers)};
  }
  catch (const fs::ReaderException& e)
  {
    return Error{e.what()};
  }
}
} // namespace tb::mdl
