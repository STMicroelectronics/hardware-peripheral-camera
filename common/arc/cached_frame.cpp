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

#define LOG_TAG "android.hardware.camera.common@1.0-arc.stm32mpu"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "cached_frame.h"

#include <errno.h>
#include <libyuv.h>

#include "common_types.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

CachedFrame::CachedFrame()
    : source_frame_(nullptr),
      cropped_buffer_capacity_(0),
      yu12_frame_(new AllocatedFrameBuffer(0)) {}

CachedFrame::~CachedFrame() { UnsetSource(); }

int CachedFrame::SetSource(const FrameBuffer* frame, int rotate_degree) {
  source_frame_ = frame;
  int res = ConvertToYU12();

  if (res != 0) {
    return res;
  }

  if (rotate_degree > 0) {
    res = CropRotateScale(rotate_degree);
  }

  return res;
}

void CachedFrame::UnsetSource() { source_frame_ = nullptr; }

uint8_t* CachedFrame::GetSourceBuffer() const {
  return source_frame_->GetData();
}

size_t CachedFrame::GetSourceDataSize() const {
  return source_frame_->GetDataSize();
}

uint32_t CachedFrame::GetSourceFourCC() const {
  return source_frame_->GetFourcc();
}

uint8_t* CachedFrame::GetCachedBuffer() const { return yu12_frame_->GetData(); }

uint32_t CachedFrame::GetCachedFourCC() const {
  return yu12_frame_->GetFourcc();
}

uint32_t CachedFrame::GetWidth() const { return yu12_frame_->GetWidth(); }

uint32_t CachedFrame::GetHeight() const { return yu12_frame_->GetHeight(); }

size_t CachedFrame::GetConvertedSize(int fourcc) const {
  return ImageProcessor::GetConvertedSize(fourcc, yu12_frame_->GetWidth(),
                                          yu12_frame_->GetHeight());
}

int CachedFrame::Convert(const CameraMetadata& metadata, FrameBuffer* out_frame,
                         bool video_hack) {
  ALOGV("%s: enter (frame buffer : %p, video hack: %d",
                                          __FUNCTION__, out_frame, video_hack);

  if (video_hack && out_frame->GetFourcc() == V4L2_PIX_FMT_YVU420) {
    out_frame->SetFourcc(V4L2_PIX_FMT_YUV420);
  }

  FrameBuffer* source_frame = yu12_frame_.get();

  ALOGV("%s: w: %d; frame w: %d, h: %d, frame h: %d", __FUNCTION__,
        GetWidth(), out_frame->GetWidth(), GetHeight(), out_frame->GetHeight());

  if (GetWidth() != out_frame->GetWidth() ||
                                        GetHeight() != out_frame->GetHeight()) {
    size_t cache_size = ImageProcessor::GetConvertedSize(
                                                      yu12_frame_->GetFourcc(),
                                                      out_frame->GetWidth(),
                                                      out_frame->GetHeight());

    if (cache_size == 0) {
      ALOGE("%s: cache size is null", __FUNCTION__);
      return -EINVAL;
    } else if (!scaled_frame_ || cache_size > scaled_frame_->GetBufferSize()) {
      scaled_frame_.reset(new AllocatedFrameBuffer(cache_size));
    }

    scaled_frame_->SetWidth(out_frame->GetWidth());
    scaled_frame_->SetHeight(out_frame->GetHeight());

    ALOGV("%s: scalling image", __FUNCTION__);

    ImageProcessor::Scale(*yu12_frame_.get(), scaled_frame_.get());

    source_frame = scaled_frame_.get();
  }

  ALOGV("%s: Processing conversion", __FUNCTION__);

  return ImageProcessor::ConvertFormat(metadata, *source_frame, out_frame);
}

int CachedFrame::ConvertToYU12() {
  size_t cache_size = ImageProcessor::GetConvertedSize(V4L2_PIX_FMT_YUV420,
                                                    source_frame_->GetWidth(),
                                                    source_frame_->GetHeight());

  if (cache_size == 0) {
    return -EINVAL;
  }

  yu12_frame_->SetDataSize(cache_size);
  yu12_frame_->SetFourcc(V4L2_PIX_FMT_YUV420);
  yu12_frame_->SetWidth(source_frame_->GetWidth());
  yu12_frame_->SetHeight(source_frame_->GetHeight());

  int res = ImageProcessor::ConvertFormat(CameraMetadata(), *source_frame_,
                                          yu12_frame_.get());
  if (res) {
    ALOGE("%s: Convert from %s to YU12 fails",
              __FUNCTION__, FormatToString(source_frame_->GetFourcc()).c_str());
    return res;
  }

  return 0;
}

