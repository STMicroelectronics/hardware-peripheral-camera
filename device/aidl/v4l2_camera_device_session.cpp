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

//#define LOG_NDEBUG 0

#include "v4l2_camera_device_session.h"

#include <log/log.h>

#include <inttypes.h>

#include <v4l2/stream_format.h>

#include <isp.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using aidl::android::hardware::common::NativeHandle;

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::PixelFormat;

using aidl::android::hardware::camera::device::Stream;
using aidl::android::hardware::camera::device::StreamRotation;
using aidl::android::hardware::camera::device::StreamType;
using aidl::android::hardware::camera::device::NotifyMsg;
using aidl::android::hardware::camera::device::ShutterMsg;
using aidl::android::hardware::camera::device::ErrorMsg;
using aidl::android::hardware::camera::device::CaptureResult;

using aidl::android::hardware::camera::metadata::ScalerAvailableStreamUseCases;

using ::android::hardware::camera::common::V1_0::v4l2::StreamFormat;

#define REQ_FMQ_SIZE_PROPERTY "ro.vendor.camera.req.fmq.size"
#define CAMERA_REQUEST_METADATA_QUEUE_SIZE (1 << 20) /* 1MB */

#define RES_FMQ_SIZE_PROPERTY "ro.vendor.camera.res.fmq.size"
#define CAMERA_RESULT_METADATA_QUEUE_SIZE  (1 << 20) /* 1MB */

static NativeHandle makeToAidlIfNotNull(const native_handle_t* nh) {
  if (nh == nullptr) {
    return NativeHandle();
  }
  return makeToAidl(nh);
}

std::shared_ptr<V4l2CameraDeviceSession> V4l2CameraDeviceSession::Create(
    const V4l2CameraConfig &config,
    std::shared_ptr<Metadata> metadata,
    std::shared_ptr<StaticProperties> static_info,
    const std::shared_ptr<ICameraDeviceCallback> &callback) {
  std::shared_ptr<V4l2CameraDeviceSession> session =
      ndk::SharedRefBase::make<V4l2CameraDeviceSession>(config,
                                                        metadata,
                                                        static_info,
                                                        callback);

  if (session == nullptr) {
    ALOGE("%s (%d): Cannot create V4l2CameraDeviceSession !",
        __func__, config.id);
    return nullptr;
  }

  Status status = session->initialize();
  if (status != Status::OK) {
    ALOGE("%s (%d): Initializing V4l2CameraDeviceSession failed !",
        __func__, config.id);
    return nullptr;
  }

  return session;
}

V4l2CameraDeviceSession::V4l2CameraDeviceSession(
    const V4l2CameraConfig &config,
    std::shared_ptr<Metadata> metadata,
    std::shared_ptr<StaticProperties> static_info,
    const std::shared_ptr<ICameraDeviceCallback> &callback)
  : config_(config),
    callback_(callback),
    metadata_(metadata),
    static_info_(static_info),
    closed_(false)
{ }

V4l2CameraDeviceSession::~V4l2CameraDeviceSession()
{ }

