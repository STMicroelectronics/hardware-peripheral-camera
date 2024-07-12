/*
 * Copyright (C) 2023 STMicroelectronics
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HARDWARE_CAMERA_METADATA_PARSER_ENUM_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_ENUM_PARSER_H

#include <unordered_map>

#include <tinyxml2.h>

#include "metadata_visitor.h"

using namespace tinyxml2;

class EnumParser {
public:
  using Enum = std::unordered_map<std::string, int64_t>;

public:
  static int parse(const XMLElement *elem,
                   const MetadataVisitor &visitor, Enum &res);

private:
  static int parseRef(const std::string &ref,
                      const MetadataVisitor &visitor, Enum &res);
  static int parseValues(const XMLElement *enumElem, Enum &res);
};

#endif // HARDWARE_CAMERA_METADATA_PARSER_ENUM_PARSER_H
