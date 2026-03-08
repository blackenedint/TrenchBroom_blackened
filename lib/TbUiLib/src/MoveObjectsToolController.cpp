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

#include "ui/MoveObjectsToolController.h"

#include "mdl/EntityNode.h"
#include "mdl/Grid.h"
#include "mdl/Hit.h"
#include "mdl/HitFilter.h"
#include "mdl/Map.h"
#include "mdl/ModelUtils.h"
#include "render/RenderContext.h"
#include "ui/GestureTracker.h"
#include "ui/HandleDragTracker.h"
#include "ui/MoveHandleDragTracker.h"
#include "ui/MoveObjectsTool.h"
#include "vm/line.h"

#include <cassert>
#include <optional>

namespace tb::ui
{
namespace
{

class MoveObjectsDragDelegate : public MoveHandleDragTrackerDelegate
{
private:
  MoveObjectsTool& m_tool;

public:
  explicit MoveObjectsDragDelegate(MoveObjectsTool& tool)
    : m_tool{tool}
  {
  }

  DragStatus move(
    const InputState& inputState,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override
  {
    switch (
      m_tool.move(inputState, proposedHandlePosition - dragState.currentHandlePosition))
    {
    case MoveObjectsTool::MoveResult::Continue:
      return DragStatus::Continue;
    case MoveObjectsTool::MoveResult::Deny:
      return DragStatus::Deny;
    case MoveObjectsTool::MoveResult::Cancel:
      return DragStatus::End;
      switchDefault();
    }
  }

  void end(const InputState& inputState, const DragState&) override
  {
    m_tool.endMove(inputState);
  }

  void cancel(const DragState&) override { m_tool.cancelMove(); }

  void setRenderOptions(
    const InputState&, render::RenderContext& renderContext) const override
  {
    renderContext.setForceShowSelectionGuide();
  }

  DragHandleSnapper makeDragHandleSnapper(
    const InputState&, const SnapMode) const override
  {
    return makeRelativeHandleSnapper(m_tool.grid());
  }
};

// BEGIN #BLACKENED
class MoveAxisDragDelegate : public HandleDragTrackerDelegate
{
private:
  MoveObjectsTool& m_tool;
  vm::line3d m_line;

public:
  MoveAxisDragDelegate(MoveObjectsTool& tool, const vm::line3d& line)
    : m_tool{tool}
    , m_line{line}
  {
  }

  HandlePositionProposer start(
    const InputState&,
    const vm::vec3d& /* initialHandlePosition */,
    const vm::vec3d& handleOffset) override
  {
    return makeHandlePositionProposer(
      makeLineHandlePicker(m_line, handleOffset),
      makeRelativeLineHandleSnapper(m_tool.grid(), m_line));
  }

  DragStatus update(
    const InputState& inputState,
    const DragState& dragState,
    const vm::vec3d& proposedHandlePosition) override
  {
    switch (
      m_tool.move(inputState, proposedHandlePosition - dragState.currentHandlePosition))
    {
    case MoveObjectsTool::MoveResult::Continue:
      return DragStatus::Continue;
    case MoveObjectsTool::MoveResult::Deny:
      return DragStatus::Deny;
    case MoveObjectsTool::MoveResult::Cancel:
      return DragStatus::End;
      switchDefault();
    }
  }

  void end(const InputState& inputState, const DragState&) override
  {
    m_tool.endMove(inputState);
  }

  void cancel(const DragState&) override { m_tool.cancelMove(); }

