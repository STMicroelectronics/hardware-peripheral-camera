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

#define LOG_TAG "android.hardware.camera.device@3.2-tracker.stm32mp1"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "camera_tracker.h"

#include <inttypes.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using ::android::hardware::graphics::common::V1_0::PixelFormat;

const Stream CameraTracker::InvalidStream = {
  -1,                         /* id */
  StreamType::OUTPUT,         /* streamType */
  0,                          /* width */
  0,                          /* height */
  PixelFormat::IMPLEMENTATION_DEFINED,  /* format */
  0,                          /* usage */
  0,                          /* dataspace */
  StreamRotation::ROTATION_0  /* rotation */
};

const StreamBuffer CameraTracker::InvalidStreamBuffer = {
  -1, /* streamId */
  0,  /* bufferId */
  nullptr,  /* buffer */
  BufferStatus::ERROR,  /* status */
  nullptr,  /* acquireFence */
  nullptr,  /* releaseFence */
};

/* Tracked Stream methods */
Stream& CameraTracker::trackStream(const Stream& stream) {
  return tracked_streams_[stream.id] = stream;
}

const Stream& CameraTracker::getStream(uint32_t id) const {
  auto it = tracked_streams_.find(id);

  if (it == tracked_streams_.end()) {
    return CameraTracker::InvalidStream;
  }

  return it->second;
}

int CameraTracker::intersectStreams(const hidl_vec<Stream>& streams,
                                    std::vector<buffer_handle_t> *buffers) {
  for (auto it = tracked_streams_.begin(); it != tracked_streams_.end(); ) {
    bool find = false;
    const Stream& tracked_stream = it->second;

    for (const Stream& stream : streams) {
      if (stream.id == tracked_stream.id) {
        find = true;
        break;
      }
    }

    /* If the the stream is unused, don't track it anymore and free associated
     * buffers
     */
    if (!find) {
      it = tracked_streams_.erase(it);

      untrackStreamBuffers(tracked_stream.id, buffers);
    } else {
      ++it;
    }
  }

  return 0;
}

void CameraTracker::clearStreams() {
  tracked_streams_.clear();
}

  /* Tracked Buffers methods */
int CameraTracker::trackBuffer(uint32_t stream_id,
                               uint64_t buffer_id, buffer_handle_t buffer) {
  stream_buffers_[stream_id][buffer_id] = buffer;

  return 0;
}

buffer_handle_t CameraTracker::getBuffer(uint32_t stream_id,
                                         uint64_t buffer_id) const {
  auto it = stream_buffers_.find(stream_id);

  if (it == stream_buffers_.end()) {
    ALOGI("%s: no stream saved for the given stream id %u",
                                                      __FUNCTION__, stream_id);
    return nullptr;
  }

  const BufferMap& tracked_buffers = it->second;
  auto buffer_it = tracked_buffers.find(buffer_id);

  if (buffer_it == tracked_buffers.end()) {
    ALOGI("%s: no buffer id %" PRIu64 " tracked for stream %u",
                                            __FUNCTION__, buffer_id, stream_id);
    return nullptr;
  }

  return buffer_it->second;
}

int CameraTracker::untrackStreamBuffers(uint32_t stream_id,
                                        std::vector<buffer_handle_t> *buffers) {
  auto it = stream_buffers_.find(stream_id);

  if (it == stream_buffers_.end()) {
    ALOGI("%s: no stream saved for the given stream id %u",
                                                      __FUNCTION__, stream_id);
    return -1;
  }

  BufferMap& tracked_buffers = it->second;

  if (buffers) {
    for (const auto& buffer : tracked_buffers) {
      buffers->push_back(buffer.second);
    }
  }

  tracked_buffers.clear();
  stream_buffers_.erase(it);

  return 0;
}

int CameraTracker::untrackBuffer(uint32_t stream_id,
                                 uint64_t buffer_id, buffer_handle_t *buffer) {
  auto it = stream_buffers_.find(stream_id);

  if (it == stream_buffers_.end()) {
    ALOGE("%s: no stream saved for the given stream id %u",
                                                      __FUNCTION__, stream_id);
    return -1;
  }

  BufferMap& tracked_buffers = it->second;
  auto buffer_it = tracked_buffers.find(buffer_id);

  if (buffer_it == tracked_buffers.end()) {
    ALOGE("%s: no buffer id %" PRIu64 " tracked for stream %u",
                                            __FUNCTION__, buffer_id, stream_id);
    return -1;
  } else {
    if (buffer) {
      *buffer = buffer_it->second;
    }
    tracked_buffers.erase(buffer_it);
  }

  return 0;
}

void CameraTracker::clearBuffers(std::vector<buffer_handle_t> *buffers) {

  if (buffers) {
    for (auto it = stream_buffers_.begin(); it != stream_buffers_.end(); ++it) {
      BufferMap& tracked_buffers = it->second;

      for (const auto& buffer_it : tracked_buffers) {
        buffers->push_back(buffer_it.second);
      }
    }
  }

  stream_buffers_.clear();
}

/* Tracked Capture methods*/
int CameraTracker::trackCapture(const CaptureRequest& request) {
  uint32_t capture_id = request.frameNumber;

  TrackedCapture tracked_capture = {
    capture_id,                             /* id */
    std::queue<StreamBuffer> { },           /* output_buffers */
    CameraTracker::InvalidStreamBuffer,     /* input_buffer */
    std::vector<StreamBuffer>(),           /* output_results*/
    InvalidStreamBuffer                     /* input result */
  };

  for (const StreamBuffer& stream_buffer : request.outputBuffers) {
    StreamBuffer buffer = stream_buffer;

    buffer.buffer = nullptr;
    tracked_capture.output_buffers.push(std::move(buffer));
  }

  if (request.inputBuffer.streamId != -1) {
    tracked_capture.input_buffer = request.inputBuffer;
    tracked_capture.input_buffer.buffer = nullptr;
  }

  capture_list_.push_back(std::move(tracked_capture));

  ALOGV("%s: track request %d", __FUNCTION__, capture_id);

  return capture_id;
}

