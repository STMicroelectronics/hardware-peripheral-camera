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

#include "metadata_visitor.h"

#include <sstream>

#include <log/log.h>

std::string join(const std::list<std::string> &list, const char *delim) {
  std::ostringstream oss;
  std::copy(list.cbegin(), list.cend(),
            std::ostream_iterator<std::string>(oss, delim));
  return oss.str();
}

MetadataVisitor::MetadataVisitor()
  : current_(nullptr)
{ }

const XMLElement *MetadataVisitor::find(const std::string &tag) const {
  auto it = statics_.find(tag);
  if (it != statics_.cend())
    return it->second;

  it = dynamics_.find(tag);
  if (it != dynamics_.cend())
    return it->second;

  it = controls_.find(tag);
  if (it != controls_.cend())
    return it->second;

  return nullptr;
}

bool MetadataVisitor::VisitEnter(const XMLElement &elem,
                                 const XMLAttribute *attr) {
  (void)(attr);
  std::string node(elem.Name());

  if (node == "entry")
    return visitEntry(elem);

  if (node == "namespace" || node == "section")
    return visitNamespace(elem);

  if (node == "static")
    current_ = &statics_;
  else if (node == "dynamic")
    current_ = &dynamics_;
  else if (node == "controls")
    current_ = &controls_;

  return true;
}

bool MetadataVisitor::VisitExit(const XMLElement &elem) {
  std::string node(elem.Name());

  if (node == "namespace" || node == "section")
    namespace_.pop_back();
  else if (node == "static" || node == "dynamic" || node == "controls")
    current_ = nullptr;

  return true;
}

bool MetadataVisitor::visitNamespace(const XMLElement &elem) {
  const char *name = elem.Attribute("name");
  if (name == nullptr) {
    ALOGE("%s: namespace / section doesn't have name, skipping (line: %d)",
              __func__, elem.GetLineNum());

    return false;
  }

  namespace_.push_back(name);
  return true;
}

bool MetadataVisitor::visitEntry(const XMLElement &elem) {
  if (current_ == nullptr) {
    ALOGE("%s: entry not inside controls, dynamic or static node, skipping (line: %d)",
              __func__, elem.GetLineNum());
    return false;
  }

  const char *name = elem.Attribute("name");
  if (name == nullptr) {
    ALOGE("%s: entry doesn't have name, skipping (line: %d)",
              __func__, elem.GetLineNum());

    return false;
  }

  std::string ns = join(namespace_, ".") + name;
  (*current_)[ns] = &elem;

  ALOGV("%s: entry %s added !", __func__, ns.c_str());

  /* don't need to visit further */
  return false;
}

