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

#include "parser_utils.h"

#include <system/camera_metadata.h>

#include <log/log.h>

int ParserUtils::getTagFromName(const char *name, uint32_t* tag) {
  if (name == nullptr || tag == nullptr) {
    return -1;
  }

  size_t name_length = strlen(name);
  // First, find the section by the longest string match
  const char* section = NULL;
  size_t section_index = 0;
  size_t section_length = 0;
  for (size_t i = 0; i < ANDROID_SECTION_COUNT; ++i) {
    const char* str = camera_metadata_section_names[i];

    ALOGV("%s: Trying to match against section '%s'", __FUNCTION__, str);

    if (strstr(name, str) == name) {  // name begins with the section name
      size_t str_length = strlen(str);

      ALOGV("%s: Name begins with section name", __FUNCTION__);

      // section name is the longest we've found so far
      if (section == NULL || section_length < str_length) {
        section = str;
        section_index = i;
        section_length = str_length;

        ALOGV("%s: Found new best section (%s)", __FUNCTION__, section);
      }
    }
  }

  if (section == NULL) {
    return -1;
  } else {
    ALOGV("%s: Found matched section '%s' (%zu)", __FUNCTION__, section,
          section_index);
  }

  // Get the tag name component of the name
  const char* name_tag_name = name + section_length + 1;  // x.y.z -> z
  if (section_length + 1 >= name_length) {
    return -1;
  }

  // Match rest of name against the tag names in that section only
  uint32_t candidate_tag = 0;
  // Match built-in tags (typically android.*)
  uint32_t tag_begin, tag_end;  // [tag_begin, tag_end)
  tag_begin = camera_metadata_section_bounds[section_index][0];
  tag_end = camera_metadata_section_bounds[section_index][1];

  for (candidate_tag = tag_begin; candidate_tag < tag_end; ++candidate_tag) {
    const char* tag_name = get_camera_metadata_tag_name(candidate_tag);

    if (strcmp(name_tag_name, tag_name) == 0) {
      ALOGV("%s: Found matched tag '%s' (%d)", __FUNCTION__, tag_name,
            candidate_tag);
      break;
    }
  }

  if (candidate_tag == tag_end) {
    return -1;
  }

  *tag = candidate_tag;
  return 0;
}
