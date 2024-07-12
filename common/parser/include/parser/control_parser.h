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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_CONTROL_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_CONTROL_PARSER_H

#include "enum_parser.h"
#include "delegate_parser.h"
#include "range_parser.h"
#include "template_parser.h"

template<class T>
class ControlParser {
  using Range = std::variant<std::monostate, std::array<T, 2>, std::vector<T>>;

public:
  static int parse(const std::string &tag, const XMLElement *entryElem,
                   const MetadataVisitor &visitor,
                   std::unique_ptr<PartialMetadataInterface> &res);
};

template<class T>
class ControlParser<std::vector<T>> {
public:
  static int parse(const std::string &tag, const XMLElement *entryElem,
                   const MetadataVisitor &,
                   std::unique_ptr<PartialMetadataInterface> &) {
    ALOGE("%s: entry '%s' is a vector which is not supported for controls (line: %d)",
                __func__, tag.c_str(), entryElem->GetLineNum());

    return -1;
  }
};


template<class T>
class OptionsHelper {
  using Range = std::variant<std::monostate, std::array<T, 2>, std::vector<T>>;

public:
  static int get(const Range &range, const std::map<int, T> &templates,
                 std::unique_ptr<ControlOptionsInterface<T>> &options);

};

template<size_t N>
class OptionsHelper<std::array<camera_metadata_rational_t, N>> {
  using Range = std::variant<std::monostate, std::array<std::array<camera_metadata_rational_t, N>, 2>, std::vector<std::array<camera_metadata_rational_t, N>>>;

public:
  static int get(const Range &,
                 const std::map<int, std::array<camera_metadata_rational_t, N>> &,
                 std::unique_ptr<ControlOptionsInterface<std::array<camera_metadata_rational_t, N>>> &);

};

template<class T>
int ControlParser<T>::parse(const std::string &tag, const XMLElement *entryElem,
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
  std::unique_ptr<ControlDelegateInterface<T>> delegate;
  err = DelegateParser<T>::parse(entryElem, enumMap, delegate);
  if (err != 0) {
    ALOGE("%s: cannot parse delegate (line: %d)",
              __func__, entryElem->GetLineNum());
    return err;
  }
  std::unique_ptr<TaggedControlDelegate<T>> tDelegate =
      std::make_unique<TaggedControlDelegate<T>>(t, std::move(delegate));

  /* parse ranges */
  Range range;
  err = RangeParser<T>::parse(entryElem, visitor, enumMap, range);
  if (err != 0) {
    ALOGE("%s: cannot parse range (line: %d)",
              __func__, entryElem->GetLineNum());
    return err;
  }

  /* parse templates */
  std::map<int, T> templates;
  err = TemplateParser<T>::parse(entryElem, enumMap, templates);
  if (err != 0) {
    ALOGE("%s: cannot parse template (line: %d)",
              __func__, entryElem->GetLineNum());
    return err;
  }

  std::unique_ptr<ControlOptionsInterface<T>> options;
  err = OptionsHelper<T>::get(range, templates, options);
  if (err != 0) {
    ALOGE("%s: cannot create options from range (line: %d)",
              __func__, entryElem->GetLineNum());
    return err;
  }

  std::unique_ptr<TaggedControlOptions<T>> tOptions;
  if (options != nullptr) {
    tOptions = std::make_unique<TaggedControlOptions<T>>(
                                    DO_NOT_REPORT_OPTIONS, std::move(options));
  }
  res = std::unique_ptr<PartialMetadataInterface>(
      new Control<T>(std::move(tDelegate), std::move(tOptions)));

  return 0;
}

template<class T>
int OptionsHelper<T>::get(
    const Range &range, const std::map<int, T> &templates,
    std::unique_ptr<ControlOptionsInterface<T>> &options) {

  if (std::holds_alternative<std::array<T, 2>>(range)) {
    const std::array<T, 2> &slider = std::get<std::array<T, 2>>(range);
    options = std::make_unique<SliderControlOptions<T>>(slider[0], slider[1],
                                                        templates);
  } else if (std::holds_alternative<std::vector<T>>(range)) {
    const std::vector<T> &values = std::get<std::vector<T>>(range);
    options = std::make_unique<MenuControlOptions<T>>(values, templates);
  }

  return 0;
}

template<size_t N>
int OptionsHelper<std::array<camera_metadata_rational_t, N>>::get(
    const Range &,
    const std::map<int, std::array<camera_metadata_rational_t, N>> &,
    std::unique_ptr<ControlOptionsInterface<std::array<camera_metadata_rational_t, N>>> &) {

  ALOGE("%s: range is not supported with rational", __func__);

  return -1;
}
template<>
int OptionsHelper<camera_metadata_rational_t>::get(
    const Range &,
    const std::map<int, camera_metadata_rational_t> &,
    std::unique_ptr<ControlOptionsInterface<camera_metadata_rational_t>> &) {

  ALOGE("%s: range is not supported with rational", __func__);

  return -1;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_CONTROL_PARSER_H