  void setRenderOptions(
    const InputState&, render::RenderContext& renderContext) const override
  {
    renderContext.setForceShowSelectionGuide();
  }
};

// END #BLACKENED
} // namespace

MoveObjectsToolController::MoveObjectsToolController(MoveObjectsTool& tool)
  : m_tool{tool}
{
}

MoveObjectsToolController::~MoveObjectsToolController() = default;

Tool& MoveObjectsToolController::tool()
{
  return m_tool;
}

const Tool& MoveObjectsToolController::tool() const
{
  return m_tool;
}

// BEGIN #BLACKENED
void MoveObjectsToolController::pick(
  const InputState& inputState, mdl::PickResult& pickResult)
{
  if (!inputState.camera().perspectiveProjection())
  {
    return;
  }

  if (!updateHandlePosition())
  {
    return;
  }

  const auto hit = m_handle.pick3D(inputState.pickRay(), inputState.camera());
  if (hit.isMatch())
  {
    pickResult.addHit(hit);
  }
}
// END #BLACKENED

std::unique_ptr<GestureTracker> MoveObjectsToolController::acceptMouseDrag(
  const InputState& inputState)
{
  using namespace mdl::HitFilters;

  if (
    !inputState.modifierKeysPressed(ModifierKeys::None)
    && !inputState.modifierKeysPressed(ModifierKeys::Alt)
    && !inputState.modifierKeysPressed(ModifierKeys::CtrlCmd)
    && !inputState.modifierKeysPressed(ModifierKeys::CtrlCmd | ModifierKeys::Alt))
  {
    return nullptr;
  }

  // BEGIN #BLACKENED
  if (inputState.camera().perspectiveProjection())
  {
    if (!updateHandlePosition())
    {
      return nullptr;
    }

    if (const auto& hit = inputState.pickResult().first(type(MoveHandle::HandleHitType));
        hit.isMatch())
    {
      if (m_tool.startMove(inputState))
      {
        const auto axis = m_handle.axisDirection(hit.target<MoveHandle::HitArea>());
        const auto line = vm::line3d{m_handle.position(), axis};
        return createHandleDragTracker(
          MoveAxisDragDelegate{m_tool, line},
          inputState,
          m_handle.position(),
          hit.hitPoint());
      }
    }

    return nullptr;
  }
  // END #BLACKENED

  // The transitivelySelected() lets the hit query match entities/brushes inside a
  // selected group, even though the entities/brushes aren't selected themselves.

  if (const auto& hit =
        inputState.pickResult().first(type(mdl::nodeHitType()) && transitivelySelected());
      hit.isMatch())
  {
    if (m_tool.startMove(inputState))
    {
      return createMoveHandleDragTracker(
        MoveObjectsDragDelegate{m_tool},
        inputState,
        hit.hitPoint(),
        hit.hitPoint());
    }
  }

  return nullptr;
}

// BEGIN #BLACKENED
void MoveObjectsToolController::setRenderOptions(
  const InputState& inputState, render::RenderContext& renderContext) const
{
  using namespace mdl::HitFilters;
  if (
    inputState.camera().perspectiveProjection()
    && inputState.pickResult().first(type(MoveHandle::HandleHitType)).isMatch())
  {
    renderContext.setForceShowSelectionGuide();
  }
}

void MoveObjectsToolController::render(
  const InputState& inputState,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  if (!inputState.camera().perspectiveProjection())
  {
    return;
  }

  if (!updateHandlePosition())
  {
    return;
  }

  m_handle.renderHandle3D(renderContext, renderBatch);

  if (!inputState.anyToolDragging())
  {
    using namespace mdl::HitFilters;
    const auto& hit = inputState.pickResult().first(type(MoveHandle::HandleHitType));
    if (hit.isMatch())
    {
      m_handle.renderHighlight3D(
        renderContext, renderBatch, hit.target<MoveHandle::HitArea>());
    }
  }
}
// END #BLACKENED

bool MoveObjectsToolController::cancel()
{
  return false;
}

// BEGIN #BLACKENED
std::optional<vm::vec3d> MoveObjectsToolController::handlePosition() const
{
  const auto& map = m_tool.map();
  const auto& selection = map.selection();
  if (!selection.hasNodes())
  {
    return std::nullopt;
  }

  if (selection.hasOnlyEntities() && selection.entities.size() == 1)
  {
    return selection.entities.front()->entity().origin();
  }

  if (const auto bounds = map.selectionBounds())
  {
    return map.grid().snap(bounds->center());
  }

  return std::nullopt;
}

bool MoveObjectsToolController::updateHandlePosition()
{
  if (const auto position = handlePosition())
  {
    m_handle.setPosition(*position);
    return true;
  }
  return false;
}

// END #BLACKENED

} // namespace tb::ui
