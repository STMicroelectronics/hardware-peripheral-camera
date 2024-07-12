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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_MINMAX_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_MINMAX_PARSER_H

#include <array>
#include <vector>

#include <tinyxml2.h>

#include "enum_parser.h"
#include "string_parser.h"

template<class T>
class MinMaxParser {
  using Range = std::variant<std::monostate, std::array<T, 2>, std::vector<T>>;

public:
  static int parse(const XMLAttribute *minAttr,
                   const XMLAttribute *maxAttr,
                   const MetadataVisitor &visitor,
                   const EnumParser::Enum &enumMap,
                   Range &res);

  static int parse(const XMLAttribute *minmaxAttr,
                   const MetadataVisitor &visitor,
                   const EnumParser::Enum &enumMap, Range &res);

private:
  static int parseValue(const XMLAttribute *attr,
                        const MetadataVisitor &visitor,
                        const EnumParser::Enum &enumMap, T &res);

  static int parseValue(const XMLAttribute *minmaxAttr,
                        const MetadataVisitor &visitor,
                        const EnumParser::Enum &enumMap,
                        std::array<T, 2> &res);
};


template<class T>
class MinMaxParser<std::vector<T>> {
  using Range = std::variant<std::monostate, std::array<std::vector<T>, 2>, std::vector<std::vector<T>>>;

public:
  static int parse(const XMLAttribute *minAttr, const XMLAttribute *,
                   const MetadataVisitor &, const EnumParser::Enum &,
                   Range &) {
    ALOGE("%s: min/max is not supported with vector (line: %d)",
              __func__, minAttr->GetLineNum());
    return -1;
  }
  static int parse(const XMLAttribute *minmaxAttr,
                   const MetadataVisitor &, const EnumParser::Enum &, Range &) {
    ALOGE("%s: minmax is not supported with vector (line: %d)",
              __func__, minmaxAttr->GetLineNum());
    return -1;
  }
};

template<class T, size_t size>
class MinMaxParser<std::array<T, size>> {
  using Range = std::variant<std::monostate, std::array<std::array<T, size>, 2>, std::vector<std::array<T, size>>>;

public:
  static int parse(const XMLAttribute *minAttr, const XMLAttribute *,
                   const MetadataVisitor &, const EnumParser::Enum &,
                   Range &) {
    ALOGE("%s: min/max is not supported with array (line: %d)",
              __func__, minAttr->GetLineNum());
    return -1;
  }
  static int parse(const XMLAttribute *minmaxAttr,
                   const MetadataVisitor &, const EnumParser::Enum &, Range &) {
    ALOGE("%s: minmax is not supported with array (line: %d)",
              __func__, minmaxAttr->GetLineNum());
    return -1;
  }
};

template<class T>
int MinMaxParser<T>::parse(const XMLAttribute *minAttr,
                           const XMLAttribute *maxAttr,
                           const MetadataVisitor &visitor,
                           const EnumParser::Enum &enumMap,
                           Range &res) {
  T min = std::numeric_limits<T>::min();
  if (minAttr != nullptr) {
    int err = parseValue(minAttr, visitor, enumMap, min);
    if (err != 0) {
      ALOGE("%s: error while parsing min attribute '%s' (line: %d)",
                __func__, minAttr->Value(), minAttr->GetLineNum());
      return err;
    }
  }

  T max = std::numeric_limits<T>::max();
  if (maxAttr != nullptr) {
    int err = parseValue(maxAttr, visitor, enumMap, max);
    if (err != 0) {
      ALOGE("%s: error while parsing max attribute '%s' (line: %d)",
                __func__, maxAttr->Value(), maxAttr->GetLineNum());
      return err;
    }
  }

  std::array<T, 2> values{min, max};
  res.template emplace<std::array<T, 2>>(std::move(values));

  return 0;
}

template<class T>
int MinMaxParser<T>::parseValue(const XMLAttribute *attr,
                                const MetadataVisitor &visitor,
                                const EnumParser::Enum &enumMap, T &res) {
  const char *minmax = attr->Value();
  const XMLElement *elem = visitor.find(minmax);
  if (elem != nullptr)
    return ValueParser<T>::parse(elem, enumMap, res);

  return StringParser<T>::parse(attr, enumMap, res);
}

template<class T>
int MinMaxParser<T>::parse(const XMLAttribute *minmaxAttr,
                           const MetadataVisitor &visitor,
                           const EnumParser::Enum &enumMap, Range &res) {

  std::array<T, 2> values;
  int err = parseValue(minmaxAttr, visitor, enumMap, values);
  if (err != 0) {
    ALOGE("%s: error while parsing minmax attribute %s (line: %d)",
              __func__, minmaxAttr->Value(), minmaxAttr->GetLineNum());
    return err;
  }

  res.template emplace<std::array<T, 2>>(std::move(values));

  return 0;
}

template<class T>
int MinMaxParser<T>::parseValue(const XMLAttribute *minmaxAttr,
                                const MetadataVisitor &visitor,
                                const EnumParser::Enum &enumMap,
                                std::array<T, 2> &res) {

  const char *minmax = minmaxAttr->Value();
  const XMLElement *elem = visitor.find(minmax);
  if (elem != nullptr)
    return ValueParser<std::array<T, 2>>::parse(elem, enumMap, res);

  T value;
  int err = StringParser<T>::parse(minmaxAttr, enumMap, value);
  if (err != 0)
    return err;

  res.fill(value);

  return 0;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_MINMAX_PARSER_H
