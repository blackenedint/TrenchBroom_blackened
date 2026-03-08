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

#pragma once

#include "Macros.h"
#include "mdl/Hit.h"
#include "mdl/HitType.h"

#include "vm/ray.h"
#include "vm/segment.h"
#include "vm/vec.h"

namespace tb::gl
{
class Camera;
}
namespace tb::render
{
class RenderBatch;
class RenderContext;
} // namespace tb::render

namespace tb::ui
{
class MoveHandle
{
public:
  static const mdl::HitType::Type HandleHitType;

  enum class HitArea
  {
    None = 0,
    XAxis = 1,
    YAxis = 2,
    ZAxis = 3
  };

private:
  vm::vec3d m_position;

public:
  MoveHandle();

  const vm::vec3d& position() const;
  void setPosition(const vm::vec3d& position);

  mdl::Hit pick3D(const vm::ray3d& pickRay, const gl::Camera& camera) const;

  void renderHandle3D(
    render::RenderContext& renderContext, render::RenderBatch& renderBatch) const;
  void renderHighlight3D(
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch,
    HitArea area) const;

  vm::vec3d axisDirection(HitArea area) const;

private:
  double handleLength(const gl::Camera& camera) const;
  double handleRadius() const;

  vm::segment3d axisSegment(const gl::Camera& camera, HitArea area) const;
};

} // namespace tb::ui
