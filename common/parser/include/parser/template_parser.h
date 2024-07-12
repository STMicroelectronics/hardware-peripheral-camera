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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_TEMPLATE_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_TEMPLATE_PARSER_H

#include <aidl/android/hardware/camera/device/RequestTemplate.h>

#include <map>

#include <tinyxml2.h>

#include "enum_parser.h"
#include "value_parser.h"

using aidl::android::hardware::camera::device::RequestTemplate;

using namespace tinyxml2;

template<class T>
class TemplateParser {
public:
  using Templates = std::map<int, T>;

public:
  static int parse(const XMLElement *elem,
                   const EnumParser::Enum &enumMap, Templates &res);

private:
  static int parseName(const XMLElement *templateElem, int &res);
};

template<class T>
int TemplateParser<T>::parse(const XMLElement *elem,
                             const EnumParser::Enum &enumMap, Templates &res) {
  for (const XMLElement *templateElem = elem->FirstChildElement("template");
       templateElem != nullptr;
       templateElem = templateElem->NextSiblingElement("template")) {
    int id;
    int err = parseName(templateElem, id);
    if (err != 0)
      return err;

    T value;
    err = ValueParser<T>::parseValue(templateElem, enumMap, value);
    if (err != 0) {
      ALOGE("%s: cannot parse template value (line: %d)",
                __func__, templateElem->GetLineNum());
      return -1;
    }

    res[id] = value;
  }

  return 0;
}

template<class T>
int TemplateParser<T>::parseName(const XMLElement *templateElem, int &res) {
  const char *name = templateElem->Attribute("name");
  if (name == nullptr) {
    res = OTHER_TEMPLATES;
    return 0;
  }

  if (strcmp(name, "preview") == 0) {
    res = static_cast<int>(RequestTemplate::PREVIEW);
  } else if (strcmp(name, "still-capture") == 0) {
    res = static_cast<int>(RequestTemplate::STILL_CAPTURE);
  } else if (strcmp(name, "video-record") == 0) {
    res = static_cast<int>(RequestTemplate::VIDEO_RECORD);
  } else if (strcmp(name, "video-snapshot") == 0) {
    res = static_cast<int>(RequestTemplate::VIDEO_SNAPSHOT);
  } else if (strcmp(name, "0-shutter-lag") == 0) {
    res = static_cast<int>(RequestTemplate::ZERO_SHUTTER_LAG);
  } else if (strcmp(name, "manual") == 0) {
    res = static_cast<int>(RequestTemplate::MANUAL);
  } else if (strcmp(name, "other") == 0) {
    res = OTHER_TEMPLATES;
  } else {
    ALOGE("%s: template name not recognized '%s' (line: %d)",
                __func__, name, templateElem->GetLineNum());
    return -1;
  }

  return 0;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_TEMPLATE_PARSER_H
