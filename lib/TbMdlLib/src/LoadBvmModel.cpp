/*
 Copyright (C) 2010 Kristian Duske
 Bvm Copyright (C) 2025 Blackened Interactive, Inc

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

#include "mdl/LoadBvmModel.h"

#include "fs/FileSystem.h"
#include "fs/ReaderException.h"
#include "gl/IndexRangeMap.h"
#include "gl/IndexRangeMapBuilder.h"
#include "gl/Material.h"
#include "gl/PrimType.h"
#include "mdl/LoadSkin.h"

#include "kd/path_utils.h"
#include "kd/result_fold.h"

#include <fmt/format.h>

#include <array>
#include <string>
#include <vector>

namespace tb::mdl
{

namespace
{
namespace BvmLayout
{
constexpr uint32_t BVM_IDENT = (('M' << 24) + ('V' << 16) + ('I' << 8) + 'B'); // BIVM
constexpr uint32_t BVM_SKIN_IDENT =
  (('K' << 24) + ('S' << 16) + ('M' << 8) + 'S'); // SMSK
constexpr uint32_t BVM_SUBMESH_IDENT =
  (('H' << 24) + ('S' << 16) + ('M' << 8) + 'S'); // SMSH

constexpr size_t MAX_BVM_NAME = 64;

// the most recent version.
constexpr int32_t BVM_CURRENTVERSION = 4;


// some helpers, so i don't need to put magic numbers.
constexpr int32_t VTXMDL_VERSION_GROUPS = 4;
constexpr int32_t VTXMDL_VERSION_COLLISION = 4;

} // namespace BvmLayout

// unique for tb.
struct BvmTriangle
{
  size_t vertices[3];
};

struct BvmSeq
{
  std::string name;
  size_t frames;
  int framerate;
  float scale;
};

struct BvmFlatFrame
{
  size_t seq;
  size_t inSeq;
  std::string name;
};

struct BvmGroupInfo
{
  int32_t groupid;
  std::string groupname; // MAX_VTXMDL_NAME
  int32_t mass;
  int32_t num_verts;
  int32_t num_indices;
};

void loadSkins(
  EntityModelSurface& surface,
  const std::vector<std::string>& skins,
  const fs::FileSystem& fs,
  Logger& logger)
{
  auto materials = std::vector<gl::Material>{};
  materials.reserve(skins.size());
  for (const auto& skin : skins)
  {
    // we just use the skinloader; it handles readFreeImageTexture; which has already been
    // modified to load .btf. and it will load png/tga as well.
    materials.push_back(loadSkin(skin, fs, logger));
  }

  surface.setSkins(std::move(materials));
}

// similar.
auto makeFrameTriangles(
  const std::vector<BvmTriangle>& triangles,
  const std::vector<vm::vec3f>& verts,
  /*const std::vector<vm::vec3f>& normals,*/
  const std::vector<vm::vec2f>& uvs)
{
  auto frameTriangles = std::vector<EntityModelVertex>{};
  frameTriangles.reserve(triangles.size());

  for (size_t i = 0; i < triangles.size(); i++)
  {
    const auto& triangle = triangles[i];
    for (size_t j = 0; j < 3; j++)
    {
      const auto vertIndex = triangle.vertices[j];
      const auto& pos = verts[vertIndex];
      // okay we can't actually do anything with the normals.
      // because EntityModelVertex only supports Pos + UV.
      // we need `GLVertexTypes::P3NT2` to actually include the normals.
      // it would require changing the whole model renderer apparently (if im
      // understanding it correctly) so we'll just do the positions and UV and hope for
      // the best.
      // const auto& normal = normals[vertIndex];
      const auto& uv = uvs[vertIndex];
      frameTriangles.emplace_back(pos, uv);
    }
  }
  return frameTriangles;
}


} // namespace

bool canLoadBvmModel(const std::filesystem::path& path, fs::Reader reader)
{
  if (!kdl::path_has_extension(kdl::path_to_lower(path), ".bvm"))
  {
    return false;
  }

  const auto ident = reader.readInt<int32_t>();
  const auto version = reader.readInt<int32_t>();

  return ident == BvmLayout::BVM_IDENT && version <= BvmLayout::BVM_CURRENTVERSION;
}