Status V4l2CameraDeviceSession::initialize() {
  ALOGV("%s (%d): Initializing camera device session", __func__, config_.id);

  /* Configure request_metadata_queue_ */
  int32_t req_fmq_size = property_get_int32(REQ_FMQ_SIZE_PROPERTY, -1);
  if (req_fmq_size < 0) {
    req_fmq_size = CAMERA_REQUEST_METADATA_QUEUE_SIZE;
  } else {
    ALOGV("%s (%d): request FMQ size overriden to %d",
        __func__, config_.id, req_fmq_size);
  }

  request_metadata_queue_ = std::make_unique<MetadataQueue>(
      static_cast<size_t>(req_fmq_size), false);
  if (!request_metadata_queue_->isValid()) {
    ALOGE("%s (%d): invalid request fmq !", __func__, config_.id);
    return Status::INTERNAL_ERROR;
  }

  /* Configure result_metadata_queue_ */
  int32_t res_fmq_size = property_get_int32(RES_FMQ_SIZE_PROPERTY, -1);
  if (res_fmq_size < 0) {
    res_fmq_size = CAMERA_RESULT_METADATA_QUEUE_SIZE;
  } else {
    ALOGV("%s (%d): result FMQ size override to %d",
        __func__, config_.id, res_fmq_size);
  }

  result_metadata_queue_ = std::make_unique<MetadataQueue>(
      static_cast<size_t>(res_fmq_size), false);
  if (!result_metadata_queue_->isValid()) {
    ALOGE("%s (%d): invalid result fmq !", __func__, config_.id);
    return Status::INTERNAL_ERROR;
  }

  /* Launch the ISP thread */
  isp_thread_.reset(
      new std::thread(&V4l2CameraDeviceSession::ISPThread, this));

  return Status::OK;
}

Status V4l2CameraDeviceSession::initStatus() {
  if (closed_)
    return Status::INTERNAL_ERROR;

  return Status::OK;
}

ScopedAStatus V4l2CameraDeviceSession::close() {
  stream_map_.clear();
  closed_ = true;

  isp_cond_.notify_one();
  if (isp_thread_)
    isp_thread_->join();

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDeviceSession::configureStreams(
    const StreamConfiguration &requested_configuration,
    std::vector<HalStream> *result) {
  if (result == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  /* Check the requested configuration is valid */
  Status status = configureStreamsVerification(requested_configuration);
  if (status != Status::OK) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(status));
  }

  /* Turn off the v4l2 driver stream and clean */
  status = configureStreamsClean(requested_configuration);
  if (status != Status::OK) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(status));
  }

  /* Find compatible format between streams and v4l2 driver */
  status = configureDriverStreams(requested_configuration);
  if (status != Status::OK) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(status));
  }

  /* Save new stream, update existing one if necessary and complete result */
  status = configureStreamsResult(requested_configuration, result);
  if (status != Status::OK) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(status));
  }

  ALOGV("%s (%d): configure streams successfull", __func__, config_.id);

  return ScopedAStatus::ok();
}

Status V4l2CameraDeviceSession::configureStreamsVerification(
    const StreamConfiguration &requested_configuration) {
  Status status = initStatus();
  if (status != Status::OK)
    return status;

  if (!static_info_->StreamConfigurationSupported(&requested_configuration)) {
    ALOGE("%s (%d): stream combination not supported", __func__, config_.id);
    return Status::ILLEGAL_ARGUMENT;
  }

  uint32_t available_size = config_.streams.size();
  uint32_t requested_size = requested_configuration.streams.size();
  uint32_t current_size = stream_map_.size();
  if (requested_size > available_size + current_size) {
    ALOGE("%s (%d): not enough v4l2 streams available to handle the requested "
          "output streams (requested streams: %d, available: %d) !",
           __func__, config_.id, requested_size, available_size + current_size);
    return Status::INTERNAL_ERROR;
  }

  for (const Stream &stream : requested_configuration.streams) {
    auto it = stream_map_.find(stream.id);
    if (it == stream_map_.cend()) {
      ALOGI("%s: requesting new stream 0x%x (%dx%d) - 0x%lx", __func__,
                stream.format, stream.width, stream.height, stream.usage);
      /* The requested stream is new and not tracked yet */
      continue;
    }

    std::shared_ptr<V4l2Stream> &v4l2_stream = it->second;
    if (!v4l2_stream->isCompatible(stream)) {
      ALOGE("%s (%d): only orientation and usage of used stream can change !",
                __func__, config_.id);
      return Status::ILLEGAL_ARGUMENT;
    }
  }

  return Status::OK;
}

