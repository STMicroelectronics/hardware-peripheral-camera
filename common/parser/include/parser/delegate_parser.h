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

#ifndef HARDWARE_CAMERA_METADATA_PARSER_DELEGATE_PARSER_H
#define HARDWARE_CAMERA_METADATA_PARSER_DELEGATE_PARSER_H

#include <memory>

#include <metadata/boottime_state_delegate.h>
#include <metadata/no_effect_control_delegate.h>

#include "value_parser.h"

template<class T>
class DelegateParser {
public:
  static int parse(const XMLElement *element, const EnumParser::Enum &enumMap,
                   std::unique_ptr<StateDelegateInterface<T>> &res);
  static int parse(const XMLElement *element, const EnumParser::Enum &enumMap,
                   std::unique_ptr<ControlDelegateInterface<T>> &res);

private:
  static int createDelegate(const XMLElement *delegateElem,
                            const std::string &name,
                            const EnumParser::Enum &enumMap,
                            std::unique_ptr<StateDelegateInterface<T>> &res);
  static int createDelegate(const XMLElement *delegateElem,
                            const std::string &name,
                            const EnumParser::Enum &enumMap,
                            std::unique_ptr<ControlDelegateInterface<T>> &res);

  static int createNoEffectDelegate(
      const XMLElement *element,
      const EnumParser::Enum &enumMap,
      std::unique_ptr<ControlDelegateInterface<T>> &res);

  static int createBoottimeDelegate(
      std::unique_ptr<StateDelegateInterface<T>> &res);
};

template<class T>
int DelegateParser<T>::parse(const XMLElement *element,
                             const EnumParser::Enum &enumMap,
                             std::unique_ptr<StateDelegateInterface<T>> &res) {
  const XMLElement *delegateElem = element->FirstChildElement("delegate");
  if (delegateElem == nullptr) {
    ALOGE("%s: delegate element not found (line: %d)",
              __func__, element->GetLineNum());
    return -1;
  }

  const char *name = delegateElem->Attribute("name");
  if (name == nullptr) {
    ALOGE("%s: delegate has no name (line: %d)",
              __func__, delegateElem->GetLineNum());
    return -1;
  }

  return createDelegate(delegateElem, name, enumMap, res);
}

template<class T>
int DelegateParser<T>::parse(const XMLElement *element,
                             const EnumParser::Enum &enumMap,
                             std::unique_ptr<ControlDelegateInterface<T>> &res) {
  const XMLElement *delegateElem = element->FirstChildElement("delegate");
  if (delegateElem == nullptr) {
    ALOGE("%s: delegate element not found (line: %d)",
              __func__, element->GetLineNum());
    return -1;
  }

  const char *name = delegateElem->Attribute("name");
  if (name == nullptr) {
    ALOGE("%s: delegate has no name (line: %d)",
              __func__, delegateElem->GetLineNum());
    return -1;
  }

  return createDelegate(delegateElem, name, enumMap, res);
}

template<class T>
int DelegateParser<T>::createDelegate(
    const XMLElement *delegateElem, const std::string &name,
    const EnumParser::Enum &enumMap,
    std::unique_ptr<StateDelegateInterface<T>> &res) {
  if (name == "fixed") {
    std::unique_ptr<ControlDelegateInterface<T>> aux;
    int err = createNoEffectDelegate(delegateElem, enumMap, aux);
    res = std::move(aux);
    return err;
  }
  if (name == "boottime")
    return createBoottimeDelegate(res);

  ALOGE("%s: delegate '%s' not recognized", __func__, name.c_str());

  return -1;
}

template<class T>
int DelegateParser<T>::createDelegate(
    const XMLElement *delegateElem, const std::string &name,
    const EnumParser::Enum &enumMap,
    std::unique_ptr<ControlDelegateInterface<T>> &res) {
  if (name == "no-effect")
    return createNoEffectDelegate(delegateElem, enumMap, res);

  ALOGE("%s: delegate '%s' not recognized", __func__, name.c_str());

  return -1;
}

template<class T>
int DelegateParser<T>::createNoEffectDelegate(
    const XMLElement *delegateElem,
    const EnumParser::Enum &enumMap,
    std::unique_ptr<ControlDelegateInterface<T>> &res) {
  T val;
  int err = ValueParser<T>::parseValue(delegateElem, enumMap, val);
  if (err != 0) {
    ALOGE("%s: cannot parse delegate value (line: %d)",
              __func__, delegateElem->GetLineNum());
    return err;
  }

  res = std::make_unique<NoEffectControlDelegate<T>>(val);
  return 0;
}

template<class T>
int DelegateParser<T>::createBoottimeDelegate(
    std::unique_ptr<StateDelegateInterface<T>> &res) {
  (void)(res);
  ALOGE("%s: boottime only available with int64_t type", __func__);
  return -1;
}
template<>
int DelegateParser<int64_t>::createBoottimeDelegate(
    std::unique_ptr<StateDelegateInterface<int64_t>> &res) {
  res = std::make_unique<BoottimeStateDelegate>();
  return 0;
}

#endif // HARDWARE_CAMERA_METADATA_PARSER_DELEGATE_PARSER_H