bool CameraTracker::hasActiveCapture() const {
  return capture_list_.size() > 0;
}

StreamBuffer CameraTracker::popNextOutputBuffer(int *capture_id) {
  for (TrackedCapture& capture : capture_list_) {
    if (capture.output_buffers.size() > 0) {
      StreamBuffer res = capture.output_buffers.front();

      if (capture_id) {
        *capture_id = capture.id;
      }

      capture.output_buffers.pop();

      return res;
    }
  }

  if (capture_id) {
    *capture_id = -1;
  }

  return CameraTracker::InvalidStreamBuffer;
}

StreamBuffer CameraTracker::popNextInputBuffer(int *capture_id) {
  for (TrackedCapture& capture : capture_list_) {
    if (capture.input_buffer.streamId != -1) {
      StreamBuffer res = capture.input_buffer;

      if (capture_id) {
        *capture_id = capture.id;
      }

      capture.input_buffer = CameraTracker::InvalidStreamBuffer;

      return res;
    }
  }

  if (capture_id) {
    *capture_id = -1;
  }

  return CameraTracker::InvalidStreamBuffer;
}

int CameraTracker::saveOutputBuffer(StreamBuffer stream_buffer,
                                    uint32_t capture_id) {
  for (TrackedCapture& capture : capture_list_) {
    if (capture.id == capture_id) {
      capture.output_results.push_back(std::move(stream_buffer));

      return 0;
    }
  }

  ALOGE("%s: no result tracked for the capture %u", __FUNCTION__, capture_id);
  return -1;
}

int CameraTracker::saveInputBuffer(StreamBuffer stream_buffer,
                                   uint32_t capture_id) {
  for (TrackedCapture& capture : capture_list_) {
    if (capture.id == capture_id) {
      capture.input_result = std::move(stream_buffer);

      return 0;
    }
  }

  ALOGE("%s: no result tracked for the capture %u", __FUNCTION__, capture_id);
  return -1;
}

int CameraTracker::isCaptureCompleted(uint32_t capture_id) const {
  for (const TrackedCapture& capture : capture_list_) {
    if (capture.id == capture_id) {
      return capture.output_buffers.size() == 0 &&
                capture.input_buffer.streamId == -1;
    }
  }

  ALOGE("%s: no result tracked for the capture %u", __FUNCTION__, capture_id);
  return -1;
}

std::vector<StreamBuffer> CameraTracker::popOutputResults(uint32_t capture_id) {
  for (TrackedCapture& capture : capture_list_) {
    if (capture.id == capture_id) {
      std::vector<StreamBuffer> res = std::move(capture.output_results);

      capture.output_results = std::vector<StreamBuffer>();

      return res;
    }
  }

  ALOGE("%s: no result tracked for the capture %u", __FUNCTION__, capture_id);
  return std::vector<StreamBuffer>();
}

StreamBuffer CameraTracker::popInputResult(uint32_t capture_id) {
  for (TrackedCapture& capture : capture_list_) {
    if (capture.id == capture_id) {
      StreamBuffer res = std::move(capture.input_result);

      capture.input_result = InvalidStreamBuffer;

      ALOGV("%s: pop input result for capture %d", __FUNCTION__, capture_id);

      return res;
    }
  }

  ALOGE("%s: no result tracked for the capture %u", __FUNCTION__, capture_id);
  return InvalidStreamBuffer;
}

int CameraTracker::setCaptureSettings(uint32_t capture_id,
                      std::shared_ptr<const helper::CameraMetadata> settings) {
  auto it = tracked_settings_.find(capture_id);

  if (it == tracked_settings_.end()) {
    TrackedSettings res = {
      settings, /* settings */
      1         /* partial_count */
    };

    tracked_settings_[capture_id] = std::move(res);

    return 0;
  }

  TrackedSettings& res = it->second;

  res.settings = settings;
  ++res.partial_count;

  return 0;
}

std::shared_ptr<const helper::CameraMetadata>
CameraTracker::getCaptureSettings(uint32_t capture_id,
                                  uint32_t *partial_count) const {
  auto it = tracked_settings_.find(capture_id);

  if (it == tracked_settings_.end()) {
    ALOGE("%s: no settings tracked for the capture %u",
                                                      __FUNCTION__, capture_id);
    return nullptr;
  }

  const TrackedSettings& settings = it->second;
  std::shared_ptr<const helper::CameraMetadata> res = settings.settings;
  if (partial_count) {
    *partial_count = settings.partial_count;
  }

  return res;
}

int CameraTracker::untrackCapture(uint32_t capture_id) {
  for (auto it = capture_list_.begin(); it != capture_list_.end(); ++it) {
    if (it->id == capture_id) {
      capture_list_.erase(it);
      break;
    }
  }

  auto it = tracked_settings_.find(capture_id);

  if (it != tracked_settings_.end()) {
    tracked_settings_.erase(it);
  }

  return 0;
}

void CameraTracker::clearCaptures() {
  capture_list_.clear();
  tracked_settings_.clear();
}

}  // namespace implementation
}  // namespace V3_2
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
