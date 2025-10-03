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

#include "io/EntityModelLoader.h"
#include "mdl/EntityModel_Forward.h"

#include <filesystem>
#include <string>

namespace tb::io
{
class FileSystem;
class Reader;

class BvmLoader : public EntityModelLoader
{
private:
  std::string m_name;
  const Reader& m_reader;
  const FileSystem& m_fs;

public:
  BvmLoader(std::string name, const Reader& reader, const FileSystem& fs);

  static bool canParse(const std::filesystem::path& path, Reader reader);

  Result<mdl::EntityModelData> load(Logger& logger) override;

private:
  // backward compatability with older versions.
  Result<mdl::EntityModelData> loadV1(Reader& reader, Logger& logger);
  Result<mdl::EntityModelData> loadV2(Reader& reader, Logger& logger);
  Result<mdl::EntityModelData> loadCurrent(Reader& reader, Logger& logger);
};

} // namespace tb::io
