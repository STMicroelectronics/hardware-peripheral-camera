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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_STATIC_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_STATIC_PARSER_H

#include <metadata/property.h>

#include <tinyxml2.h>

#include "metadata_visitor.h"
#include "enum_parser.h"
#include "value_parser.h"
#include "parser_utils.h"

template<class T>
class StaticParser {
public:
  static int parse(const std::string &tag, const XMLElement *entryElem,
                   const MetadataVisitor &visitor,
                   std::unique_ptr<PartialMetadataInterface> &res);
};


template<class T>
int StaticParser<T>::parse(const std::string &tag, const XMLElement *entryElem,
                           const MetadataVisitor &visitor,
                           std::unique_ptr<PartialMetadataInterface> &res) {
  EnumParser::Enum enumMap;
  EnumParser::parse(entryElem, visitor, enumMap);

  T val;
  int err = ValueParser<T>::parse(entryElem, enumMap, val);
  if (err != 0)
    return err;

  uint32_t t;
  err = ParserUtils::getTagFromName(tag.c_str(), &t);
  if (err) {
    ALOGE("%s: cannot find tag '%s' (line: %d)",
              __func__, tag.c_str(), entryElem->GetLineNum());
    return -1;
  }

  res = std::unique_ptr<PartialMetadataInterface>(
      new Property<T>(t, val));

  return 0;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_STATIC_PARSER_H
