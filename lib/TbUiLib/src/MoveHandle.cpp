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

#include "ui/MoveHandle.h"

#include "PreferenceManager.h"
#include "Preferences.h"
#include "gl/Camera.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"

#include "vm/vec.h"

#include <limits>

namespace tb::ui
{
namespace
{
constexpr double kHandleLength = 64.0;
constexpr float kHighlightMix = 0.35f;
} // namespace

const mdl::HitType::Type MoveHandle::HandleHitType = mdl::HitType::freeType();

MoveHandle::MoveHandle()
  : m_position{0, 0, 0}
{
}

const vm::vec3d& MoveHandle::position() const
{
  return m_position;
}

void MoveHandle::setPosition(const vm::vec3d& position)
{
  m_position = position;
}

vm::vec3d MoveHandle::axisDirection(const HitArea area) const
{
  switch (area)
  {
  case HitArea::XAxis:
    return vm::vec3d::axis(0);
  case HitArea::YAxis:
    return vm::vec3d::axis(1);
  case HitArea::ZAxis:
    return vm::vec3d::axis(2);
  case HitArea::None:
    break;
  }
  return vm::vec3d{0, 0, 0};
}

double MoveHandle::handleLength(const gl::Camera& camera) const
{
  const auto scale = double(camera.perspectiveScalingFactor(vm::vec3f{m_position}));
  return kHandleLength * scale;
}

double MoveHandle::handleRadius() const
{
  return double(pref(Preferences::MoveHandleRadius));
}

vm::segment3d MoveHandle::axisSegment(const gl::Camera& camera, const HitArea area) const
{
  const auto axis = axisDirection(area);
  const auto end = m_position + axis * handleLength(camera);
  return vm::segment3d{m_position, end};
}

mdl::Hit MoveHandle::pick3D(const vm::ray3d& pickRay, const gl::Camera& camera) const
{
  auto best = mdl::Hit::NoHit;
  auto bestDist = std::numeric_limits<double>::max();
  const auto radius = handleRadius();

  for (const auto area : {HitArea::XAxis, HitArea::YAxis, HitArea::ZAxis})
  {
    const auto segment = axisSegment(camera, area);
    if (const auto dist = camera.pickLineSegmentHandle(pickRay, segment, radius))
    {
      if (*dist < bestDist)
      {
        bestDist = *dist;
        const auto hitPoint = vm::point_at_distance(pickRay, *dist);
        best = mdl::Hit{HandleHitType, *dist, hitPoint, area};
      }
    }
  }

  return best;
}

void MoveHandle::renderHandle3D(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setShowOccludedObjectsTransparent();
  // todo; fix this..i may need to use a different range.
  //  error: conversion from ‘double’ to ‘float’ may change value
  //  [-Werror=float-conversion]
  // adding cast.
  renderService.setLineWidth(static_cast<float>(handleRadius()) * 2.0f);

  const auto& camera = renderContext.camera();
  for (const auto area : {HitArea::XAxis, HitArea::YAxis, HitArea::ZAxis})
  {
    Color color;
    switch (area)
    {
    case HitArea::XAxis:
      color = pref(Preferences::XAxisColor);
      break;
    case HitArea::YAxis:
      color = pref(Preferences::YAxisColor);
      break;
    case HitArea::ZAxis:
      color = pref(Preferences::ZAxisColor);
      break;
    case HitArea::None:
      color = pref(Preferences::NoAxisColor);
      break;
    }

    renderService.setForegroundColor(color);
    const auto segment = axisSegment(camera, area);
    renderService.renderLine(vm::vec3f{segment.start()}, vm::vec3f{segment.end()});
    renderService.renderHandle(vm::vec3f{segment.end()});
  }
}

void MoveHandle::renderHighlight3D(
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch,
  const HitArea area) const
{
  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setShowOccludedObjectsTransparent();
  // todo; fix this..i may need to use a different range.
  // error: conversion from ‘double’ to ‘float’ may change value
  // [-Werror=float-conversion]
  // adding cast.
  renderService.setLineWidth((static_cast<float>(handleRadius()) * 2.0f) + 1.0f);

  Color color;
  switch (area)
  {
  case HitArea::XAxis:
    color = pref(Preferences::XAxisColor);
    break;
  case HitArea::YAxis:
    color = pref(Preferences::YAxisColor);
    break;
  case HitArea::ZAxis:
    color = pref(Preferences::ZAxisColor);
    break;
  case HitArea::None:
    return;
  }
  color = mixColors(color.to<RgbaF>(), color.to<RgbaF>(), kHighlightMix);

  renderService.setForegroundColor(color);
  const auto segment = axisSegment(renderContext.camera(), area);
  renderService.renderLine(vm::vec3f{segment.start()}, vm::vec3f{segment.end()});
  renderService.renderHandleHighlight(vm::vec3f{segment.end()});
}

} // namespace tb::ui
