/*
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

#ifndef CAMERA_TRACKER_H
#define CAMERA_TRACKER_H

#include <list>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#include <android/hardware/camera/device/3.2/types.h>
#include "CameraMetadata.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using namespace ::android::hardware::camera::common::V1_0;

class CameraTracker {
public:
  /* Tracked Stream methods */
  Stream& trackStream(const Stream& stream);
  const Stream& getStream(uint32_t id) const;
  int intersectStreams(const hidl_vec<Stream>& streams,
                       std::vector<buffer_handle_t> *buffers);
  void clearStreams();

  /* Tracked Buffers methods */
  int trackBuffer(uint32_t stream_id,
                  uint64_t buffer_id, buffer_handle_t buffer);
  buffer_handle_t getBuffer(uint32_t stream_id, uint64_t buffer_id) const;
  int untrackStreamBuffers(uint32_t stream_id,
                           std::vector<buffer_handle_t> *buffers);
  int untrackBuffer(uint32_t stream_id,
                    uint64_t buffer_id, buffer_handle_t *buffer);
  void clearBuffers(std::vector<buffer_handle_t> *buffers);

  /* Tracked Capture methods*/
  int trackCapture(const CaptureRequest& request);
  bool hasActiveCapture() const;
  StreamBuffer popNextOutputBuffer(int *capture_id);
  StreamBuffer popNextInputBuffer(int *capture_id);
  int saveOutputBuffer(StreamBuffer stream_buffer, uint32_t capture_id);
  int saveInputBuffer(StreamBuffer stream_buffer, uint32_t capture_id);
  int isCaptureCompleted(uint32_t capture_id) const;
  std::vector<StreamBuffer> popOutputResults(uint32_t capture_id);
  StreamBuffer popInputResult(uint32_t capture_id);
  int setCaptureSettings(uint32_t capture_id,
                       std::shared_ptr<const helper::CameraMetadata> settings);
  std::shared_ptr<const helper::CameraMetadata> getCaptureSettings(
                                                uint32_t capture_id,
                                                uint32_t *partial_count) const;
  int untrackCapture(uint32_t capture_id);
  void clearCaptures();

private:
  struct TrackedCapture {
    uint32_t id;
    std::queue<StreamBuffer> output_buffers;
    StreamBuffer input_buffer;
    std::vector<StreamBuffer> output_results;
    StreamBuffer input_result;
  };

  struct TrackedSettings {
    std::shared_ptr<const helper::CameraMetadata> settings;
    uint32_t partial_count;
  };

public:
  static const Stream InvalidStream;
  static const StreamBuffer InvalidStreamBuffer;

private:
  typedef std::unordered_map<uint64_t, buffer_handle_t> BufferMap;
  std::unordered_map<uint32_t, Stream> tracked_streams_;
  std::unordered_map<uint32_t, BufferMap> stream_buffers_;

  std::list<TrackedCapture> capture_list_;
  std::map<uint32_t, TrackedSettings> tracked_settings_;

};

}  // namespace implementation
}  // namespace V3_2
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android

#endif /* !CAMERA_TRACKER_H */