Result<EntityModelData> loadV1(
  std::string modelName, fs::Reader& reader, const fs::FileSystem& fs, Logger& logger)
{
  // v1 is the most simple;
  // it's almost the same as q1 mdl; except normals are included
  // and no single frames, only sequences (frame groups).
  const auto base_texture = reader.readString(BvmLayout::MAX_BVM_NAME);
  /*const auto emission_texture = */ reader.readString(BvmLayout::MAX_BVM_NAME);
  /*const auto interior_texture = */ reader.readString(BvmLayout::MAX_BVM_NAME);
  /*const auto emission_scale = */ reader.readFloat<float>();
  const auto origin = reader.readVec<float, 3>();
  const auto scale = reader.readFloat<float>();
  const auto num_verts = reader.readSize<int32_t>();
  const auto num_indices = reader.readSize<int32_t>();
  /*const auto flags = */ reader.readInt<int32_t>();
  /*const auto num_sequences =*/reader.readSize<int32_t>();

  auto triangles = std::vector<BvmTriangle>{};
  triangles.reserve(num_indices / 3);

  // read indices.
  for (int i = 0; i < num_indices; i += 3)
  {
    auto tri = BvmTriangle{};
    tri.vertices[0] = reader.readSize<int32_t>();
    tri.vertices[1] = reader.readSize<int32_t>();
    tri.vertices[2] = reader.readSize<int32_t>();
    triangles.push_back(tri);
  }

  auto verts = std::vector<vm::vec3f>{};
  auto normals = std::vector<vm::vec3f>{};
  auto uvs = std::vector<vm::vec2f>{};
  verts.reserve(num_verts);
  normals.reserve(num_verts);
  uvs.reserve(num_verts);

  // read verts, normals, uvs
  for (int i = 0; i < num_verts; i++)
  {
    auto pos = reader.readVec<float, 3>();
    verts.push_back((pos + origin) * scale); // just apply the origin and scale now.

    auto nrm = reader.readVec<float, 3>();
    normals.push_back(nrm);

    auto uv = reader.readVec<float, 2>();
    uvs.push_back(uv);
  }

  auto model = mdl::EntityModelData{mdl::PitchType::Normal, mdl::Orientation::Oriented};

  // v1 doesn't support multiple skins;
  const auto skins =
    std::vector<std::string>{fmt::format("textures/models/{}.btf", base_texture)};

  // v1 is just staying as a static pose.
  const size_t num_frames = 1;

  auto& surface = model.addSurface(modelName, num_frames);
  loadSkins(surface, skins, fs, logger);

  // take all the base vertices and make a single frame.
  auto bounds = vm::bbox3f::builder{};
  bounds.add(verts.begin(), verts.end());

  const auto frameTriangles = makeFrameTriangles(triangles, verts, /*normals,*/ uvs);
  auto size = gl::IndexRangeMap::Size{};
  size.inc(gl::PrimType::Triangles, frameTriangles.size());

  auto builder =
    gl::IndexRangeMapBuilder<EntityModelVertex::Type>{frameTriangles.size() * 3, size};

  builder.addTriangles(frameTriangles);

  auto name = "staticpose";
  auto& frame = model.addFrame(std::move(name), bounds.bounds());
  surface.addMesh(frame, std::move(builder.vertices()), std::move(builder.indices()));

  return model;
}