int CachedFrame::CropRotateScale(int rotate_degree) {
  // TODO(henryhsu): Move libyuv part to ImageProcessor.
  ALOGV("%s: enter: %d", __FUNCTION__, rotate_degree);

  if (yu12_frame_->GetHeight() % 2 != 0 || yu12_frame_->GetWidth() % 2 != 0) {
    ALOGE("%s: yu12_frame_ has odd dimension: %dx%d",
              __FUNCTION__, yu12_frame_->GetWidth(), yu12_frame_->GetHeight());
    return -EINVAL;
  }

  if (yu12_frame_->GetHeight() > yu12_frame_->GetWidth()) {
    ALOGE("%s: yu12_frame_ is tall frame already: %dx%d",
              __FUNCTION__, yu12_frame_->GetWidth(), yu12_frame_->GetHeight());
    return -EINVAL;
  }

  // Step 1: Crop and rotate
  //
  //   Original frame                  Cropped frame              Rotated frame
  // --------------------               --------
  // |     |      |     |               |      |                 ---------------
  // |     |      |     |               |      |                 |             |
  // |     |      |     |   =======>>   |      |     =======>>   |             |
  // |     |      |     |               |      |                 ---------------
  // |     |      |     |               |      |
  // --------------------               --------
  //
  int cropped_width = yu12_frame_->GetHeight() * yu12_frame_->GetHeight() /
                      yu12_frame_->GetWidth();
  if (cropped_width % 2 == 1) {
    // Make cropped_width to the closest even number.
    cropped_width++;
  }
  int cropped_height = yu12_frame_->GetHeight();
  int margin = (yu12_frame_->GetWidth() - cropped_width) / 2;

  int rotated_height = cropped_width;
  int rotated_width = cropped_height;

  int rotated_y_stride = rotated_width;
  int rotated_uv_stride = rotated_width / 2;
  size_t rotated_size =
      rotated_y_stride * rotated_height + rotated_uv_stride * rotated_height;
  if (rotated_size > cropped_buffer_capacity_) {
    cropped_buffer_.reset(new uint8_t[rotated_size]);
    cropped_buffer_capacity_ = rotated_size;
  }
  uint8_t* rotated_y_plane = cropped_buffer_.get();
  uint8_t* rotated_u_plane =
      rotated_y_plane + rotated_y_stride * rotated_height;
  uint8_t* rotated_v_plane =
      rotated_u_plane + rotated_uv_stride * rotated_height / 2;
  libyuv::RotationMode rotation_mode = libyuv::RotationMode::kRotate90;
  switch (rotate_degree) {
    case 90:
      rotation_mode = libyuv::RotationMode::kRotate90;
      break;
    case 270:
      rotation_mode = libyuv::RotationMode::kRotate270;
      break;
    default:
      ALOGE("%s: Invalid rotation degree: %d", __FUNCTION__, rotate_degree);
      return -EINVAL;
  }
  // This libyuv method first crops the frame and then rotates it 90 degrees
  // clockwise.
  int res = libyuv::ConvertToI420(
      yu12_frame_->GetData(), yu12_frame_->GetDataSize(), rotated_y_plane,
      rotated_y_stride, rotated_u_plane, rotated_uv_stride, rotated_v_plane,
      rotated_uv_stride, margin, 0, yu12_frame_->GetWidth(),
      yu12_frame_->GetHeight(), cropped_width, cropped_height, rotation_mode,
      libyuv::FourCC::FOURCC_I420);

  if (res) {
    ALOGE("%s: ConvertToI420 failed: %d", __FUNCTION__, res);
    return res;
  }

  // Step 2: Scale
  //
  //                               Final frame
  //  Rotated frame            ---------------------
  // --------------            |                   |
  // |            |  =====>>   |                   |
  // |            |            |                   |
  // --------------            |                   |
  //                           |                   |
  //                           ---------------------
  //
  //
  res = libyuv::I420Scale(
      rotated_y_plane, rotated_y_stride, rotated_u_plane, rotated_uv_stride,
      rotated_v_plane, rotated_uv_stride, rotated_width, rotated_height,
      yu12_frame_->GetData(), yu12_frame_->GetWidth(),
      yu12_frame_->GetData() +
          yu12_frame_->GetWidth() * yu12_frame_->GetHeight(),
      yu12_frame_->GetWidth() / 2,
      yu12_frame_->GetData() +
          yu12_frame_->GetWidth() * yu12_frame_->GetHeight() * 5 / 4,
      yu12_frame_->GetWidth() / 2, yu12_frame_->GetWidth(),
      yu12_frame_->GetHeight(), libyuv::FilterMode::kFilterNone);
  ALOGE_IF(res, "%s: I420Scale failed: %d", __FUNCTION__, res);
  return res;
}

} // namespace arc
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
