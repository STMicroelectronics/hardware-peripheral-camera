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

#ifndef V4L2_CAMERA_HAL_V4L2_WRAPPER_H_
#define V4L2_CAMERA_HAL_V4L2_WRAPPER_H_

#include <android-base/unique_fd.h>
#include <mutex>
#include <set>
#include <string>

#include "stream_format.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace v4l2 {

class V4L2Wrapper {
 public:

  V4L2Wrapper(const std::string device_path);
  virtual ~V4L2Wrapper();

  virtual const std::string getDevicePath() const;
  static int IsV4L2VideoDevice(int fd, v4l2_capability *cap);

  // Helper class to ensure all opened connections are closed.
  class Connection {
    public:
      Connection(std::shared_ptr<V4L2Wrapper> device)
        : device_(std::move(device)), connect_result_(device_->Connect()) {}

      ~Connection() {
        if (connect_result_ == 0) {
          device_->Disconnect();
        }
      }

    // Check whether the connection succeeded or not.
    inline int status() const { return connect_result_; }

    private:
      std::shared_ptr<V4L2Wrapper> device_;
      const int connect_result_;
  };

  struct V4L2Buffer {
    uint8_t* start;
    uint32_t length;
    uint32_t index;
    std::shared_ptr<StreamFormat> format;
  };

  /* Turn the stream on or off. */
  virtual int StreamOn();
  virtual int StreamOff();

  /* Manage controls. */
  virtual int QueryControl(uint32_t control_id, v4l2_query_ext_ctrl* result);
  virtual int GetControl(uint32_t control_id, int32_t* value);
  virtual int SetControl(uint32_t control_id,
                         int32_t desired,
                         int32_t* result = nullptr);

  /* Manage format. */
  virtual int GetFormats(std::set<uint32_t>* v4l2_formats);
  virtual int GetSupportedFormats(const std::set<uint32_t>& v4l2_formats,
                                  StreamFormats *formats);
  virtual int GetFormatFrameSizes(uint32_t v4l2_format,
                                  std::set<std::array<int32_t, 2>>* sizes);

  /* Durations are returned in ns. */
  virtual int GetFormatFrameDurationRange(uint32_t v4l2_format,
                                          const std::array<int32_t, 2>& size,
                                          std::array<int64_t, 2>* duration_range);

  virtual int SetFormat(const StreamFormat& resolved_format);
  /* Request/release userspace buffer mode via VIDIOC_REQBUFS. */
  virtual int RequestBuffers(uint32_t num_buffers, uint32_t *num_done,
                                                        uint32_t *buffer_size);
  virtual int ExportBuffer(uint32_t index, int32_t *fd);

  virtual int EnqueueRequest(uint32_t index);
  virtual int DequeueRequest(uint32_t *index);

 private:
  /* Connect or disconnect to the device. Access by creating/destroying
   * a V4L2Wrapper::Connection object.
   */
  int Connect();
  void Disconnect();

  /* Perform an ioctl call in a thread-safe fashion. */
  template <typename T>
  int ioctlLocked(int request, T data);

  inline bool Connected() { return device_fd_.get() >= 0; }

private:
  /* The camera device path. For example, /dev/video0. */
  const std::string device_path_;
  /* The opened device fd. */
  base::unique_fd device_fd_;
  /* Whether or not the device supports the extended control query. */
  bool extended_query_supported_;
  /* The format this device is set up for. */
  std::shared_ptr<StreamFormat> format_;
  /* Lock protecting use of the device. */
  std::mutex device_lock_;
  /* Lock protecting connecting/disconnecting the device. */
  std::mutex connection_lock_;
  /* Reference count connections. */
  int connection_count_;

  uint32_t buffer_size_;

  friend class Connection;

  /* disallow copy constructor */
  V4L2Wrapper(const V4L2Wrapper&);
  void operator=(const V4L2Wrapper&);

};

} // namespace v4l2
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // V4L2_CAMERA_HAL_V4L2_WRAPPER_H_
