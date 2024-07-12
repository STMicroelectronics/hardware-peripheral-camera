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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_VALUE_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_VALUE_PARSER_H

#include <vector>

#include <tinyxml2.h>

#include "string_parser.h"

using namespace tinyxml2;

template<class T>
class ValueParser {
friend ValueParser<std::vector<T>>;

public:
  static int parse(const XMLElement *element,
                   const EnumParser::Enum &enumMap, T &res);

  static int parseValue(const XMLElement *valueElem,
                        const EnumParser::Enum &enumMap, T &res);

};

template<class T>
class ValueParser<std::vector<T>> {
public:
  static int parse(const XMLElement *element,
                   const EnumParser::Enum &enumMap, std::vector<T> &res);

  static int parseValue(const XMLElement *valueElem,
                        const EnumParser::Enum &enumMap, std::vector<T> &res);

};

template<class T>
int ValueParser<T>::parse(const XMLElement *element,
                          const EnumParser::Enum &enumMap, T &res) {
  const XMLElement *valueElem = element->FirstChildElement("value");
  if (valueElem == nullptr) {
    ALOGE("%s: cannot find 'value' element (line: %d)",
              __func__, element->GetLineNum());
    return -1;
  }

  return StringParser<T>::parse(valueElem, enumMap, res);
}

/* std::vector partial specialization */
template<class T>
int ValueParser<std::vector<T>>::parse(const XMLElement *element,
                                       const EnumParser::Enum &enumMap,
                                       std::vector<T> &res) {
  for (const XMLElement *valueElem = element->FirstChildElement("value");
       valueElem != nullptr;
       valueElem = valueElem->NextSiblingElement("value")) {
    T val;
    int err = ValueParser<T>::parseValue(valueElem, enumMap, val);
    if (err != 0) {
      ALOGE("%s: error while parsing value (line: %d)",
                __func__, valueElem->GetLineNum());
      return err;
    }

    res.push_back(val);
  }

  return 0;
}

template<class T>
int ValueParser<T>::parseValue(const XMLElement *valueElem,
                               const EnumParser::Enum &enumMap,
                               T &res) {
  return StringParser<T>::parse(valueElem, enumMap, res);
}

template<class T>
int ValueParser<std::vector<T>>::parseValue(const XMLElement *valueElem,
                                            const EnumParser::Enum &enumMap,
                                            std::vector<T> &res) {
  return parse(valueElem, enumMap, res);
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_VALUE_PARSER_H
