/*
 * Copyright (C) 2019 The Android Open Source Project
 * Copyright (C) 2019 STMicroelectronics
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

#ifndef V4L2_CAMERA_HAL_STREAM_FORMAT_H_
#define V4L2_CAMERA_HAL_STREAM_FORMAT_H_

#include <linux/videodev2.h>
#include <set>
#include <unistd.h>
#include <vector>

#include <aidl/android/hardware/graphics/common/PixelFormat.h>

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace v4l2 {

using ::aidl::android::hardware::graphics::common::PixelFormat;

class StreamFormat;

typedef std::vector<StreamFormat> StreamFormats;

class StreamFormat {
 public:
  StreamFormat(PixelFormat format, uint32_t width, uint32_t height,
               uint32_t implementation_defined);
  StreamFormat(uint32_t format, uint32_t width, uint32_t height);
  StreamFormat(const v4l2_format& format);
  virtual ~StreamFormat() = default;

  void FillFormatRequest(v4l2_format* format) const;

  // Accessors.
  inline uint32_t type() const { return type_; };
  inline uint32_t width() const { return width_; };
  inline uint32_t height() const { return height_; };
  inline int pixel_format() const { return V4L2ToHalPixelFormat(v4l2_pixel_format_); }
  inline uint32_t v4l2_pixel_format() const { return v4l2_pixel_format_; }

  bool operator==(const StreamFormat& other) const;
  bool operator!=(const StreamFormat& other) const;

  // HAL <-> V4L2 conversions
  // Returns 0 for unrecognized.
  static uint32_t HalToV4L2PixelFormat(PixelFormat hal_pixel_format,
                                       uint32_t implementation_defined = 0);
  // Returns -1 for unrecognized.
  static int V4L2ToHalPixelFormat(uint32_t v4l2_pixel_format);

  static int V4L2ToHalPixelFormat(const std::set<uint32_t> v4l2_pixel_formats,
                                  std::set<PixelFormat> *hal_pixel_format);

  static int FindMatchingFormat(const StreamFormats& formats,
                                uint32_t pixel_format, uint32_t width,
                                uint32_t height);

  static int FindFormatByResolution(const StreamFormats& formats,
                                     uint32_t width, uint32_t height);

  static StreamFormats GetQualifiedStreams(
                                const std::vector<uint32_t>& qualified_formats,
                                const StreamFormats& supported_stream);

 private:
  uint32_t type_;
  uint32_t v4l2_pixel_format_;
  uint32_t width_;
  uint32_t height_;

  static const std::vector<uint32_t> kSupportedFourCCs;
};

} // v4l2
} // V1_0
} // common
} // camera
} // hardware
} // android

#endif  // V4L2_CAMERA_HAL_STREAM_FORMAT_H_
