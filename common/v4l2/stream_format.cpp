/*
 * Copyright 2019 The Android Open Source Project
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

#define LOG_TAG "android.hardware.camera.common@1.0-v4l2.stm32mp1"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "stream_format.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace v4l2 {

StreamFormat::StreamFormat(PixelFormat format, uint32_t width, uint32_t height,
                           uint32_t implementation_defined)
    // TODO(b/30000211): multiplanar support.
    : type_(V4L2_BUF_TYPE_VIDEO_CAPTURE),
      v4l2_pixel_format_(HalToV4L2PixelFormat(format, implementation_defined)),
      width_(width),
      height_(height) {}

StreamFormat::StreamFormat(uint32_t format, uint32_t width, uint32_t height)
    // TODO(b/30000211): multiplanar support.
    : type_(V4L2_BUF_TYPE_VIDEO_CAPTURE),
      v4l2_pixel_format_(format),
      width_(width),
      height_(height) {}

StreamFormat::StreamFormat(const v4l2_format& format)
    : type_(format.type),
      // TODO(b/30000211): multiplanar support.
      v4l2_pixel_format_(format.fmt.pix.pixelformat),
      width_(format.fmt.pix.width),
      height_(format.fmt.pix.height) {}

void StreamFormat::FillFormatRequest(v4l2_format* format) const {
  memset(format, 0, sizeof(*format));
  format->type = type_;
  format->fmt.pix.pixelformat = v4l2_pixel_format_;
  format->fmt.pix.width = width_;
  format->fmt.pix.height = height_;
}

bool StreamFormat::operator==(const StreamFormat& other) const {
  // Used to check that a requested format was actually set, so
  // don't compare bytes per line or min buffer size.
  return (type_ == other.type_ &&
          v4l2_pixel_format_ == other.v4l2_pixel_format_ &&
          width_ == other.width_ && height_ == other.height_);
}

bool StreamFormat::operator!=(const StreamFormat& other) const {
  return !(*this == other);
}

int StreamFormat::V4L2ToHalPixelFormat(uint32_t v4l2_pixel_format) {
  PixelFormat res;

  // Translate V4L2 format to HAL format.
  switch (v4l2_pixel_format) {
    case V4L2_PIX_FMT_BGR32:
      res = PixelFormat::RGBA_8888;
      break;
    case V4L2_PIX_FMT_JPEG:
      res = PixelFormat::BLOB;
      break;
    case V4L2_PIX_FMT_NV21:
      res = PixelFormat::YCRCB_420_SP;
      break;
    case V4L2_PIX_FMT_YUV420:
      res = PixelFormat::YCBCR_420_888;
      break;
    case V4L2_PIX_FMT_YUYV:
      res = PixelFormat::YCBCR_422_I;
      break;
    case V4L2_PIX_FMT_YVU420:
      res = PixelFormat::YV12;
      break;
    default:
      // Unrecognized format.
      ALOGV("Unrecognized v4l2 pixel format 0x%x", v4l2_pixel_format);
      return -1;
  }

  return static_cast<int>(res);
}

uint32_t StreamFormat::HalToV4L2PixelFormat(PixelFormat hal_pixel_format,
                                            uint32_t implementation_defined) {
  switch (hal_pixel_format) {
    case PixelFormat::BLOB:
      return V4L2_PIX_FMT_JPEG;
    case PixelFormat::IMPLEMENTATION_DEFINED:
      return implementation_defined;
    case PixelFormat::RGBA_8888:
      return V4L2_PIX_FMT_BGR32;
    case PixelFormat::YCBCR_420_888:
      // This is a flexible YUV format that depends on platform. Different
      // platform may have different format. It can be YVU420 or NV12. Now we
      // return YVU420 first.
      // TODO(): call drm_drv.get_fourcc() to get correct format.
      return V4L2_PIX_FMT_YUV420;
    case PixelFormat::YCBCR_422_I:
      return V4L2_PIX_FMT_YUYV;
    case PixelFormat::YCRCB_420_SP:
      return V4L2_PIX_FMT_NV21;
    case PixelFormat::YV12:
      return V4L2_PIX_FMT_YVU420;
    default:
      ALOGV("Pixel format 0x%x is unsupported.", hal_pixel_format);
      break;
  }
  return -1;
}

int StreamFormat::V4L2ToHalPixelFormat(
                                    const std::set<uint32_t> v4l2_pixel_formats,
                                    std::set<PixelFormat> *hal_pixel_format) {
  for (uint32_t v4l2_pixel_format : v4l2_pixel_formats) {
    int32_t hal_format = V4L2ToHalPixelFormat(v4l2_pixel_format);

    if (hal_format < 0) {
      ALOGW("%s: 0x%x format not recognized, ignoring it",
                                              __FUNCTION__, v4l2_pixel_format);
      continue;
    }

    hal_pixel_format->insert(static_cast<PixelFormat>(hal_format));
  }

  return 0;
}

/* Helper functiont to find a matching StreamFormat in the given
 * formats array.
 *
 * @param formats: the StreamFormat array where to search for the corresponding
 *                 format
 * @param pixel_format: the pixel format to match
 * @param width: the format width to match
 * @param height: the format height to match
 *
 * @return the index of the matching StreamFormat in the given formats array or
 *         -1 if not found
 *
 */
int StreamFormat::FindMatchingFormat(const StreamFormats& formats,
                                     uint32_t v4l2_pixel_format, uint32_t width,
                                     uint32_t height) {
  for (size_t i = 0; i < formats.size(); ++i) {
    const StreamFormat& format = formats[i];

    if (format.v4l2_pixel_format() == v4l2_pixel_format &&
            format.width() == width && format.height() == height) {
      return i;
    }
  }

  return -1;
}

/* Helper function to find a format based on its resolution in the given formats
 * array
 *
 * @param formats: the StreamFormat array where to search for the corresponding
 *                 format resolution
 * @param width: the width to match
 * @param height: the height to match
 *
 * @return the index of the matched resolution in the given formats array or -1
 *         if not found
 *
 */
int StreamFormat::FindFormatByResolution(const StreamFormats& formats,
                                         uint32_t width, uint32_t height) {
  for (size_t i = 0; i < formats.size(); ++i) {
    const StreamFormat& format = formats[i];

    if (format.width() == width && format.height() == height) {
      ALOGV("%s: find format %x", __FUNCTION__, format.v4l2_pixel_format());
      return i;
    }
  }

  ALOGV("%s: can't find any format for the given resolution (%dx%d)",
                                                  __FUNCTION__, width, height);

  return -1;
}

StreamFormats StreamFormat::GetQualifiedStreams(
                                const std::vector<uint32_t>& qualified_formats,
                                const StreamFormats& supported_streams) {
  StreamFormats qualified_streams;

  for (uint32_t qualified_format : qualified_formats) {
    for (const StreamFormat& supported_stream : supported_streams) {
      if (supported_stream.v4l2_pixel_format() != qualified_format) {
        continue;
      }

      if (FindFormatByResolution(qualified_streams, supported_stream.width(),
                                              supported_stream.height()) > 0) {
        continue;
      }

      ALOGV("%s: add qualified stream: format %x, width %d, height %d",
                          __FUNCTION__, supported_stream.v4l2_pixel_format(),
                          supported_stream.width(), supported_stream.height());
      qualified_streams.push_back(supported_stream);
    }
  }

  return qualified_streams;
}

} // v4l2
} // V1_0
} // common
} // camera
} // hardware
} // android
