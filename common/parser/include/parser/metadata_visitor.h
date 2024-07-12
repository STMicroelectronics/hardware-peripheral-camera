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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_METADATA_VISITOR_H
#define HARDWARE_CAMERA_METADATA_PARSER_METADATA_VISITOR_H

#include <list>
#include <string>
#include <unordered_map>

#include <tinyxml2.h>

using namespace tinyxml2;

class MetadataVisitor : public XMLVisitor {
public:
  using ElementMap = std::unordered_map<std::string, const XMLElement *>;

public:
  MetadataVisitor();

  const ElementMap &statics() const { return statics_; }
  const ElementMap &dynamics() const { return dynamics_; }
  const ElementMap &controls() const { return controls_; }

  const XMLElement *find(const std::string &tag) const;

  bool VisitEnter(const XMLElement &elem, const XMLAttribute *attr);
  bool VisitExit(const XMLElement &elem);

private:


  bool visitNamespace(const XMLElement &elem);
  bool visitEntry(const XMLElement &elem);

private:
  std::list<std::string> namespace_;
  ElementMap statics_;
  ElementMap dynamics_;
  ElementMap controls_;
  ElementMap *current_;
};

#endif // HARDWARE_CAMERA_METADATA_PARSER_METADATA_VISITOR_H
