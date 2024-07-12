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

#include "enum_parser.h"

#include <log/log.h>

#include "string_parser.h"

int EnumParser::parse(const XMLElement *elem,
                      const MetadataVisitor &visitor, Enum &res) {
  if (elem == nullptr) {
    ALOGE("%s: cannot parse enum on null element", __func__);
    return -1;
  }

  for (const XMLElement *enumElem = elem->FirstChildElement("enum");
       enumElem != nullptr;
       enumElem = enumElem->NextSiblingElement("enum")) {
    const char *str = enumElem->GetText();
    if (str != nullptr) {
      int err = parseRef(str, visitor, res);
      if (err != 0) {
        ALOGE("%s: error while parsing enum from '%s' (line: %d)",
                  __func__, str, enumElem->GetLineNum());
        return -1;
      }
    } else {
      int err = parseValues(enumElem, res);
      if (err != 0) {
        ALOGE("%s: error while parsing enum values (line: %d)",
                  __func__, enumElem->GetLineNum());
        return -1;
      }
    }
  }

  return 0;
}

int EnumParser::parseRef(const std::string &ref,
                         const MetadataVisitor &visitor, Enum &res) {
  const XMLElement *elem = visitor.find(ref);
  if (elem == nullptr) {
    ALOGE("%s: cannot find '%s' element", __func__, ref.c_str());
    return -1;
  }

  return parse(elem, visitor, res);
}

int EnumParser::parseValues(const XMLElement *enumElem, Enum &res) {

  int64_t id = 0;
  for (const XMLElement *valueElem = enumElem->FirstChildElement("value");
      valueElem != nullptr;
      valueElem = valueElem->NextSiblingElement("value"), ++id) {
    const char *value = valueElem->GetText();
    if (value == nullptr) {
      ALOGE("%s: no valid value found for enum (line: %d)",
                __func__, valueElem->GetLineNum());
      return -1;
    }

    const XMLAttribute *idAttr = valueElem->FindAttribute("id");
    if (idAttr != nullptr) {
      int64_t aux;
      int err = StringParser<int64_t>::parse(idAttr, {}, aux);
      if (err) {
        ALOGE("%s: cannot parse id of enum '%s' (line: %d)",
                    __func__, value, valueElem->GetLineNum());
        return -1;
      }
      id = aux;
    }

    res[std::string(value)] = id;
  }

  return 0;
}