Result<EntityModelData> loadV2(
  fs::Reader& reader, const fs::FileSystem& fs, Logger& logger)
{
  auto model = mdl::EntityModelData{mdl::PitchType::Normal, mdl::Orientation::Oriented};
  const auto origin = reader.readVec<float, 3>();
  const auto scale = reader.readFloat<float>();
  const auto num_submeshes = reader.readSize<int32_t>();
  const auto num_sequences = reader.readSize<int32_t>();
  /*const auto flags = */ reader.readInt<int32_t>();

  auto seqs = std::vector<BvmSeq>{};

  // read sequences.
  if (num_sequences == 0)
  {
    // no sequences to read; we're a static pose;
    seqs.push_back({"staticpose", 1, 10, 1.f});
  }
  else
  {
    seqs.reserve(num_sequences);
    for (size_t si = 0; si < num_sequences; si++)
    {

      auto seq = BvmSeq{};
      seq.name = reader.readString(BvmLayout::MAX_BVM_NAME);
      seq.frames = reader.readSize<int32_t>();
      seq.framerate = reader.readInt<int32_t>();
      seq.scale = reader.readFloat<float>();
      if (seq.scale <= 0)
        seq.scale = 1.f;
      seqs.push_back(std::move(seq));
    }
  }

  // build flatframes.
  std::vector<BvmFlatFrame> flatFrames;
  size_t Ftotal = 0;
  for (size_t s = 0; s < seqs.size(); ++s)
    Ftotal += seqs[s].frames;

  flatFrames.reserve(Ftotal);
  if (num_sequences == 0)
  {
    flatFrames.push_back({0, 0, "staticpose"});
  }
  else
  {
    for (size_t s = 0; s < seqs.size(); ++s)
    {
      for (size_t f = 0; f < seqs[s].frames; ++f)
      {
        flatFrames.push_back({s, f, fmt::format("{}_{:03}", seqs[s].name, f)});
      }
    }
  }

  std::vector<vm::bbox3f::builder> frameBounds(Ftotal);
  // We'll remember per-surface data so we can add meshes after frames are created
  struct SurfaceTemp
  {
    std::string name;
    std::vector<BvmTriangle> triangles;
    std::vector<vm::vec2f> uvs;                      // static per submesh
    std::vector<std::vector<vm::vec3f>> perFramePos; // size = Ftotal
    std::vector<std::vector<vm::vec3f>> perFrameNrm; // size = Ftotal
    mdl::EntityModelSurface* surface = nullptr;      // created now, filled later
  };
  std::vector<SurfaceTemp> surfaces;
  surfaces.reserve(num_submeshes);


  // parse all submeshes..
  for (size_t smi = 0; smi < num_submeshes; smi++)
  {
    const auto sm_name = fmt::format("submesh_{}", smi);
    const auto ident = reader.readInt<uint32_t>();
    if (ident != BvmLayout::BVM_SUBMESH_IDENT)
    {
      return Error{fmt::format("Unknown BVM submesh ident: {}", ident)};
    }
    const auto tex_diffuse = reader.readString(BvmLayout::MAX_BVM_NAME);
    /*mesh.tex_interior = */ reader.readString(BvmLayout::MAX_BVM_NAME);
    /*mesh.tex_emission = */ reader.readString(BvmLayout::MAX_BVM_NAME);
    /*mesh.emission_scale =*/reader.readFloat<float>();
    const auto num_indices = reader.readSize<int32_t>();
    const auto num_verts = reader.readSize<int32_t>();
    const auto num_skins = reader.readSize<int32_t>();

    auto sm_skins = std::vector<std::string>{};
    sm_skins.push_back(tex_diffuse); // insert skin0 first.
    for (int sk = 0; sk < num_skins; sk++)
    {
      const auto sk_ident = reader.readInt<uint32_t>();
      if (sk_ident != BvmLayout::BVM_SKIN_IDENT)
      {
        return Error{fmt::format("Unknown BVM skin ident: {}", sk_ident)};
      }
      const auto sk_diffuse = reader.readString(BvmLayout::MAX_BVM_NAME);
      /*const auto sk_interior = */ reader.readString(BvmLayout::MAX_BVM_NAME);
      /*const auto sk_emission = */ reader.readString(BvmLayout::MAX_BVM_NAME);
      /*const auto sk_emscale = */ reader.readFloat<float>();
      sm_skins.push_back(fmt::format("textures/models/{}.btf", sk_diffuse));
    }

    // triangles
    auto sm_triangles = std::vector<BvmTriangle>{};
    sm_triangles.reserve(num_indices / 3);

    // read indices.
    for (int i = 0; i < num_indices; i += 3)
    {
      auto tri = BvmTriangle{};
      tri.vertices[0] = reader.readSize<int32_t>();
      tri.vertices[1] = reader.readSize<int32_t>();
      tri.vertices[2] = reader.readSize<int32_t>();
      sm_triangles.push_back(tri);
    }

    auto sm_basepos = std::vector<vm::vec3f>{};
    auto sm_basenrm = std::vector<vm::vec3f>{};
    auto sm_uvs = std::vector<vm::vec2f>{};
    sm_basepos.reserve(num_verts);
    sm_basenrm.reserve(num_verts);
    sm_uvs.reserve(num_verts);

    // read verts, normals, uvs
    for (int i = 0; i < num_verts; i++)
    {
      auto pos = reader.readVec<float, 3>();
      sm_basepos.push_back((pos + origin) * scale);

      auto nrm = reader.readVec<float, 3>();
      sm_basenrm.push_back(nrm);

      auto uv = reader.readVec<float, 2>();
      sm_uvs.push_back(uv);
    }

    // create the surface ..
    auto& surface = model.addSurface(sm_name, Ftotal);
    loadSkins(surface, sm_skins, fs, logger);

    auto tmp = SurfaceTemp{};
    tmp.name = sm_name;
    tmp.triangles = std::move(sm_triangles);
    tmp.uvs = std::move(sm_uvs);
    tmp.perFramePos.resize(Ftotal);
    tmp.perFrameNrm.resize(Ftotal);
    tmp.surface = &surface;

    // there are no sequences; so this is populating the static pose.
    if (num_sequences == 0)
    {
      tmp.perFramePos[0] = std::move(sm_basepos);
      tmp.perFrameNrm[0] = std::move(sm_basenrm);
      frameBounds[0].add(tmp.perFramePos[0].begin(), tmp.perFramePos[0].end());
    }
    else
    {
      size_t flatIdx = 0;
      for (size_t si = 0; si < seqs.size(); si++)
      {
        for (size_t fi = 0; fi < seqs[si].frames; fi++, flatIdx++)
        {
          auto& posDest = tmp.perFramePos[flatIdx];
          auto& nrmDest = tmp.perFrameNrm[flatIdx];
          posDest.resize(num_verts);
          nrmDest.resize(num_verts);
          for (size_t vi = 0; vi < num_verts; vi++)
          {
            auto pos = reader.readVec<float, 3>();
            posDest[vi] = (pos + origin) * (scale * seqs[si].scale);
            nrmDest[vi] = reader.readVec<float, 3>();
          }
          frameBounds[flatIdx].add(posDest.begin(), posDest.end());
        }
      }
    }
    surfaces.push_back(std::move(tmp));
  }

  // add frames, and each surface per-frame mesh
  auto frames = std::vector<mdl::EntityModelFrame*>{};
  frames.reserve(Ftotal);
  for (size_t i = 0; i < Ftotal; ++i)
  {
    frames.push_back(&model.addFrame(flatFrames[i].name, frameBounds[i].bounds()));
  }

  using Vertex = EntityModelVertex;
  for (auto& surf : surfaces)
  {
    gl::IndexRangeMap rangeMap{gl::PrimType::Triangles, 0, 3 * surf.triangles.size()};
    for (size_t fi = 0; fi < Ftotal; ++fi)
    {
      const auto& pos = surf.perFramePos[fi];
      std::vector<Vertex> frameVerts;
      frameVerts.reserve(3 * surf.triangles.size());
      for (const auto& tri : surf.triangles)
      {
        const auto i0 = tri.vertices[0];
        const auto i1 = tri.vertices[1];
        const auto i2 = tri.vertices[2];
        frameVerts.emplace_back(pos[i0], surf.uvs[i0]);
        frameVerts.emplace_back(pos[i1], surf.uvs[i1]);
        frameVerts.emplace_back(pos[i2], surf.uvs[i2]);
      }
      surf.surface->addMesh(
        *frames[fi], std::move(frameVerts), gl::IndexRangeMap{rangeMap});
    }
  }


  return model;
}

