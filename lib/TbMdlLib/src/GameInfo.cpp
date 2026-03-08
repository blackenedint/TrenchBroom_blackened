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

#include "mdl/GameInfo.h"
// BEGIN #BLACKENED
#include "PreferenceManager.h"
// END #BLACKENED

#include "kd/reflection_impl.h"

namespace tb::mdl
{

kdl_reflect_impl(GameInfo);

GameInfo makeGameInfo(GameConfig gameConfig)
{
  const auto gamePathPrefPath = std::filesystem::path{"Games"} / gameConfig.name / "Path";
  const auto defaultEnginePrefPath =
    std::filesystem::path{"Games"} / gameConfig.name / "Default Engine";

  return GameInfo{
    std::move(gameConfig),
    Preference<std::filesystem::path>{gamePathPrefPath, {}},
    Preference<std::filesystem::path>{defaultEnginePrefPath, {}},
  };
}

// BEGIN #BLACKENED
// use these to get the paths, instead of reading the preferences directly
// so that the path tokens will resolve.
std::filesystem::path GameInfo::getGamePath() const
{
#if defined( BLACKENED )
  const auto gamePath = pref(gamePathPreference);
  auto pathStr = gamePath.string();
  if (auto p = PreferenceManager::resolveSpecialGamePathToken(pathStr))
    return *p;
#endif
  return pref(gamePathPreference);
}
std::filesystem::path GameInfo::getDefaultEnginePath() const
{
#if defined( BLACKENED )
  const auto gamePath = pref(defaultEnginePathPreference);
  auto pathStr = gamePath.string();
  if (auto p = PreferenceManager::resolveSpecialGamePathToken(pathStr))
    return *p;
#endif
  return pref(defaultEnginePathPreference);
}
// same as the other gameconfig; get path with special tokens resolved.
std::filesystem::path CompilationTool::getResolvedPath() const
{
  // todo; just remove this; realized it doesn't matter; unless i add full expansion for
  // resolveSpecialGamePathToken #if defined( BLACKENED )
  //  if (
  //    auto p =
  //      PreferenceManager::resolveSpecialGamePathToken(pref(pathPreference).path.string()))
  //    return *p;
  // #endif
  return pref(pathPreference);
}
// END #BLACKENED

} // namespace tb::mdl
