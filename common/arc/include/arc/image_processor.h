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

#ifndef HAL_USB_IMAGE_PROCESSOR_H_
#define HAL_USB_IMAGE_PROCESSOR_H_

#include <set>
#include <string>
#include <vector>

// FourCC pixel formats (defined as V4L2_PIX_FMT_*).
#include <linux/videodev2.h>
// Declarations of HAL_PIXEL_FORMAT_XXX.
#include <system/graphics.h>

#include "CameraMetadata.h"
#include "exif_utils.h"
#include "frame_buffer.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

using android::hardware::camera::common::V1_0::helper::CameraMetadata;

// V4L2_PIX_FMT_YVU420(YV12) in ImageProcessor has alignment requirement.
// The stride of Y, U, and V planes should a multiple of 16 pixels.
struct ImageProcessor {
  // Calculate the output buffer size when converting to the specified pixel
  // format. |fourcc| is defined as V4L2_PIX_FMT_* in linux/videodev2.h.
  // Return 0 on error.
  static size_t GetConvertedSize(int fourcc, uint32_t width, uint32_t height);

  /*
   * We don't use std::set for qualified format because the returned vector is
   * sorted by prefered qualified format.
   */
  static int GetQualifiedFormats(const std::set<uint32_t>& supported_formats,
                                 std::vector<uint32_t> *qualified_formats);

  // Return whether this class supports the provided conversion.
  static bool SupportsConversion(uint32_t from_fourcc, uint32_t to_fourcc);

  // Convert format from |in_frame.fourcc| to |out_frame->fourcc|. Caller should
  // fill |data|, |buffer_size|, |width|, and |height| of |out_frame|. The
  // function will fill |out_frame->data_size|. Return non-zero error code on
  // failure; return 0 on success.
  static int ConvertFormat(const CameraMetadata& metadata,
                           const FrameBuffer& in_frame, FrameBuffer* out_frame);

  // Scale image size according to |in_frame| and |out_frame|. Only support
  // V4L2_PIX_FMT_YUV420 format. Caller should fill |data|, |width|, |height|,
  // and |buffer_size| of |out_frame|. The function will fill |data_size| and
  // |fourcc| of |out_frame|.
  static int Scale(const FrameBuffer& in_frame, FrameBuffer* out_frame);

private:

  // YV12 horizontal stride should be a multiple of 16 pixels for each plane.
  // |dst_stride_uv| is the pixel stride of u or v plane.
  static int YU12ToYV12(const void* yv12, void* yu12, int width, int height,
                                          int dst_stride_y, int dst_stride_uv);

  static int YU12ToNV21(const void* yv12, void* nv21, int width, int height);

  static bool ConvertToJpeg(const CameraMetadata& metadata,
                          const FrameBuffer& in_frame, FrameBuffer* out_frame);

  static bool SetExifTags(const CameraMetadata& metadata, ExifUtils* utils);

  inline static size_t Align16(size_t value) { return (value + 15) & ~15; }

private:
  /* Format from which the conversion is available*/
  static const std::vector<uint32_t> kSupportedFourCCs;

  // How precise the float-to-rational conversion for EXIF tags would be.
  static const int kRationalPrecision;

  // Default JPEG quality settings.
  static const int DEFAULT_JPEG_QUALITY;

};

} // namespace ar
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // HAL_USB_IMAGE_PROCESSOR_H_
