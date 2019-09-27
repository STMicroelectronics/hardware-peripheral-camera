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

#define LOG_TAG "android.hardware.camera.common@1.0-arc.stm32mp1"
// #define LOG_NDEBUG 0

#include "image_processor.h"

#include <errno.h>
#include <libyuv.h>
#include <time.h>
#include <utils/Log.h>

#include "common_types.h"
#include "exif_utils.h"
#include "jpeg_compressor.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

/*
 * Formats have different names in different header files. Here is the mapping
 * table:
 *
 * android_pixel_format_t          videodev2.h           FOURCC in libyuv
 * -----------------------------------------------------------------------------
 * HAL_PIXEL_FORMAT_YV12         = V4L2_PIX_FMT_YVU420 = FOURCC_YV12
 * HAL_PIXEL_FORMAT_YCrCb_420_SP = V4L2_PIX_FMT_NV21   = FOURCC_NV21
 * HAL_PIXEL_FORMAT_RGBA_8888    = V4L2_PIX_FMT_RGB32  = FOURCC_BGR4
 * HAL_PIXEL_FORMAT_YCbCr_422_I  = V4L2_PIX_FMT_YUYV   = FOURCC_YUYV
 *                                                     = FOURCC_YUY2
 *                                 V4L2_PIX_FMT_YUV420 = FOURCC_I420
 *                                                     = FOURCC_YU12
 *                                 V4L2_PIX_FMT_MJPEG  = FOURCC_MJPG
 *
 * Camera device generates FOURCC_YUYV and FOURCC_MJPG.
 * Preview needs FOURCC_ARGB format.
 * Software video encoder needs FOURCC_YU12.
 * CTS requires FOURCC_YV12 and FOURCC_NV21 for applications.
 *
 * Android stride requirement:
 * YV12 horizontal stride should be a multiple of 16 pixels. See
 * android.graphics.ImageFormat.YV12.
 * The stride of ARGB, YU12, and NV21 are always equal to the width.
 *
 * Conversion Path:
 * MJPG/YUYV (from camera) -> YU12 -> ARGB (preview)
 *                                 -> NV21 (apps)
 *                                 -> YV12 (apps)
 *                                 -> YU12 (video encoder)
 */

const std::vector<uint32_t> ImageProcessor::kSupportedFourCCs = {
    V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG
};

const int ImageProcessor::kRationalPrecision = 10000;

const int ImageProcessor::DEFAULT_JPEG_QUALITY = 80;

size_t ImageProcessor::GetConvertedSize(int fourcc, uint32_t width,
                                        uint32_t height) {
  if ((width % 2) || (height % 2)) {
    ALOGE("%s: Width or height is not even (%dx%d)",
                                                  __FUNCTION__, width, height);
    return 0;
  }

  switch (fourcc) {
    case V4L2_PIX_FMT_YVU420:  // YV12
      return Align16(width) * height + Align16(width / 2) * height;
    case V4L2_PIX_FMT_YUV420:  // YU12
    // Fall-through.
    case V4L2_PIX_FMT_NV21:  // NV21
      return width * height * 3 / 2;
    case V4L2_PIX_FMT_BGR32:
    case V4L2_PIX_FMT_RGB32:
      return width * height * 4;
    default:
      ALOGI("%s: Pixel format %s is unsupported",
                                  __FUNCTION__, FormatToString(fourcc).c_str());
      return 0;
  }
}

int ImageProcessor::GetQualifiedFormats(
                                  const std::set<uint32_t>& supported_formats,
                                  std::vector<uint32_t> *qualified_formats) {
  for (uint32_t supported_fourcc : kSupportedFourCCs) {
    for (uint32_t supported_format : supported_formats) {
      if (supported_format != supported_fourcc) {
        continue;
      }

      if (std::find(qualified_formats->begin(), qualified_formats->end(),
                                supported_format) != qualified_formats->end()) {
        continue;
      }

      ALOGV("%s: found qualified format: 0x%x", __FUNCTION__, supported_format);

      qualified_formats->push_back(supported_format);
    }
  }

  return 0;
}

bool ImageProcessor::SupportsConversion(uint32_t from_fourcc,
                                        uint32_t to_fourcc) {
  switch (from_fourcc) {
    case V4L2_PIX_FMT_YUYV:
      return (to_fourcc == V4L2_PIX_FMT_YUV420);
    case V4L2_PIX_FMT_YUV420:
      return (
          to_fourcc == V4L2_PIX_FMT_YUV420 ||
          to_fourcc == V4L2_PIX_FMT_YVU420 || to_fourcc == V4L2_PIX_FMT_NV21 ||
          to_fourcc == V4L2_PIX_FMT_RGB32 || to_fourcc == V4L2_PIX_FMT_BGR32 ||
          to_fourcc == V4L2_PIX_FMT_JPEG);
    case V4L2_PIX_FMT_MJPEG:
      return (to_fourcc == V4L2_PIX_FMT_YUV420);
    default:
      return false;
  }
}