Status V4l2CameraDeviceSession::configureStreamsClean(
    const StreamConfiguration &requested_configuration) {
  std::unordered_map<uint32_t, std::shared_ptr<V4l2Stream>> aux = stream_map_;

  /* erase reused streams from aux to keep only old streams */
  for (const Stream &stream : requested_configuration.streams)
    aux.erase(stream.id);

  /* erase old streams from stream_map_ and put back the stream config in the
   * configuration list
   */
  for (const auto &p : aux) {
    stream_map_.erase(p.first);
    config_.streams.push_back(p.second->configuration());
  }

  return Status::OK;
}

Status V4l2CameraDeviceSession::configureDriverStreams(
    const StreamConfiguration &requested_configuration) {

  for (const Stream &stream : requested_configuration.streams) {
    decltype(stream_map_)::const_iterator it = stream_map_.find(stream.id);
    if (it != stream_map_.cend()) {
      const std::shared_ptr<V4l2Stream> &v4l2_stream = it->second;
      Status status = v4l2_stream->update(stream);
      if (status != Status::OK)
        return status;
    } else {
      V4l2StreamConfig stream_conf;
      Status status = findBestStreamConfiguration(stream, stream_conf);
      if (status != Status::OK)
        return status;

      std::shared_ptr<V4l2Stream> v4l2_stream = V4l2Stream::Create(
                                                    stream_conf, stream, this);
      if (v4l2_stream == nullptr) {
        config_.streams.push_back(std::move(stream_conf));
        return Status::INTERNAL_ERROR;
      }

      stream_map_[stream.id] = std::move(v4l2_stream);
    }
  }

  return Status::OK;
}

Status V4l2CameraDeviceSession::findBestStreamConfiguration(
    const Stream &stream, V4l2StreamConfig &res) {
  auto it = config_.streams.cbegin();
  for (; it != config_.streams.cend(); ++it) {
    const V4l2StreamConfig &config = *it;
    if (
      (
        (
          stream.useCase == ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_STILL_CAPTURE ||
          stream.useCase == ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_VIDEO_RECORD
        ) &&
        config.usage == V4l2StreamConfig::Capture
      ) ||
      (
        (
          stream.useCase == ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_PREVIEW ||
          stream.useCase == ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT
        ) &&
        stream.format != PixelFormat::BLOB &&
        config.usage == V4l2StreamConfig::Preview
      ) ||
      (
        stream.format == PixelFormat::IMPLEMENTATION_DEFINED &&
        config.usage == V4l2StreamConfig::Preview
      ) ||
      (
        stream.format == PixelFormat::BLOB &&
        config.usage == V4l2StreamConfig::Capture
      )) {
      break;
    }
  }

  /* fallback to first available configuration */
  if (it == config_.streams.cend())
    it = config_.streams.cbegin();

  res = *it;
  config_.streams.erase(it);

  return Status::OK;
}

Status V4l2CameraDeviceSession::configureStreamsResult(
    const StreamConfiguration &requested_configuration,
    std::vector<HalStream> *result) {

  for (const Stream &stream : requested_configuration.streams) {
    const std::shared_ptr<V4l2Stream> &v4l2_stream = stream_map_[stream.id];
    const V4l2StreamConfig &stream_conf = v4l2_stream->configuration();

    HalStream res;
    res.id = stream.id;
    if (stream.format == PixelFormat::IMPLEMENTATION_DEFINED) {
      res.overrideFormat = static_cast<PixelFormat>(
          StreamFormat::V4L2ToHalPixelFormat(
              stream_conf.implementation_defined_format));
    } else {
      res.overrideFormat = stream.format;
    }

    if (stream.streamType == StreamType::OUTPUT) {
      res.consumerUsage = static_cast<BufferUsage>(0);
      res.producerUsage = static_cast<BufferUsage>(
          static_cast<int64_t>(res.producerUsage) |
          static_cast<int64_t>(BufferUsage::CPU_WRITE_OFTEN)
      );
    } else {
      res.consumerUsage = static_cast<BufferUsage>(
          static_cast<int64_t>(res.consumerUsage) |
          static_cast<int64_t>(BufferUsage::CPU_READ_OFTEN)
      );
      res.producerUsage = static_cast<BufferUsage>(0);
    }

    res.maxBuffers = stream_conf.num_buffers;
    if (res.maxBuffers == 0) {
      ALOGE("%s (%d): not enough buffer for stream on %s !",
                __func__, config_.id, stream_conf.node);
      return Status::INTERNAL_ERROR;
    }

    v4l2_stream->setUsage(static_cast<BufferUsage>(
        static_cast<int64_t>(stream.usage) |
        static_cast<int64_t>(res.producerUsage) |
        static_cast<int64_t>(res.consumerUsage)
    ));

    res.overrideDataSpace = stream.dataSpace;

    ALOGV("%s (%d): stream %d configured: type: %d, format: 0x%x, w: %d, h: %d"
          ", rotation: %d, consumerUsage: 0x%" PRIx64
          ", producerUsage: 0x%" PRIx64 ", dataspace: %d, maxBuffers: %d",
              __func__, config_.id, stream.id, stream.streamType,
              res.overrideFormat, stream.width, stream.height,
              stream.rotation, res.consumerUsage,
              res.producerUsage, res.overrideDataSpace, res.maxBuffers);

    result->push_back(std::move(res));
  }

  return Status::OK;
}

