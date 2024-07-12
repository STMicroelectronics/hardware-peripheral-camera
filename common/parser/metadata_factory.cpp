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

// #define LOG_NDEBUG 0

#include "metadata_factory.h"

#include <log/log.h>

#include "static_parser.h"
#include "dynamic_parser.h"
#include "control_parser.h"

int MetadataFactory::load(const char *cfgPath) {
  XMLError err = configXml_.LoadFile(cfgPath);
  if (err != XML_SUCCESS) {
    ALOGE("%s: Unable to load camera config file '%s' ! Error: %s",
              __func__, cfgPath, XMLDocument::ErrorIDToName(err));
    return -1;
  }

  ALOGI("%s: configuration file load !", __func__);
  return 0;
}

int MetadataFactory::parse(std::unique_ptr<Metadata> &metadata) {
  configXml_.Accept(&visitor_);

  components_ = PartialMetadataSet();
  current_ = Static;
  int err = parse(visitor_.statics());
  if (err != 0)
    return err;

  current_ = Dynamic;
  err = parse(visitor_.dynamics());
  if (err != 0)
    return err;

  current_ = Control;
  err = parse(visitor_.controls());
  if (err != 0)
    return err;

  metadata = std::make_unique<Metadata>(std::move(components_));

  return 0;
}

int MetadataFactory::parse(const MetadataVisitor::ElementMap &map) {
  for (const auto &p : map) {
    const XMLElement *entryElem = p.second;
    const char *type = entryElem->Attribute("type");
    if (type == nullptr) {
      ALOGE("%s: entry must have a type (line: %d)",
                __func__, entryElem->GetLineNum());
      return -1;
    }

    int err = inferType(p.first, entryElem, type);
    if (err != 0) {
      ALOGE("%s: cannot parse entry (line: %d)",
                  __func__, entryElem->GetLineNum());
      return -1;
    }
  }

  return 0;
}

int MetadataFactory::inferType(const std::string &tag,
                               const XMLElement *entryElem,
                               const std::string &type) {
  if (type == "byte")
    return inferArray<uint8_t>(tag, entryElem);
  else if (type == "int32")
    return inferArray<int32_t>(tag, entryElem);
  else if (type == "int64")
    return inferArray<int64_t>(tag, entryElem);
  else if (type == "float")
    return inferArray<float>(tag, entryElem);
  else if (type == "rational")
    return inferArray<camera_metadata_rational_t>(tag, entryElem);
  else {
    ALOGE("%s: type %s not recognized (line: %d)",
              __func__, type.c_str(), entryElem->GetLineNum());
    return -1;
  }
}

template<class T>
int MetadataFactory::inferArray(const std::string &tag,
                                const XMLElement *entryElem) {
  const XMLElement *arrayElem = entryElem->FirstChildElement("array");
  if (arrayElem == nullptr)
    return inferParser<T>(tag, entryElem);

  for (const XMLElement *sizeElem = arrayElem->FirstChildElement("size");
       sizeElem != nullptr;
       sizeElem = sizeElem->NextSiblingElement("size")) {
    const char *str = sizeElem->GetText();
    if (str == nullptr) {
      ALOGE("%s: size not valid (line: %d)", __func__, sizeElem->GetLineNum());
      return -1;
    }

    if (strcmp(str, "n") != 0)
      return inferArray<T>(tag, entryElem, str);
  }

  return inferVector<T>(tag, entryElem);
}

template<class T>
int MetadataFactory::inferArray(const std::string &tag,
                                const XMLElement *entryElem,
                                const std::string &size) {
  if (size == "1")
    return inferVector<std::array<T, 1>>(tag, entryElem);
  if (size == "2")
    return inferVector<std::array<T, 2>>(tag, entryElem);
  if (size == "3")
    return inferVector<std::array<T, 3>>(tag, entryElem);
  if (size == "4")
    return inferVector<std::array<T, 4>>(tag, entryElem);
  if (size == "5")
    return inferVector<std::array<T, 5>>(tag, entryElem);
  if (size == "6")
    return inferVector<std::array<T, 6>>(tag, entryElem);
  if (size == "7")
    return inferVector<std::array<T, 7>>(tag, entryElem);
  if (size == "8")
    return inferVector<std::array<T, 8>>(tag, entryElem);
  if (size == "9")
    return inferVector<std::array<T, 9>>(tag, entryElem);

  ALOGE("%s: size '%s' not handled (line: %d)",
          __func__, size.c_str(), entryElem->GetLineNum());
  return -1;
}

template<class T>
int MetadataFactory::inferVector(const std::string &tag,
                                 const XMLElement *entryElem) {
  const XMLElement *arrayElem = entryElem->FirstChildElement("array");
  if (arrayElem == nullptr)
    return inferParser<T>(tag, entryElem);

  for (const XMLElement *sizeElem = arrayElem->FirstChildElement("size");
       sizeElem != nullptr;
       sizeElem = sizeElem->NextSiblingElement("size")) {
    const char *str = sizeElem->GetText();
    if (str == nullptr) {
      ALOGE("%s: size not valid (line: %d)", __func__, sizeElem->GetLineNum());
      return -1;
    }

    if (strcmp(str, "n") == 0)
      return inferParser<std::vector<T>>(tag, entryElem);
  }

  return inferParser<T>(tag, entryElem);
}

template<class T>
int MetadataFactory::inferParser(const std::string &tag,
                                 const XMLElement *entryElem) {
  std::unique_ptr<PartialMetadataInterface> res;

  ALOGV("%s: parse entry '%s'", __func__, tag.c_str());

  int err = 0;
  switch(current_) {
  case Static:
    err = StaticParser<T>::parse(tag, entryElem, visitor_, res);
    break;
  case Dynamic:
    err = DynamicParser<T>::parse(tag, entryElem, visitor_, res);
    break;
  case Control:
    err = ControlParser<T>::parse(tag, entryElem, visitor_, res);
    break;
  }

  if (err != 0) {
    ALOGE("%s cannot parse entry (line: %d)",
            __func__, entryElem->GetLineNum());
    return err;
  }

  ALOGV("%s: '%s' entry parsed", __func__, tag.c_str());

  components_.insert(std::move(res));

  return 0;
}

