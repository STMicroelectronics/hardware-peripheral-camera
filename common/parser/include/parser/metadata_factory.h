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

#ifndef HARDWARE_CAMERA_METADATA_METADATA_FACTORY_H
#define HARDWARE_CAMERA_METADATA_METADATA_FACTORY_H

#include <string>

#include <tinyxml2.h>

#include <metadata/metadata.h>
#include <metadata/partial_metadata_factory.h>

#include "metadata_visitor.h"

using namespace ::android::hardware::camera::common::V1_0::metadata;

class MetadataFactory {
private:
  enum Category {
    Static,
    Dynamic,
    Control
  };

public:
  int load(const char *cfgPath);

  int parse(std::unique_ptr<Metadata> &metadata);

private:
  int parse(const MetadataVisitor::ElementMap &map);
  int inferType(const std::string &tag,
                const XMLElement *entryElem, const std::string &type);

  template<class T>
  int inferArray(const std::string &tag, const XMLElement *entryElem);

  template<class T>
  int inferArray(const std::string &tag,
                 const XMLElement *entryElem, const std::string &size);

  template<class T>
  int inferVector(const std::string &tag, const XMLElement *entryElem);

  template<class T>
  int inferParser(const std::string &tag, const XMLElement *entryElem);

private:
  XMLDocument configXml_;
  MetadataVisitor visitor_;
  PartialMetadataSet components_;
  Category current_;
};

#endif // HARDWARE_CAMERA_METADATA_METADATA_FACTORY_H
