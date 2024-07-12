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

#ifndef AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_STREAM_H
#define AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_STREAM_H

#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/device/BufferStatus.h>
#include <aidl/android/hardware/camera/device/Stream.h>
#include <aidl/android/hardware/camera/device/StreamBuffer.h>
#include <aidlcommonsupport/NativeHandle.h>

#include <unordered_map>
#include <list>
#include <queue>
#include <thread>

#include <CameraMetadata.h>

#include <arc/frame_buffer.h>
#include <helper/mapper_helper.h>
#include <v4l2/v4l2_wrapper.h>

#include "v4l2_stream_config.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using namespace ::android::hardware::camera::common::V1_0;

using aidl::android::hardware::graphics::common::BufferUsage;

using aidl::android::hardware::camera::common::Status;
using aidl::android::hardware::camera::device::BufferStatus;
using aidl::android::hardware::camera::device::Stream;
using aidl::android::hardware::camera::device::StreamBuffer;

using ::android::hardware::camera::common::V1_0::helper::MapperHelper;
using ::android::hardware::camera::common::V1_0::v4l2::StreamFormat;
using ::android::hardware::camera::common::V1_0::v4l2::StreamFormats;
using ::android::hardware::camera::common::V1_0::v4l2::V4L2Wrapper;

class V4l2Stream {
public:

  struct TrackedStreamBuffer {
    int32_t frame_number;
    int32_t stream_id;
    int64_t buffer_id;
    BufferStatus status;
    native_handle_t *acquire_fence;
    native_handle_t *release_fence;
    std::shared_ptr<const helper::CameraMetadata> settings;
  };

  struct CallbackInterface {
    virtual ~CallbackInterface() = default;

    virtual void processCaptureBufferError(const TrackedStreamBuffer &sb) = 0;
    virtual void processCaptureBufferResult(const TrackedStreamBuffer &sb) = 0;
  };

public:
  static std::shared_ptr<V4l2Stream> Create(const V4l2StreamConfig& config,
                                            const Stream &stream,
                                            CallbackInterface *cb);

  V4l2Stream(const V4l2StreamConfig& config, const Stream &stream,
             CallbackInterface *cb);
  virtual ~V4l2Stream();

  const V4l2StreamConfig &configuration() const { return config_; }
  const Stream &stream() const { return stream_; }

  void setUsage(const BufferUsage &usage) { stream_.usage = usage; }

  bool isCompatible(const Stream& stream);
  Status update(const Stream& stream);

  void freeBuffer(int64_t id);

  Status processCaptureBuffer(
      int32_t frame_number,
      const StreamBuffer &buffer,
      const std::shared_ptr<const helper::CameraMetadata> &settings);

  void flush();

private:
  Status initialize();
  Status configureDriver();
  Status configurePipeline(const StreamFormat &format);

  Status findBestFitFormat(const Stream &stream, StreamFormat *stream_format);

  void captureRequestThread();

  Status processCaptureResultConversion(
       const std::unique_ptr<arc::V4L2FrameBuffer> &v4l2_buffer,
       TrackedStreamBuffer &capture_info);

  Status importBuffer(const StreamBuffer &stream_buffer);
  Status saveBuffer(const StreamBuffer &stream_buffer);
  buffer_handle_t getSavedBuffer(int64_t id);

private:
  static MapperHelper mapper_helper_;

  V4l2StreamConfig config_;
  Stream stream_;
  CallbackInterface *cb_;

  /* V4L2 Wrapper */
  std::shared_ptr<V4L2Wrapper> v4l2_wrapper_;
  std::shared_ptr<V4L2Wrapper::Connection> connection_;

  /* All formats supported by the driver */
  StreamFormats supported_formats_;
  /* Supported driver formats from which we can convert to another one */
  StreamFormats qualified_formats_;

  /* V4l2 buffers */
  std::vector<std::unique_ptr<arc::V4L2FrameBuffer>> v4l2_buffers_;
  std::queue<int> available_buffers_;
  std::mutex v4l2_buffer_mutex_;

  /* Framework buffers */
  std::unordered_map<int64_t, buffer_handle_t> buffer_map_;
  std::mutex buffer_mutex_;

  std::mutex flush_mutex_;

  /* Capture variable */
  std::mutex capture_mutex_;
  std::condition_variable capture_cond_;
  std::queue<TrackedStreamBuffer> capture_queue_;
  std::unique_ptr<std::thread> capture_result_thread_;
  bool capture_active_;

  std::mutex convert_mutex_;

  bool started_;
};

} // implementation
} // device
} // camera
} // hardware
} // android

#endif // AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_STREAM_H
