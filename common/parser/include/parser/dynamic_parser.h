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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_DYNAMIC_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_DYNAMIC_PARSER_H

#include "enum_parser.h"
#include "delegate_parser.h"

template<class T>
class DynamicParser {
public:
  static int parse(const std::string &tag, const XMLElement *entryElem,
                   const MetadataVisitor &visitor,
                   std::unique_ptr<PartialMetadataInterface> &res);
};

template<class T>
class DynamicParser<std::vector<T>> {
public:
  static int parse(const std::string &tag, const XMLElement *entryElem,
                   const MetadataVisitor &,
                   std::unique_ptr<PartialMetadataInterface> &) {
    ALOGE("%s: entry '%s' is a vector which is not supported for dynamics (line: %d)",
                __func__, tag.c_str(), entryElem->GetLineNum());

    return -1;
  }
};

template<class T>
int DynamicParser<T>::parse(const std::string &tag, const XMLElement *entryElem,
                            const MetadataVisitor &visitor,
                            std::unique_ptr<PartialMetadataInterface> &res) {
  uint32_t t;
  int err = ParserUtils::getTagFromName(tag.c_str(), &t);
  if (err) {
    ALOGE("%s: cannot find tag '%s' (line: %d)",
              __func__, tag.c_str(), entryElem->GetLineNum());
    return err;
  }

  /* parse enum */
  EnumParser::Enum enumMap;
  EnumParser::parse(entryElem, visitor, enumMap);

  /* parse delegate */
  std::unique_ptr<StateDelegateInterface<T>> delegate;
  err = DelegateParser<T>::parse(entryElem, enumMap, delegate);
  if (err != 0) {
    ALOGE("%s: cannot parse delegate (line: %d)",
              __func__, entryElem->GetLineNum());
    return err;
  }

  res = std::unique_ptr<PartialMetadataInterface>(
      new State<T>(t, std::move(delegate)));

  return 0;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_DYNAMIC_PARSER_H
