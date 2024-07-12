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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_RANGE_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_RANGE_PARSER_H

#include <array>
#include <variant>
#include <vector>
#include <string>
#include <limits>

#include <tinyxml2.h>

#include "metadata_visitor.h"
#include "enum_parser.h"
#include "value_parser.h"
#include "minmax_parser.h"

using namespace tinyxml2;

template<class T>
class RangeParser {
  using Range = std::variant<std::monostate, std::array<T, 2>, std::vector<T>>;

public:
  static int parse(const XMLElement *elem,
                   const MetadataVisitor &visitor,
                   const EnumParser::Enum &enumMap, Range &res);

private:
  static int parseValuesRef(const std::string &ref,
                            const MetadataVisitor &visitor,
                            const EnumParser::Enum &enumMap,
                            Range &res);

  static int parseValues(const XMLElement *rangeElem,
                         const EnumParser::Enum &enumMap, Range &res);
};

template<class T>
int RangeParser<T>::parse(const XMLElement *elem,
                          const MetadataVisitor &visitor,
                          const EnumParser::Enum &enumMap, Range &res) {
  const XMLElement *rangeElem = elem->FirstChildElement("range");
  if (rangeElem == nullptr)
    return 0;

  const XMLAttribute *minmaxAttr = rangeElem->FindAttribute("minmax");
  if (minmaxAttr != nullptr)
    return MinMaxParser<T>::parse(minmaxAttr, visitor, enumMap, res);

  const XMLAttribute *minAttr = rangeElem->FindAttribute("min");
  const XMLAttribute *maxAttr = rangeElem->FindAttribute("max");
  if (minAttr != nullptr || maxAttr != nullptr)
    return MinMaxParser<T>::parse(minAttr, maxAttr, visitor, enumMap, res);

  const char *ref = rangeElem->GetText();
  if (ref != nullptr)
    return parseValuesRef(ref, visitor, enumMap, res);

  return parseValues(rangeElem, enumMap, res);
}

template<class T>
int RangeParser<T>::parseValuesRef(const std::string &ref,
                                   const MetadataVisitor &visitor,
                                   const EnumParser::Enum &enumMap,
                                   Range &res) {
  const XMLElement *elem = visitor.find(ref);
  if (elem == nullptr) {
    ALOGE("%s: cannot find '%s' element", __func__, ref.c_str());
    return -1;
  }

  std::vector<T> values;
  int err = ValueParser<std::vector<T>>::parse(elem, enumMap, values);
  if (err != 0) {
    ALOGE("%s: error while parsing value from '%s' (line: %d)",
              __func__, ref.c_str(), elem->GetLineNum());
    return err;
  }

  res.template emplace<std::vector<T>>(std::move(values));

  return 0;
}

template<class T>
int RangeParser<T>::parseValues(const XMLElement *rangeElem,
                                const EnumParser::Enum &enumMap, Range &res) {
  std::vector<T> values;

  int err = ValueParser<std::vector<T>>::parse(rangeElem, enumMap, values);
  if (err != 0) {
    ALOGE("%s: error while parsing range values (line: %d)",
              __func__, rangeElem->GetLineNum());
    return err;
  }

  res.template emplace<std::vector<T>>(std::move(values));

  return 0;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_RANGE_PARSER_H
