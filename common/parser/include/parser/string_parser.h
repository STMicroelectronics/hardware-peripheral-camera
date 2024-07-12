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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_STRING_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_STRING_PARSER_H

#include <log/log.h>
#include <system/camera_metadata.h>

#include <cstdlib>
#include <vector>
#include <sstream>

#include <tinyxml2.h>

using namespace tinyxml2;

static std::vector<std::string> split(const std::string &str, char delim) {
  std::vector<std::string> res;
  std::istringstream iss(str);
  std::string s;
  while (std::getline(iss, s, delim)) {
    res.push_back(s);
  }

  return res;
}

template<class T>
class StringParser {
public:
  static int parse(const XMLAttribute *attr,
                   const EnumParser::Enum &enumMap, T &res);
  static int parse(const XMLElement *elem,
                   const EnumParser::Enum &enumMap, T &res);

  static int parseEnum(const std::string &str,
                       const EnumParser::Enum &enumMap, T &res);
  static int parse(const std::string &str, T &res);
};


template<class T, size_t size>
class StringParser<std::array<T, size>> {
public:
  static int parse(const XMLAttribute *attr,
                   const EnumParser::Enum &enumMap, std::array<T, size> &res);
  static int parse(const XMLElement *elem,
                   const EnumParser::Enum &enumMap, std::array<T, size> &res);

private:
  static int parse(const std::vector<std::string> &list,
                   const EnumParser::Enum &enumMap, std::array<T, size> &res);
};

template<class T>
int StringParser<T>::parse(const XMLAttribute *attr,
                           const EnumParser::Enum &enumMap, T &res) {
  const char *text = attr->Value();
  if (text == nullptr)
    return -1;

  return parseEnum(text, enumMap, res);
}

template<class T>
int StringParser<T>::parse(const XMLElement *elem,
                           const EnumParser::Enum &enumMap, T &res) {
  const char *text = elem->GetText();
  if (text == nullptr)
    return -1;

  return parseEnum(text, enumMap, res);
}

template<class T, size_t size>
int StringParser<std::array<T, size>>::parse(const XMLAttribute *attr,
                                             const EnumParser::Enum &enumMap,
                                             std::array<T, size> &res) {
  const char *str = attr->Value();
  return parse(split(str, ' '), enumMap, res);
}

template<class T, size_t size>
int StringParser<std::array<T, size>>::parse(const XMLElement *elem,
                                             const EnumParser::Enum &enumMap,
                                             std::array<T, size> &res) {
  const char *str = elem->GetText();
  if (str == nullptr) {
    ALOGE("%s: no text found in element (line: %d)",
            __func__, elem->GetLineNum());
    return -1;
  }

  return parse(split(str, ' '), enumMap, res);
}

template<class T, size_t size>
int StringParser<std::array<T, size>>::parse(const std::vector<std::string> &list,
                                             const EnumParser::Enum &enumMap,
                                             std::array<T, size> &res) {
  if (list.size() != res.size()) {
    ALOGE("%s: expecting %ld values, got %ld values",
              __func__, res.size(), list.size());
    return -1;
  }

  int i = 0;
  for (const std::string &s : list) {
    T val;
    int err = StringParser<T>::parseEnum(s, enumMap, val);
    if (err != 0)
      return err;

    res[i] = val;
    ++i;
  }

  return 0;
}

template<class T>
int StringParser<T>::parseEnum(const std::string &str,
                               const EnumParser::Enum &enumMap, T &res) {
  auto it = enumMap.find(str);
  if (it != enumMap.cend()) {
    res = it->second;
    return 0;
  }

  return parse(str, res);
}

template<>
inline int StringParser<int32_t>::parse(const std::string &str, int32_t &res) {
  errno = 0;
  res = std::strtol(str.c_str(), nullptr, 10);

  if (errno != 0) {
    ALOGE("%s: the string '%s' cannot be parsed (%d)",
            __func__, str.c_str(), errno);
    return -1;
  }

  return 0;
}

template<>
inline int StringParser<uint8_t>::parse(const std::string &str, uint8_t &res) {
  int32_t val;
  int err = StringParser<int32_t>::parse(str, val);
  if (err != 0)
    return err;

  if (res < 0 || res > 255) {
    ALOGE("%s: the number '%s' cannot fit in a byte", __func__, str.c_str());
    return -1;
  }

  res = val;

  return 0;
}

template<>
inline int StringParser<int64_t>::parse(const std::string &str, int64_t &res) {
  errno = 0;
  res = std::strtoll(str.c_str(), nullptr, 10);

  if (errno != 0) {
    ALOGE("%s: the string '%s' cannot be parsed (%d)",
                __func__, str.c_str(), errno);
    return -1;
  }

  return 0;
}

template<>
inline int StringParser<float>::parse(const std::string &str, float &res) {
  errno = 0;
  res = std::strtof(str.c_str(), nullptr);

  if (errno != 0) {
    ALOGE("%s: the string '%s' cannot be parsed (%d)",
              __func__, str.c_str(), errno);
    return -1;
  }

  return 0;
}

template<>
inline int StringParser<camera_metadata_rational_t>::parse(const std::string &str,
                                                    camera_metadata_rational_t &res) {
  const std::vector<std::string> vec = split(str, '/');
  if (vec.size() != 2) {
    ALOGE("%s: wrong format for rational type (must be n/d)", __func__);
    return -1;
  }

  int32_t numerator;
  int err = StringParser<int32_t>::parse(vec[0], numerator);
  if (err != 0)
    return -1;

  int32_t denominator;
  err = StringParser<int32_t>::parse(vec[1], denominator);
  if (err != 0)
    return -1;

  res = { numerator, denominator };

  return 0;
}

template<>
inline int StringParser<camera_metadata_rational_t>::parseEnum(
    const std::string &str,
    const EnumParser::Enum &enumMap, camera_metadata_rational_t &res) {
  (void)(enumMap);

  return parse(str, res);
}


#endif // HARDWARE_CAMERA_METADATA_PARSER_STRING_PARSER_H