Result<EntityModelData> loadCurrent(
  fs::Reader& reader, int version, const fs::FileSystem& fs, Logger& logger)
{
  auto model = mdl::EntityModelData{mdl::PitchType::Normal, mdl::Orientation::Oriented};
  const auto origin = reader.readVec<float, 3>();
  const auto scale = reader.readFloat<float>();
  /*const auto flags = */ reader.readInt<int32_t>();
  /*const auto mins =*/reader.readVec<float, 3>();
  /*const auto maxs =*/reader.readVec<float, 3>();

  size_t group_count = 0;
  size_t group_offset = 0;
  if (version >= BvmLayout::VTXMDL_VERSION_GROUPS)
  {
	group_count = reader.readSize<int32_t>();
	group_offset = reader.readSize<int32_t>();
  }

  const auto sequence_count = reader.readSize<int32_t>();
  const auto sequence_offset = reader.readSize<int32_t>();

  const auto submesh_count = reader.readSize<int32_t>();
  const auto submesh_offset = reader.readSize<int32_t>();

  // we don't need the collision in trenchbroom, so just read past it.
  if (version >= BvmLayout::VTXMDL_VERSION_COLLISION)
  {
    /*const auto collision_count =*/ reader.readSize<int32_t>();
    /*const auto collision_offset =*/ reader.readSize<int32_t>();
  }

  /*const auto metadatasize = */ reader.readSize<int32_t>();
  /*const auto metadataoffset = */ reader.readSize<int32_t>();

  auto seqs = std::vector<BvmSeq>{};

  // read sequences.
  reader.seekFromBegin(sequence_offset);
  seqs.reserve(sequence_count);
  for (size_t si = 0; si < sequence_count; si++)
  {
    auto seq = BvmSeq{};
    seq.name = reader.readString(BvmLayout::MAX_BVM_NAME);
    seq.frames = reader.readSize<int32_t>();
    seq.framerate = reader.readInt<int32_t>();
    seq.scale = reader.readFloat<float>();
    if (seq.scale <= 0)
      seq.scale = 1.f;
    seqs.push_back(std::move(seq));
  }

  // build flatframes.
  std::vector<BvmFlatFrame> flatFrames;
  size_t Ftotal = 0;
  for (size_t s = 0; s < seqs.size(); ++s)
    Ftotal += seqs[s].frames;

  flatFrames.reserve(Ftotal);
  for (size_t s = 0; s < seqs.size(); ++s)
    for (size_t f = 0; f < seqs[s].frames; ++f)
      flatFrames.push_back({s, f, fmt::format("{}_{:03}", seqs[s].name, f)});

  std::vector<vm::bbox3f::builder> frameBounds(Ftotal);

  // parse the submesh data out first; and then actually do the reading.
  struct Submesh
  {
	//v4+
	int32_t flags;
	int32_t groupid;
	int32_t slotid;

    std::string name;
    size_t num_skins;
    size_t skin_offset;
    size_t num_verts;
    size_t uv_offset;

    size_t num_indices;
    size_t indice_offset;

    size_t sequence_offset;
  };

  std::vector<Submesh> submeshes;
  submeshes.reserve(submesh_count);
  reader.seekFromBegin(submesh_offset);
  for (int smi = 0; smi < submesh_count; smi++)
  {
    const auto ident = reader.readInt<uint32_t>();
    if (ident != BvmLayout::BVM_SUBMESH_IDENT)
    {
      return Error{fmt::format("Unknown BVM submesh ident: {}", ident)};
    }

    auto smesh = Submesh{};
    smesh.name = fmt::format("submesh_{}", smi);

	if ( version >= BvmLayout::VTXMDL_VERSION_GROUPS )
	{
		smesh.flags = reader.readInt<int32_t>();
		smesh.groupid = reader.readInt<int32_t>();
		smesh.slotid = reader.readInt<int32_t>();
	}


    // read counts and offsets.
    smesh.num_skins = reader.readSize<int32_t>();
    smesh.skin_offset = reader.readSize<int32_t>();

    smesh.num_verts = reader.readSize<int32_t>();
    smesh.uv_offset = reader.readSize<int32_t>();

    smesh.num_indices = reader.readSize<int32_t>();
    smesh.indice_offset = reader.readSize<int32_t>();

    smesh.sequence_offset = reader.readSize<int32_t>();

    submeshes.push_back(std::move(smesh));
  }


  // We'll remember per-surface data so we can add meshes after frames are created
  struct SubmeshTemp
  {
    std::string name;
    std::vector<BvmTriangle> triangles;
    std::vector<vm::vec2f> uvs;
    std::vector<std::vector<vm::vec3f>> perFramePos; // size = Ftotal
    std::vector<std::vector<vm::vec3f>> perFrameNrm; // size = Ftotal
  };
  std::vector<SubmeshTemp> tmp_submeshes;
  tmp_submeshes.reserve(submesh_count);

  // parse all submeshes..
  for (size_t smi = 0; smi < submesh_count; smi++)
  {
    auto smesh = submeshes[smi];

    auto tmp_sm = SubmeshTemp{};
    tmp_sm.name = smesh.name;

    auto sm_skins = std::vector<std::string>{};
    if (smesh.num_skins > 0)
    {
      reader.seekFromBegin(smesh.skin_offset);
      for (int sk = 0; sk < smesh.num_skins; sk++)
      {
        const auto sk_ident = reader.readInt<uint32_t>();
        if (sk_ident != BvmLayout::BVM_SKIN_IDENT)
        {
          return Error{fmt::format("Unknown BVM skin ident: {}", sk_ident)};
        }
        const auto sk_diffuse = reader.readString(BvmLayout::MAX_BVM_NAME);
        /*const auto sk_interior = */ reader.readString(BvmLayout::MAX_BVM_NAME);
        /*const auto sk_emission = */ reader.readString(BvmLayout::MAX_BVM_NAME);
        /*const auto sk_emscale = */ reader.readFloat<float>();
        sm_skins.push_back(fmt::format("textures/models/{}.btf", sk_diffuse));
      }
    }

    // now make the entitymodelsurface...
    auto& surface = model.addSurface(std::move(smesh.name), Ftotal);
    loadSkins(surface, sm_skins, fs, logger);

    // grab UVs
    tmp_sm.uvs.reserve(smesh.num_verts);
    reader.seekFromBegin(smesh.uv_offset);
    for (int uvi = 0; uvi < smesh.num_verts; uvi++)
      tmp_sm.uvs.push_back(reader.readVec<float, 2>());


    // triangles
    tmp_sm.triangles.reserve(smesh.num_indices / 3);
    reader.seekFromBegin(smesh.indice_offset);

    //  read them into bvmtriangles
    for (int i = 0; i < smesh.num_indices; i += 3)
    {
      auto tri = BvmTriangle{};
      tri.vertices[0] = reader.readSize<uint32_t>();
      tri.vertices[2] = reader.readSize<uint32_t>();
      tri.vertices[1] = reader.readSize<uint32_t>();
      tmp_sm.triangles.push_back(tri);
    }

    tmp_sm.perFramePos.resize(Ftotal);
    tmp_sm.perFrameNrm.resize(Ftotal);

    // actually read sequences.
    reader.seekFromBegin(smesh.sequence_offset);
    size_t flatIdx = 0;
    for (size_t si = 0; si < seqs.size(); si++)
    {
      for (size_t fi = 0; fi < seqs[si].frames; fi++, flatIdx++)
      {
        auto& posDest = tmp_sm.perFramePos[flatIdx];
        auto& nrmDest = tmp_sm.perFrameNrm[flatIdx];
        posDest.resize(smesh.num_verts);
        nrmDest.resize(smesh.num_verts);
        for (size_t vi = 0; vi < smesh.num_verts; vi++)
        {
          auto pos = reader.readVec<float, 3>();
          posDest[vi] = (pos + origin) * (scale * seqs[si].scale);
          nrmDest[vi] = reader.readVec<float, 3>();
        }
        frameBounds[flatIdx].add(posDest.begin(), posDest.end());
      }
    }

    tmp_submeshes.push_back(std::move(tmp_sm));
  }

  // insert all the frames in the model first.
  for (size_t i = 0; i < Ftotal; ++i)
    model.addFrame(flatFrames[i].name, frameBounds[i].bounds());

  for (size_t i = 0; i < tmp_submeshes.size(); i++)
  {
    auto& tmp_sm = tmp_submeshes[i];


    for (size_t fi = 0; fi < Ftotal; ++fi)
    {
      auto rangeMap = gl::IndexRangeMap{
        gl::PrimType::Triangles, 0, 3 * tmp_sm.triangles.size()};
      const auto& pos = tmp_sm.perFramePos[fi];
      std::vector<mdl::EntityModelVertex> frameVerts;
      frameVerts.reserve(3 * tmp_sm.triangles.size());
      for (const auto& tri : tmp_sm.triangles)
      {
        const auto i0 = tri.vertices[0];
        const auto i1 = tri.vertices[1];
        const auto i2 = tri.vertices[2];
        frameVerts.emplace_back(pos[i0], tmp_sm.uvs[i0]);
        frameVerts.emplace_back(pos[i1], tmp_sm.uvs[i1]);
        frameVerts.emplace_back(pos[i2], tmp_sm.uvs[i2]);
      }

      auto& frames = model.frames();
      auto& surface = model.surface(i);
      surface.addMesh(frames[fi], std::move(frameVerts), std::move(rangeMap));
    }
  }


  return model;
}

Result<EntityModelData> loadBvmModel(
  std::string name, fs::Reader reader, const fs::FileSystem& fs, Logger& logger)
{
  try
  {
    const auto ident = reader.readInt<uint32_t>();
    const auto version = reader.readInt<int32_t>();

    if (ident != BvmLayout::BVM_IDENT)
    {
      return Error{fmt::format("Unknown BVM model ident: {}", ident)};
    }
    if (version > BvmLayout::BVM_CURRENTVERSION)
    {
      return Error{fmt::format("Unknown BVM model version: {}", version)};
    }

    switch (version)
    {
    case 1:
      return loadV1(name, reader, fs, logger);
    case 2:
      return loadV2(reader, fs, logger);
    case 3:
    default:
      return loadCurrent(reader, version, fs, logger);
    }
  }
  catch (const fs::ReaderException& e)
  {
    return Error{e.what()};
  }
}

} // namespace tb::mdl
