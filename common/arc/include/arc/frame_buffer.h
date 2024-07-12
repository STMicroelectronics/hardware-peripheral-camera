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

#ifndef HAL_USB_FRAME_BUFFER_H_
#define HAL_USB_FRAME_BUFFER_H_

#include <stdint.h>

#include <memory>
#include <mutex>
#include <string>

#include <android-base/unique_fd.h>
#include <hardware/gralloc.h>
#include <helper/mapper_helper.h>

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

using ::android::hardware::camera::common::V1_0::helper::MapperHelper;

class FrameBuffer {
 public:
  FrameBuffer();

  virtual ~FrameBuffer();

  // If mapped successfully, the address will be assigned to |data_| and return
  // 0. Otherwise, returns -EINVAL.
  virtual int Map() = 0;

  // Unmaps the mapped address. Returns 0 for success.
  virtual int Unmap() = 0;

  uint8_t* GetData() const { return data_; }
  size_t GetDataSize() const { return data_size_; }
  size_t GetBufferSize() const { return buffer_size_; }
  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  uint32_t GetFourcc() const { return fourcc_; }

  void SetFourcc(uint32_t fourcc) { fourcc_ = fourcc; }
  virtual int SetDataSize(size_t data_size);

 protected:
  uint8_t* data_;

  // The number of bytes used in the buffer.
  size_t data_size_;

  // The number of bytes allocated in the buffer.
  size_t buffer_size_;

  // Frame resolution.
  uint32_t width_;
  uint32_t height_;

  // This is V4L2_PIX_FMT_* in linux/videodev2.h.
  uint32_t fourcc_;
};

// AllocatedFrameBuffer is used for the buffer from hal malloc-ed. User should
// be aware to manage the memory.
class AllocatedFrameBuffer : public FrameBuffer {
 public:
  explicit AllocatedFrameBuffer(int buffer_size);
  explicit AllocatedFrameBuffer(uint8_t* buffer, int buffer_size);
  ~AllocatedFrameBuffer() override;

  // No-op for the two functions.
  int Map() override { return 0; }
  int Unmap() override { return 0; }

  void SetWidth(uint32_t width) { width_ = width; }
  void SetHeight(uint32_t height) { height_ = height; }
  int SetDataSize(size_t data_size) override;
  void Reset();

 private:
  std::unique_ptr<uint8_t[]> buffer_;
};

// V4L2FrameBuffer is used for the buffer from V4L2CameraDevice. Maps the fd
// in constructor. Unmaps and closes the fd in destructor.
class V4L2FrameBuffer : public FrameBuffer {
 public:
  V4L2FrameBuffer(base::unique_fd fd, int buffer_size, uint32_t width,
                  uint32_t height, uint32_t fourcc);

  // Unmaps |data_| and closes |fd_|.
  ~V4L2FrameBuffer();

  int Map() override;
  int Unmap() override;
  int GetFd() const { return fd_.get(); }

 private:
  // File descriptor of V4L2 frame buffer.
  base::unique_fd fd_;

  bool is_mapped_;

  // Lock to guard |is_mapped_|.
  std::mutex lock_;
};

// GrallocFrameBuffer is used for the buffer from Android framework. Uses
// CameraBufferMapper to lock and unlock the buffer.
class GrallocFrameBuffer : public FrameBuffer {
 public:
  GrallocFrameBuffer(buffer_handle_t buffer, uint32_t width, uint32_t height,
                     uint32_t fourcc, uint32_t device_buffer_length,
                     uint32_t stream_usage, MapperHelper *mapper);
  ~GrallocFrameBuffer();

  int Map() override;
  int Unmap() override;

  int Map(hidl_handle acquire_fence);
  int Unmap(hidl_handle *release_fence);

 private:
  // The currently used buffer for |buffer_mapper_| operations.
  buffer_handle_t buffer_;

  // Used to import gralloc buffer.
  MapperHelper *mapper_;

  bool is_mapped_;

  // Lock to guard |is_mapped_|.
  std::mutex lock_;

  // Camera stream and device buffer context.
  uint32_t device_buffer_length_;
  uint32_t stream_usage_;
};

} // namespace arc
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // HAL_USB_FRAME_BUFFER_H_