ScopedAStatus V4l2CameraDeviceSession::constructDefaultRequestSettings(
    RequestTemplate type, CameraMetadata *settings) {
  Status status = initStatus();
  if (status != Status::OK) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(status));
  }

  if (settings == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  int type_int = static_cast<int>(type);
  if (!(type_int > 0 && type_int < MetadataCommon::kRequestTemplateCount)) {
    ALOGE("%s: invalid template request type: %d", __func__, type_int);
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  if (!default_settings_[type_int]) {
    /* No template already initilaized.
     * generate one if the device support it
     */
    if (!static_info_->TemplateSupported(type)) {
      ALOGW("%s: camera doesn't support template type %d", __func__, type_int);
      return ScopedAStatus::fromServiceSpecificError(
                  static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
    }

    /* Initialize the template */
    std::unique_ptr<CameraMetadataHelper> new_template =
                                      std::make_unique<CameraMetadataHelper>();
    int res = metadata_->GetRequestTemplate(type_int, new_template.get());
    if (res != 0) {
      ALOGE("%s: failed to generate template of type %d", __func__, type_int);
      return ScopedAStatus::fromServiceSpecificError(
                  static_cast<int32_t>(Status::INTERNAL_ERROR));
    }

    default_settings_[type_int] = std::move(new_template);
  }

  const camera_metadata_t *raw_metadata =
                                      default_settings_[type_int]->getAndLock();
  const uint8_t *data = reinterpret_cast<const uint8_t *>(raw_metadata);
  int size = get_camera_metadata_size(raw_metadata);

  settings->metadata = std::vector<uint8_t>(data, data + size);

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDeviceSession::flush() {
  std::lock_guard l(flush_mutex_);
  Status status = initStatus();
  if (status != Status::OK) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(status));
  }

  for (const auto &p : stream_map_)
    p.second->flush();

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDeviceSession::getCaptureRequestMetadataQueue(
    MQDescriptor<int8_t, SynchronizedReadWrite> *queue) {
  if (queue == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  *queue = request_metadata_queue_->dupeDesc();

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDeviceSession::getCaptureResultMetadataQueue(
    MQDescriptor<int8_t, SynchronizedReadWrite> *queue) {
  if (queue == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  *queue = result_metadata_queue_->dupeDesc();

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDeviceSession::isReconfigurationRequired(
    const CameraMetadata &old_session_params,
    const CameraMetadata &new_session_params, bool *required) {
  if (required == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  (void)(old_session_params);
  (void)(new_session_params);

  /* partial reconfiguration is not supported */
  *required = true;

  return ScopedAStatus::ok();
}

void V4l2CameraDeviceSession::updateBufferCaches(
    const std::vector<BufferCache> &caches_to_remove) {
  for (const BufferCache &cache : caches_to_remove) {
    decltype(stream_map_)::const_iterator it = stream_map_.find(cache.streamId);
    if (it == stream_map_.cend()) {
      ALOGE("%s (%d): stream %d, buffer %" PRIu64 " is not tracked !",
                __func__, config_.id, cache.streamId, cache.bufferId);
      continue;
    }

    it->second->freeBuffer(cache.bufferId);
  }
}

ScopedAStatus V4l2CameraDeviceSession::processCaptureRequest(
    const std::vector<CaptureRequest> &requests,
    const std::vector<BufferCache> &caches_to_remove,
    int32_t *num_processed) {
  ALOGV("%s (%d): enter: nb request: %zu",
              __func__, config_.id, requests.size());

  if (num_processed == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  updateBufferCaches(caches_to_remove);

  std::lock_guard l(flush_mutex_);

  Status res = Status::OK;
  for (size_t i = 0; i < requests.size(); ++i) {
    Status s = processOneCaptureRequest(requests[i]);
    if (s == Status::OK) {
      ++(*num_processed);
    } else {
      res = s;
      break;
    }
  }

  if (res != Status::OK)
    return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(res));

  return ScopedAStatus::ok();
}

Status V4l2CameraDeviceSession::processOneCaptureRequest(
    const CaptureRequest &request) {
  ALOGV("%s (%d): enter: frame: %d, nb output buffers: %zu", __func__,
            config_.id, request.frameNumber, request.outputBuffers.size());

  Status status = processCaptureRequestVerification(request);
  if (status != Status::OK)
    return status;

  std::shared_ptr<helper::CameraMetadata> settings;
  status = processCaptureRequestMetadata(request, settings);
  if (status != Status::OK)
    return status;

  status = processCaptureRequestEnqueue(request, settings);
  if (status != Status::OK)
    return status;

  if (settings)
    processCaptureMetadataResult(request.frameNumber, *settings);

  /* TODO: handle inputBuffer ? */

  return Status::OK;
}

Status V4l2CameraDeviceSession::processCaptureRequestVerification(
    const CaptureRequest &request) {
  Status status = initStatus();

  if (status == Status::INTERNAL_ERROR) {
    ALOGE("%s: capture request with bad camera status (closed) ", __func__);
    return status;
  }

  /* If it is the first CaptureRequest, the settings field must not be empty */
  if (request.fmqSettingsSize == 0 &&
        previous_settings_.size() == 0 &&
        request.settings.metadata.size() == 0) {
    ALOGE("%s (%d): capture request settings must not be empty for first "
          "request !", __func__, config_.id);
    return Status::ILLEGAL_ARGUMENT;
  }

  if (request.outputBuffers.size() == 0) {
    ALOGE("%s (%d): invalid number of output buffers: %zu < 1",
              __func__, config_.id, request.outputBuffers.size());
    return Status::ILLEGAL_ARGUMENT;
  } else if (request.outputBuffers[0].status != BufferStatus::OK) {
    ALOGE("%s (%d): need at least one valid output buffers", __func__, config_.id);
    return Status::ILLEGAL_ARGUMENT;
  }

  return Status::OK;
}

Status V4l2CameraDeviceSession::processCaptureRequestMetadata(
    const CaptureRequest &request,
    std::shared_ptr<helper::CameraMetadata> &metadata) {
  std::vector<uint8_t> settings;

  /* Capture Settings */
  if (request.fmqSettingsSize > 0) {
    settings.resize(request.fmqSettingsSize);

    if (!request_metadata_queue_->read(reinterpret_cast<int8_t *>(settings.data()),
                                       request.fmqSettingsSize)) {
      ALOGE("%s (%d): capture request settings metadata couldn't be read from "
            "fmq !", __func__, config_.id);
      processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
      return Status::ILLEGAL_ARGUMENT;
    }
  } else if (request.settings.metadata.size() > 0) {
    settings = request.settings.metadata;
  } else {
    settings = previous_settings_;
  }

  previous_settings_ = std::move(settings);

  const camera_metadata_t *aux =
      reinterpret_cast<const camera_metadata_t *>(previous_settings_.data());

  /* clone metadata to the helper */
  metadata = std::make_shared<helper::CameraMetadata>();
  *metadata = aux;

  if (!metadata_->IsValidRequest(*metadata)) {
    ALOGE("%s (%d): invalid request settings !", __func__, config_.id);
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return Status::ILLEGAL_ARGUMENT;
  }

  /* Setting and getting settings are best effort here,
   * since there's no way to know through V4L2 exactly what
   * settings are used for a buffer unless we were to enqueue them
   * one at a time, which would be too slow
   */
  int res = metadata_->SetRequestSettings(*metadata);
  if (res) {
    ALOGE("%s (%d): failed to set settings: %d !", __func__, config_.id, res);
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return Status::INTERNAL_ERROR;
  }

  res = metadata_->FillResultMetadata(metadata.get());
  if (res) {
    ALOGE("%s (%d): failed to fill result metadata !", __func__, config_.id);
    /* Notify the metadata won't be available for this capture and continue */
    processCaptureRequestError(request, ErrorCode::ERROR_RESULT);
  }

  return Status::OK;
}

Status V4l2CameraDeviceSession::processCaptureRequestEnqueue(
    const CaptureRequest &request,
    const std::shared_ptr<const helper::CameraMetadata> &settings) {

  for (const StreamBuffer &sb : request.outputBuffers) {
    decltype(stream_map_)::const_iterator it = stream_map_.find(sb.streamId);
    if (it == stream_map_.cend()) {
      ALOGE("%s (%d): The stream %d is not tracked !",
                __func__, config_.id, sb.streamId);
      processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
      return Status::ILLEGAL_ARGUMENT;
    }

    Status status = it->second->processCaptureBuffer(request.frameNumber,
                                                     sb, settings);
    if (status != Status::OK)
      return status;
  }

  return Status::OK;
}

void V4l2CameraDeviceSession::processCaptureMetadataResult(
    int32_t frame_number, const helper::CameraMetadata &settings) {
  int64_t timestamp = 0;
  MetadataCommon::SingleTagValue(settings, ANDROID_SENSOR_TIMESTAMP, &timestamp);

  int64_t exposure_time = 0;
  MetadataCommon::SingleTagValue(settings, ANDROID_SENSOR_EXPOSURE_TIME, &exposure_time);

  NotifyMsg msg;
  ShutterMsg shutter = {
    .frameNumber = frame_number,
    .timestamp = timestamp,
    .readoutTimestamp = timestamp + exposure_time,
  };
  msg.set<NotifyMsg::Tag::shutter>(std::move(shutter));
  callback_->notify({ std::move(msg) });

  const camera_metadata_t *metadata = settings.getAndLock();
  uint32_t size = get_camera_metadata_size(metadata);
  const int8_t *data = reinterpret_cast<const int8_t *>(metadata);

  CaptureResult result;
  result.frameNumber = frame_number;
  result.inputBuffer.streamId = -1;
  result.partialResult = 1;

  bool res = result_metadata_queue_->write(data, size);
  if (!res) {
    ALOGE("%s: failed to write metadata to FMQ", __func__);
    result.fmqResultSize = 0;
    result.result.metadata = std::vector<uint8_t>(data, data + size);
  } else {
    result.fmqResultSize = size;
  }

  std::vector<CaptureResult> results;
  results.push_back(std::move(result));
  {
    /* the callback must not be called concurently */
    std::lock_guard lock(result_mutex_);
    callback_->processCaptureResult(std::move(results));
  }
}

void V4l2CameraDeviceSession::processCaptureRequestError(
    const CaptureRequest &request, ErrorCode error) {
  NotifyMsg error_msg;
  ErrorMsg msg = {
    .frameNumber = request.frameNumber,
    .errorStreamId = -1,
    .errorCode = error,
  };
  error_msg.set<NotifyMsg::Tag::error>(std::move(msg));
  callback_->notify({ std::move(error_msg) });

  if (error == ErrorCode::ERROR_DEVICE)
    close();
}

void V4l2CameraDeviceSession::processCaptureBufferError(
    const V4l2Stream::TrackedStreamBuffer &tsb) {
  ALOGE("%s: buffer error frame: %d, stream: %d",
            __func__, tsb.frame_number, tsb.stream_id);
  NotifyMsg error_msg;
  ErrorMsg error = {
    .frameNumber = tsb.frame_number,
    .errorStreamId = tsb.stream_id,
    .errorCode = ErrorCode::ERROR_BUFFER
  };
  error_msg.set<NotifyMsg::Tag::error>(std::move(error));
  callback_->notify({ std::move(error_msg) });

  V4l2Stream::TrackedStreamBuffer aux = tsb;
  aux.status = BufferStatus::ERROR;
  aux.release_fence = tsb.acquire_fence;

  processCaptureBufferResult(std::move(aux));
}

void V4l2CameraDeviceSession::processCaptureBufferResult(
    const V4l2Stream::TrackedStreamBuffer &tsb) {
  ALOGV("%s: buffer result frame: %d, stream: %d",
            __func__, tsb.frame_number, tsb.stream_id);
  StreamBuffer sb = {
    .streamId = tsb.stream_id,
    .bufferId = tsb.buffer_id,
    .status = tsb.status,
    .releaseFence = makeToAidlIfNotNull(tsb.release_fence)
  };
  CaptureResult result = {
    .frameNumber = tsb.frame_number,
    .fmqResultSize = 0,
    .partialResult = 0
  };
  result.outputBuffers.push_back(std::move(sb));
  result.inputBuffer.streamId = -1;

  std::vector<CaptureResult> results;
  results.push_back(std::move(result));

  {
    /* the callback must not be called concurently */
    std::lock_guard lock(result_mutex_);
    callback_->processCaptureResult(std::move(results));
  }
}

ScopedAStatus V4l2CameraDeviceSession::signalStreamFlush(
    const std::vector<int32_t> &stream_ids,
    int32_t stream_config_counter) {
  // Ignore this call
  (void)(stream_ids);
  (void)(stream_config_counter);

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDeviceSession::switchToOffline(
    const std::vector<int32_t> &streams_to_keep,
    CameraOfflineSessionInfo *offline_session_info,
    std::shared_ptr<ICameraOfflineSession> *session) {
  if (offline_session_info == nullptr || session == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  (void)(streams_to_keep);

  // Not supported
  return ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
}

void V4l2CameraDeviceSession::ISPThread() {
  ALOGI("%s: ISP Thread started", __func__);

  static struct isp_descriptor isp_desc = {
    .isp_fd = -1,
    .stat_fd = -1,
  };
  int ret;

  ret = discover_dcmipp(&isp_desc);
  if (ret) {
    ALOGE("%s: failed to get DCMIPP ISP information (error %d)", __func__, ret);
    return;
  }

  int32_t frequency = property_get_int32("vendor.camera.isp.update.frequency", 10000);
  ALOGI("%s: ISP executed every %d ms", __func__, frequency);

  std::unique_lock<std::mutex> lock(isp_mutex_);
  while (!closed_) {
    ret = set_white_balance(&isp_desc);
    if (ret) {
      ALOGE("%s: failed to execute ISP set_white_balance function (error %d)", __func__, ret);
    }

    isp_cond_.wait_for(lock, std::chrono::milliseconds(frequency), [this] { return closed_; });
  }

  close_dcmipp(&isp_desc);

  ALOGI("%s: ISP Thread ended", __func__);
}

} // implementation
} // device
} // camera
} // hardware
} // android