int ImageProcessor::ConvertFormat(const CameraMetadata& metadata,
                                  const FrameBuffer& in_frame,
                                  FrameBuffer* out_frame) {
  ALOGV("%s: enter", __FUNCTION__);

  if ((in_frame.GetWidth() % 2) || (in_frame.GetHeight() % 2)) {
    ALOGE("%s: Width or height is not even (%dx%d)",
              __FUNCTION__, in_frame.GetWidth(), in_frame.GetHeight());
    return -EINVAL;
  }

  size_t data_size = GetConvertedSize(
      out_frame->GetFourcc(), in_frame.GetWidth(), in_frame.GetHeight());

  if (out_frame->SetDataSize(data_size)) {
    ALOGE("%s: Set data size failed", __FUNCTION__);
    return -EINVAL;
  }

  ALOGV("%s: convert 0x%x to 0x%x: in: %p, w: %d, h: %d, out: %p",
            __FUNCTION__, in_frame.GetFourcc(), out_frame->GetFourcc(),
            in_frame.GetData(), in_frame.GetWidth(),
            in_frame.GetHeight(), out_frame->GetData());


  if (in_frame.GetFourcc() == V4L2_PIX_FMT_YUYV) {
    switch (out_frame->GetFourcc()) {
      case V4L2_PIX_FMT_YUV420:  // YU12
      {
        int res = libyuv::YUY2ToI420(
            in_frame.GetData(),      /* src_yuy2 */
            in_frame.GetWidth() * 2, /* src_stride_yuy2 */
            out_frame->GetData(),    /* dst_y */
            out_frame->GetWidth(),   /* dst_stride_y */
            out_frame->GetData() +
                out_frame->GetWidth() * out_frame->GetHeight(), /* dst_u */
            out_frame->GetWidth() / 2, /* dst_stride_u */
            out_frame->GetData() + out_frame->GetWidth() *
                                       out_frame->GetHeight() * 5 /
                                       4, /* dst_v */
            out_frame->GetWidth() / 2,    /* dst_stride_v */
            in_frame.GetWidth(), in_frame.GetHeight());
        ALOGE_IF(res, "%s: YUY2ToI420() for YU12 returns %d", __FUNCTION__, res);
        return res ? -EINVAL : 0;
      }
      default:
        ALOGE("%s: Destination pixel format %s is unsupported "
                  "for YUYV source format.",
                  __FUNCTION__, FormatToString(out_frame->GetFourcc()).c_str());
        return -EINVAL;
    }
  } else if (in_frame.GetFourcc() == V4L2_PIX_FMT_YUV420) {
    // V4L2_PIX_FMT_YVU420 is YV12. I420 is usually referred to YU12
    // (V4L2_PIX_FMT_YUV420), and YV12 is similar to YU12 except that U/V
    // planes are swapped.
    switch (out_frame->GetFourcc()) {
      case V4L2_PIX_FMT_YVU420:  // YV12
      {
        int ystride = Align16(in_frame.GetWidth());
        int uvstride = Align16(in_frame.GetWidth() / 2);
        int res = YU12ToYV12(in_frame.GetData(), out_frame->GetData(),
                             in_frame.GetWidth(), in_frame.GetHeight(), ystride,
                             uvstride);
        ALOGE_IF(res, "%s: YU12ToYV12() returns %d", __FUNCTION__, res);
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_YUV420:  // YU12
      {
        memcpy(out_frame->GetData(), in_frame.GetData(),
               in_frame.GetDataSize());
        return 0;
      }
      case V4L2_PIX_FMT_NV21:  // NV21
      {
        // TODO(henryhsu): Use libyuv::I420ToNV21.
        int res = YU12ToNV21(in_frame.GetData(), out_frame->GetData(),
                             in_frame.GetWidth(), in_frame.GetHeight());
        ALOGE_IF(res, "%s: YU12ToNV21() returns %d", __FUNCTION__, res);
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_BGR32: {
        int res = libyuv::I420ToABGR(
            in_frame.GetData(),  /* src_y */
            in_frame.GetWidth(), /* src_stride_y */
            in_frame.GetData() +
                in_frame.GetWidth() * in_frame.GetHeight(), /* src_u */
            in_frame.GetWidth() / 2,                        /* src_stride_u */
            in_frame.GetData() +
                in_frame.GetWidth() * in_frame.GetHeight() * 5 / 4, /* src_v */
            in_frame.GetWidth() / 2,   /* src_stride_v */
            out_frame->GetData(),      /* dst_abgr */
            out_frame->GetWidth() * 4, /* dst_stride_abgr */
            in_frame.GetWidth(), in_frame.GetHeight());
        ALOGE_IF(res, "%s: I420ToABGR() returns %d", __FUNCTION__, res);
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_RGB32: {
        int res = libyuv::I420ToARGB(
            in_frame.GetData(),  /* src_y */
            in_frame.GetWidth(), /* src_stride_y */
            in_frame.GetData() +
                in_frame.GetWidth() * in_frame.GetHeight(), /* src_u */
            in_frame.GetWidth() / 2,                        /* src_stride_u */
            in_frame.GetData() +
                in_frame.GetWidth() * in_frame.GetHeight() * 5 / 4, /* src_v */
            in_frame.GetWidth() / 2,   /* src_stride_v */
            out_frame->GetData(),      /* dst_argb */
            out_frame->GetWidth() * 4, /* dst_stride_argb */
            in_frame.GetWidth(), in_frame.GetHeight());
        ALOGE_IF(res, "%s: I420ToARGB() returns %d", __FUNCTION__, res);
        return res ? -EINVAL : 0;
      }
      case V4L2_PIX_FMT_JPEG: {
        bool res = ConvertToJpeg(metadata, in_frame, out_frame);
        ALOGE_IF(!res, "%s: ConvertToJpeg() returns %d", __FUNCTION__, res);
        return !res ? -EINVAL : 0;
      }
      default:
        ALOGE("%s: Destination pixel format %s"
                  " is unsupported for YU12 source format.",
                  __FUNCTION__, FormatToString(out_frame->GetFourcc()).c_str());
        return -EINVAL;
    }
  } else if (in_frame.GetFourcc() == V4L2_PIX_FMT_MJPEG) {
    switch (out_frame->GetFourcc()) {
      case V4L2_PIX_FMT_YUV420:  // YU12
      {
        int res = libyuv::MJPGToI420(
            in_frame.GetData(),     /* sample */
            in_frame.GetDataSize(), /* sample_size */
            out_frame->GetData(),   /* dst_y */
            out_frame->GetWidth(),  /* dst_stride_y */
            out_frame->GetData() +
                out_frame->GetWidth() * out_frame->GetHeight(), /* dst_u */
            out_frame->GetWidth() / 2, /* dst_stride_u */
            out_frame->GetData() + out_frame->GetWidth() *
                                       out_frame->GetHeight() * 5 /
                                       4, /* dst_v */
            out_frame->GetWidth() / 2,    /* dst_stride_v */
            in_frame.GetWidth(), in_frame.GetHeight(), out_frame->GetWidth(),
            out_frame->GetHeight());
        ALOGE_IF(res, "%s: MJPEGToI420() returns %d", __FUNCTION__, res);
        return res ? -EINVAL : 0;
      }
      default:
        ALOGE("%s: Destination pixel format %s"
                  " is unsupported for MJPEG source format.",
                  __FUNCTION__, FormatToString(out_frame->GetFourcc()).c_str());
        return -EINVAL;
    }
  } else {
    ALOGE("%s: Convert format doesn't support source format %s",
              __FUNCTION__, FormatToString(in_frame.GetFourcc()).c_str());
    return -EINVAL;
  }
}

int ImageProcessor::Scale(const FrameBuffer& in_frame, FrameBuffer* out_frame) {
  if (in_frame.GetFourcc() != V4L2_PIX_FMT_YUV420) {
    ALOGE("%s: Pixel format %s is unsupported",
              __FUNCTION__,  FormatToString(in_frame.GetFourcc()).c_str());
    return -EINVAL;
  }

  size_t data_size = GetConvertedSize(
      in_frame.GetFourcc(), out_frame->GetWidth(), out_frame->GetHeight());

  if (out_frame->SetDataSize(data_size)) {
    ALOGE("%s: Set data size failed", __FUNCTION__);
    return -EINVAL;
  }
  out_frame->SetFourcc(in_frame.GetFourcc());

  ALOGD("%s: Scale image from %dx%d to %dx%d", __FUNCTION__,
            in_frame.GetWidth(), in_frame.GetHeight(),
            out_frame->GetWidth(), out_frame->GetHeight());

  int ret = libyuv::I420Scale(
      in_frame.GetData(), in_frame.GetWidth(),
      in_frame.GetData() + in_frame.GetWidth() * in_frame.GetHeight(),
      in_frame.GetWidth() / 2,
      in_frame.GetData() + in_frame.GetWidth() * in_frame.GetHeight() * 5 / 4,
      in_frame.GetWidth() / 2, in_frame.GetWidth(), in_frame.GetHeight(),
      out_frame->GetData(), out_frame->GetWidth(),
      out_frame->GetData() + out_frame->GetWidth() * out_frame->GetHeight(),
      out_frame->GetWidth() / 2,
      out_frame->GetData() +
          out_frame->GetWidth() * out_frame->GetHeight() * 5 / 4,
      out_frame->GetWidth() / 2, out_frame->GetWidth(), out_frame->GetHeight(),
      libyuv::FilterMode::kFilterNone);
  ALOGE_IF(ret, "%s: I420Scale failed: %d", __FUNCTION__, ret);
  return ret;
}

int ImageProcessor::YU12ToYV12(const void* yu12, void* yv12, int width,
                              int height, int dst_stride_y, int dst_stride_uv) {
  if ((width % 2) || (height % 2)) {
    ALOGE("%s: Width or height is not even (%dx%d)",
              __FUNCTION__, width, height);
    return -EINVAL;
  }

  if (dst_stride_y < width || dst_stride_uv < width / 2) {
    ALOGE("%s: Y plane stride (%d) or U/V plane stride (%d)"
              " is invalid for width %d",
              __FUNCTION__, dst_stride_y, dst_stride_uv, width);
    return -EINVAL;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(yu12);
  uint8_t* dst = reinterpret_cast<uint8_t*>(yv12);
  const uint8_t* u_src = src + width * height;
  uint8_t* u_dst = dst + dst_stride_y * height + dst_stride_uv * height / 2;
  const uint8_t* v_src = src + width * height * 5 / 4;
  uint8_t* v_dst = dst + dst_stride_y * height;

  return libyuv::I420Copy(src, width, u_src, width / 2, v_src, width / 2, dst,
                          dst_stride_y, u_dst, dst_stride_uv, v_dst,
                          dst_stride_uv, width, height);
}

int ImageProcessor::YU12ToNV21(const void* yu12, void* nv21,
                                                        int width, int height) {
  if ((width % 2) || (height % 2)) {
    ALOGE("%s: Width or height is not even (%dx%d)",
              __FUNCTION__, width, height);
    return -EINVAL;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(yu12);
  uint8_t* dst = reinterpret_cast<uint8_t*>(nv21);
  const uint8_t* u_src = src + width * height;
  const uint8_t* v_src = src + width * height * 5 / 4;
  uint8_t* vu_dst = dst + width * height;

  memcpy(dst, src, width * height);

  for (int i = 0; i < height / 2; i++) {
    for (int j = 0; j < width / 2; j++) {
      *vu_dst++ = *v_src++;
      *vu_dst++ = *u_src++;
    }
  }

  return 0;
}

bool ImageProcessor::ConvertToJpeg(const CameraMetadata& metadata,
                          const FrameBuffer& in_frame, FrameBuffer* out_frame) {
  ExifUtils utils;
  int jpeg_quality, thumbnail_jpeg_quality;
  camera_metadata_ro_entry entry;

  if (metadata.exists(ANDROID_JPEG_QUALITY)) {
    entry = metadata.find(ANDROID_JPEG_QUALITY);
    jpeg_quality = entry.data.u8[0];
  } else {
    ALOGE("%s: Could not find jpeg quality in metadata, defaulting to %d",
              __FUNCTION__, DEFAULT_JPEG_QUALITY);
    jpeg_quality = DEFAULT_JPEG_QUALITY;
  }

  if (metadata.exists(ANDROID_JPEG_THUMBNAIL_QUALITY)) {
    entry = metadata.find(ANDROID_JPEG_THUMBNAIL_QUALITY);
    thumbnail_jpeg_quality = entry.data.u8[0];
  } else {
    thumbnail_jpeg_quality = jpeg_quality;
  }

  if (!utils.Initialize(in_frame.GetData(), in_frame.GetWidth(),
                        in_frame.GetHeight(), thumbnail_jpeg_quality)) {
    ALOGE("%s: ExifUtils initialization failed.", __FUNCTION__);
    return false;
  }

  if (!SetExifTags(metadata, &utils)) {
    ALOGE("%s: Setting Exif tags failed.", __FUNCTION__);
    return false;
  }

  if (!utils.GenerateApp1()) {
    ALOGE("%s: Generating APP1 segment failed.", __FUNCTION__);
    return false;
  }

  JpegCompressor compressor;

  if (!compressor.CompressImage(in_frame.GetData(), in_frame.GetWidth(),
                                in_frame.GetHeight(), jpeg_quality,
                                utils.GetApp1Buffer(), utils.GetApp1Length())) {
    ALOGE("%s: JPEG image compression failed", __FUNCTION__);
    return false;
  }

  size_t buffer_length = compressor.GetCompressedImageSize();
  memcpy(out_frame->GetData(), compressor.GetCompressedImagePtr(),
         buffer_length);
  return true;
}

bool ImageProcessor::SetExifTags(const CameraMetadata& metadata,
                                                            ExifUtils* utils) {
  time_t raw_time = 0;
  struct tm time_info;
  bool time_available = time(&raw_time) != -1;
  localtime_r(&raw_time, &time_info);

  if (!utils->SetDateTime(time_info)) {
    ALOGE("%s: Setting data time failed.", __FUNCTION__);
    return false;
  }

  float focal_length;
  camera_metadata_ro_entry entry = metadata.find(ANDROID_LENS_FOCAL_LENGTH);

  if (entry.count) {
    focal_length = entry.data.f[0];
  } else {
    ALOGE("%s: Cannot find focal length in metadata.", __FUNCTION__);
    return false;
  }

  if (!utils->SetFocalLength(
          static_cast<uint32_t>(focal_length * kRationalPrecision),
          kRationalPrecision)) {
    ALOGE("%s: Setting focal length failed.", __FUNCTION__);
    return false;
  }

  if (metadata.exists(ANDROID_JPEG_GPS_COORDINATES)) {
    entry = metadata.find(ANDROID_JPEG_GPS_COORDINATES);
    if (entry.count < 3) {
      ALOGE("%s: Gps coordinates in metadata is not complete.", __FUNCTION__);
      return false;
    }
    if (!utils->SetGpsLatitude(entry.data.d[0])) {
      ALOGE("%s: Setting gps latitude failed.", __FUNCTION__);
      return false;
    }
    if (!utils->SetGpsLongitude(entry.data.d[1])) {
      ALOGE("%s: Setting gps longitude failed.", __FUNCTION__);
      return false;
    }
    if (!utils->SetGpsAltitude(entry.data.d[2])) {
      ALOGE("%s: Setting gps altitude failed.", __FUNCTION__);
      return false;
    }
  }

  if (metadata.exists(ANDROID_JPEG_GPS_PROCESSING_METHOD)) {
    entry = metadata.find(ANDROID_JPEG_GPS_PROCESSING_METHOD);
    std::string method_str(reinterpret_cast<const char*>(entry.data.u8));

    if (!utils->SetGpsProcessingMethod(method_str)) {
      ALOGE("%s: Setting gps processing method failed.", __FUNCTION__);
      return false;
    }
  }

  if (time_available && metadata.exists(ANDROID_JPEG_GPS_TIMESTAMP)) {
    entry = metadata.find(ANDROID_JPEG_GPS_TIMESTAMP);
    time_t timestamp = static_cast<time_t>(entry.data.i64[0]);

    if (gmtime_r(&timestamp, &time_info)) {
      if (!utils->SetGpsTimestamp(time_info)) {
        ALOGE("%s: Setting gps timestamp failed.", __FUNCTION__);
        return false;
      }
    } else {
      ALOGE("%s: Time tranformation failed.", __FUNCTION__);
      return false;
    }
  }

  if (metadata.exists(ANDROID_JPEG_ORIENTATION)) {
    entry = metadata.find(ANDROID_JPEG_ORIENTATION);

    if (!utils->SetOrientation(entry.data.i32[0])) {
      ALOGE("%s: Setting orientation failed.", __FUNCTION__);
      return false;
    }
  }

  if (metadata.exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
    entry = metadata.find(ANDROID_JPEG_THUMBNAIL_SIZE);

    if (entry.count < 2) {
      ALOGE("%s: Thumbnail size in metadata is not complete.", __FUNCTION__);
      return false;
    }

    int thumbnail_width = entry.data.i32[0];
    int thumbnail_height = entry.data.i32[1];

    if (thumbnail_width > 0 && thumbnail_height > 0) {
      if (!utils->SetThumbnailSize(static_cast<uint16_t>(thumbnail_width),
                                   static_cast<uint16_t>(thumbnail_height))) {
        ALOGE("%s: Setting thumbnail size failed.", __FUNCTION__);
        return false;
      }
    }
  }
  return true;
}

} // namespace arc
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
