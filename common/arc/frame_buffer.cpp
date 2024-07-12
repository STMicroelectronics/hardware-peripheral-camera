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

#include "utils/Log.h"

#include "frame_buffer.h"

#include <sys/mman.h>

#include <utility>

#include "image_processor.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

FrameBuffer::FrameBuffer()
    : data_(nullptr),
      data_size_(0),
      buffer_size_(0),
      width_(0),
      height_(0),
      fourcc_(0) {}

FrameBuffer::~FrameBuffer() {}

int FrameBuffer::SetDataSize(size_t data_size) {
  if (data_size > buffer_size_) {
    ALOGE("%s: Buffer overflow: Buffer only has %zu, but data needs %zu",
              __FUNCTION__, buffer_size_, data_size);
    return -EINVAL;
  }
  data_size_ = data_size;
  return 0;
}

AllocatedFrameBuffer::AllocatedFrameBuffer(int buffer_size) {
  buffer_.reset(new uint8_t[buffer_size]);
  buffer_size_ = buffer_size;
  data_ = buffer_.get();
}

AllocatedFrameBuffer::AllocatedFrameBuffer(uint8_t* buffer, int buffer_size) {
  buffer_.reset(buffer);
  buffer_size_ = buffer_size;
  data_ = buffer;
}

AllocatedFrameBuffer::~AllocatedFrameBuffer() {}

int AllocatedFrameBuffer::SetDataSize(size_t size) {
  if (size > buffer_size_) {
    buffer_.reset(new uint8_t[size]);
    buffer_size_ = size;
    data_ = buffer_.get();
  }
  data_size_ = size;
  return 0;
}

void AllocatedFrameBuffer::Reset() { memset(data_, 0, buffer_size_); }

V4L2FrameBuffer::V4L2FrameBuffer(base::unique_fd fd, int buffer_size,
                                 uint32_t width, uint32_t height,
                                 uint32_t fourcc)
    : fd_(std::move(fd)), is_mapped_(false) {
  buffer_size_ = buffer_size;
  width_ = width;
  height_ = height;
  fourcc_ = fourcc;
}

V4L2FrameBuffer::~V4L2FrameBuffer() {
  if (Unmap()) {
    ALOGE("%s: Unmap failed", __FUNCTION__);
  }
}

int V4L2FrameBuffer::Map() {
  std::lock_guard l(lock_);

  if (is_mapped_) {
    ALOGE("%s: The buffer is already mapped", __FUNCTION__);
    return -EINVAL;
  }

  void* addr = mmap(NULL, buffer_size_, PROT_READ, MAP_SHARED, fd_.get(), 0);

  if (addr == MAP_FAILED) {
    ALOGE("%s: mmap() failed: %s", __FUNCTION__,  strerror(errno));
    return -EINVAL;
  }

  data_ = static_cast<uint8_t*>(addr);
  is_mapped_ = true;

  return 0;
}

int V4L2FrameBuffer::Unmap() {
  std::lock_guard l(lock_);

  if (is_mapped_ && munmap(data_, buffer_size_)) {
    ALOGE("%s: mummap() failed: %s", __FUNCTION__, strerror(errno));
    return -EINVAL;
  }

  is_mapped_ = false;

  return 0;
}

GrallocFrameBuffer::GrallocFrameBuffer(buffer_handle_t buffer, uint32_t width,
                                       uint32_t height, uint32_t fourcc,
                                       uint32_t device_buffer_length,
                                       uint32_t stream_usage,
                                       MapperHelper *mapper)
    : buffer_(buffer),
      mapper_(mapper),
      is_mapped_(false),
      device_buffer_length_(device_buffer_length),
      stream_usage_(stream_usage) {
  width_ = width;
  height_ = height;
  fourcc_ = fourcc;
}

GrallocFrameBuffer::~GrallocFrameBuffer() {
  if (Unmap()) {
    ALOGE("%s: Unmap failed", __FUNCTION__);
  }
}

int GrallocFrameBuffer::Map(hidl_handle acquire_fence) {
  std::lock_guard l(lock_);

  ALOGV("%s: enter", __FUNCTION__);

  if (is_mapped_) {
    ALOGE("%s: The buffer is already mapped", __FUNCTION__);
    return -EINVAL;
  }

  void* addr = nullptr;

  switch (fourcc_) {
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YUYV:
    {
      YCbCrLayout yuv_data = mapper_->lockYCbCr(buffer_, stream_usage_, width_,
                                                height_, acquire_fence);
      addr = yuv_data.y;
      break;
    }
    case V4L2_PIX_FMT_JPEG:
      addr = mapper_->lock(buffer_, stream_usage_, device_buffer_length_, 1,
                           acquire_fence);
      break;
    case V4L2_PIX_FMT_ABGR32:
    case V4L2_PIX_FMT_ARGB32:
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_RGB565:
      addr = mapper_->lock(buffer_, stream_usage_, width_, height_,
                           acquire_fence);
      break;
    default:
      return -EINVAL;
  }

  if (!addr) {
    ALOGE("%s: Failed to lock buffer", __FUNCTION__);
    return -EINVAL;
  }

  data_ = static_cast<uint8_t*>(addr);

  if (fourcc_ == V4L2_PIX_FMT_YVU420 || fourcc_ == V4L2_PIX_FMT_YUV420 ||
      fourcc_ == V4L2_PIX_FMT_NV21 || fourcc_ == V4L2_PIX_FMT_ARGB32 ||
      fourcc_ == V4L2_PIX_FMT_ABGR32 || fourcc_ == V4L2_PIX_FMT_RGB565) {
    buffer_size_ = ImageProcessor::GetConvertedSize(fourcc_, width_, height_);

    ALOGV("%s: calculated converted size: %zu", __FUNCTION__, buffer_size_);
  }

  is_mapped_ = true;
  return 0;
}

int GrallocFrameBuffer::Map() {
  /* suppose wait is already done and use an empty fence*/
  hidl_handle acquire_fence;

  return Map(acquire_fence);
}

int GrallocFrameBuffer::Unmap(hidl_handle *release_fence) {
  std::lock_guard l(lock_);

  if (is_mapped_) {
   int res = mapper_->unlock(buffer_, release_fence);

    if (res) {
      ALOGE("%s: Failed to unmap buffer: %d", __FUNCTION__, res);
      return -EINVAL;
    }
  }

  is_mapped_ = false;

  return 0;
}

int GrallocFrameBuffer::Unmap() {
  return Unmap(nullptr);
}

} // namespace arc
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
