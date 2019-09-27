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

#define LOG_TAG "android.hardware.camera.device@3.2-service.stm32mp1"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "camera_device_session.h"

#include <unordered_set>

#include <arc/cached_frame.h>
#include <arc/image_processor.h>
#include <CameraMetadata.h>
#include <cutils/properties.h>
#include <sync/sync.h>
#include <utils/Trace.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using ::android::hardware::camera::common::V1_0::arc::ImageProcessor;
using ::android::hardware::camera::common::V1_0::v4l2::PixelFormat;
using ::android::hardware::graphics::common::V1_0::BufferUsage;

#define REQ_FMQ_SIZE_PROPERTY "ro.camera.req.fmq.size"
#define CAMERA_REQUEST_METADATA_QUEUE_SIZE (1 << 20) /* 1MB */

#define RES_FMQ_SIZE_PROPERTY "ro.camera.res.fmq.size"
#define CAMERA_RESULT_METADATA_QUEUE_SIZE (1 << 20) /* 1MB */

#define CAMERA_SYNC_TIMEOUT 5000

MapperHelper CameraDeviceSession::mapper_helper_;

CameraDeviceSession::CameraDeviceSession(
                                  std::shared_ptr<V4L2Wrapper> v4l2_wrapper,
                                  std::shared_ptr<Metadata> metadata,
                                  std::shared_ptr<StaticProperties> static_info,
                                  const sp<ICameraDeviceCallback>& callback)
  : closed_ { false },
    disconnected_ { true },
    init_fail_ { false },
    started_ { false },
    state_lock_ { },
    flush_queue_lock_ { },
    flush_result_lock_ { },
    callback_ { callback },
    previous_settings_ { },
    metadata_ { metadata },
    static_info_ { static_info },
    v4l2_buffers_ { },
    v4l2_wrapper_ { v4l2_wrapper },
    connection_ { nullptr },
    supported_formats_ { },
    qualified_formats_ { },
    implem_defined_format_ { },
    camera_tracker_ { },
    capture_tracker_lock_ { },
    capture_wait_lock_ { },
    capture_active_ { },
    capture_result_thread_ { },
    request_metadata_queue_ { nullptr },
    result_metadata_queue_ { nullptr } {

}

int CameraDeviceSession::initialize() {
  /* Configure request_metadata_queue_ */
  ALOGV("%s: initializing camera device session", __FUNCTION__);

  int32_t req_fmq_size = property_get_int32(REQ_FMQ_SIZE_PROPERTY, -1);

  if (req_fmq_size < 0) {
    req_fmq_size = CAMERA_REQUEST_METADATA_QUEUE_SIZE;
  } else {
    ALOGV("%s: request FMQ size overridden to %d", __FUNCTION__, req_fmq_size);
  }

  request_metadata_queue_ = std::make_unique<RequestMetadataQueue>(
                                      static_cast<size_t>(req_fmq_size), false);

  if (!request_metadata_queue_->isValid()) {
    ALOGE("%s: invalid request fmq", __FUNCTION__);
    Mutex::Autolock _l(state_lock_);
    init_fail_ = true;
    return -1;
  }

  /* configure result_metadata_queue_ */
  int32_t res_fmq_size = property_get_int32(RES_FMQ_SIZE_PROPERTY, -1);

  if (res_fmq_size < 0) {
    res_fmq_size = CAMERA_RESULT_METADATA_QUEUE_SIZE;
  } else {
    ALOGV("%s: result FMQ size overridden to %d", __FUNCTION__, res_fmq_size);
  }

  result_metadata_queue_ = std::make_unique<ResultMetadataQueue>(
                                      static_cast<size_t>(res_fmq_size), false);

  if (!result_metadata_queue_->isValid()) {
    ALOGE("%s: invalid result fmq", __FUNCTION__);
    Mutex::Autolock _l(state_lock_);
    init_fail_ = true;
    return -1;
  }

  /* New connection to the V4L2 camera */
  connection_.reset(new V4L2Wrapper::Connection(v4l2_wrapper_));

  if (connection_->status()) {
    ALOGE("%s: v4l2 connection failure: %d",
                                          __FUNCTION__, connection_->status());
    Mutex::Autolock _l(state_lock_);
    init_fail_ = true;
    return -1;
  } else {
    Mutex::Autolock _l(state_lock_);
    disconnected_ = false;
  }

  /* Retrieve all supported formats */
  std::set<uint32_t> v4l2_pixel_format;

  if (v4l2_wrapper_->GetFormats(&v4l2_pixel_format)) {
    ALOGE("%s: failed to get formats", __FUNCTION__);
    Mutex::Autolock _l(state_lock_);
    init_fail_ = true;
    return -1;
  }

  if (v4l2_wrapper_->GetSupportedFormats(v4l2_pixel_format,
                                         &supported_formats_)) {
    ALOGE("%s: failed to get supported formats", __FUNCTION__);
    Mutex::Autolock _l(state_lock_);
    init_fail_ = true;
    return -1;
  }

  std::vector<uint32_t> v4l2_qualified_format;

  if (ImageProcessor::GetQualifiedFormats(v4l2_pixel_format,
                                          &v4l2_qualified_format)) {
    ALOGE("%s: can't get qualified formats", __FUNCTION__);
    Mutex::Autolock _l(state_lock_);
    init_fail_ = true;
    return -1;

  }

  qualified_formats_ = StreamFormat::GetQualifiedStreams(v4l2_qualified_format,
                                                           supported_formats_);

  implem_defined_format_ = V4L2_PIX_FMT_BGR32;

  /* Launch the capture thread */
  capture_result_thread_.reset(
            new std::thread(&CameraDeviceSession::captureRequestThread, this));

  return 0;
}

Status CameraDeviceSession::initStatus() const {
  Mutex::Autolock _l(state_lock_);

  if (init_fail_) {
      return Status::INTERNAL_ERROR;
  }
  if (disconnected_) {
      return Status::CAMERA_DISCONNECTED;
  }
  if (closed_) {
      return Status::INTERNAL_ERROR;
  }

  return Status::OK;
}

// Methods from ::android::hardware::camera::device::V3_2::ICameraDeviceSession follow.
Return<void> CameraDeviceSession::constructDefaultRequestSettings(
          RequestTemplate type, constructDefaultRequestSettings_cb _hidl_cb) {
  Status status = initStatus();
  CameraMetadata out_metadata;
  int type_int = static_cast<int>(type);

  ALOGV("%s: enter", __FUNCTION__);

  if (status == Status::OK) {
    if (!(type_int > 0 && type_int < MetadataCommon::kRequestTemplateCount)) {
      ALOGE("%s: invalid template request type: %d", __FUNCTION__, type_int);
      status = Status::ILLEGAL_ARGUMENT;
      goto out_constructDefaultRequestSettings;
    }

    if (!default_settings_[type_int]) {
      /* No template already initialized.
       * generate one if the device support it.
       */
      if (!static_info_->TemplateSupported(type)) {
        ALOGW("%s: camera doesn't support template type %d",
                                                        __FUNCTION__, type_int);
        status = Status::ILLEGAL_ARGUMENT;
        goto out_constructDefaultRequestSettings;
      }

      /* Initialize the template */
      std::unique_ptr<CameraMetadataHelper> new_template =
                                      std::make_unique<CameraMetadataHelper>();
      int res = metadata_->GetRequestTemplate(type_int,
                                                           new_template.get());

      if (res) {
        ALOGE("%s: failed to generate template of type %d",
                                                        __FUNCTION__, type_int);
        status = Status::INTERNAL_ERROR;
        goto out_constructDefaultRequestSettings;
      }

      /* Save the template */
      default_settings_[type_int] = std::move(new_template);
    }

    const camera_metadata_t* raw_metadata =
                                      default_settings_[type_int]->getAndLock();

    out_metadata.setToExternal(
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t*>(raw_metadata)),
        get_camera_metadata_size(raw_metadata));
  }

out_constructDefaultRequestSettings:
  _hidl_cb(status, out_metadata);

  return Void();
}

/********** Configure Stream Section **********/

/*
 * @brief This method is called by the framework to configure new streams.
 *
 * This method is the root method of streams configuration. It calls
 * successively methods to :
 * - verify that the given @p requested_configuration can be configured
 * - clean any previously configured streams that are now unused
 * - configure the driver with the new stream
 * - compute the result to send to the framework with the given @p _hidl_cb
 *
 * @param requested_configuration: the streams and their attributes to configure
 * @param _hidl_cb: the callback to call when a result is available for this
 *                  method. The corresponding status is returned in this
 *                  callback:
 *                  Status::OK if the streams are successfully configured
 *                  Status::ILLEGAL_ARGUMENT if the streams can't be configured
 *                                           because of an invalid argument
 *                  Status::INTERNAL_ERROR if the streams can't be configured
 *                                         because of an internal HAL error
 *
 *
 */
Return<void> CameraDeviceSession::configureStreams(
        const StreamConfiguration& requested_configuration,
        configureStreams_cb _hidl_cb) {
  HalStreamConfiguration configuration;
  Status status = Status::OK;

  ALOGV("%s: enter", __FUNCTION__);

  /* Check the requested configuation is valid */
  status = configureStreamsVerification(requested_configuration);

  if (status != Status::OK) {
    _hidl_cb(status, configuration);
    return Void();
  }

  /* Turn off the V4L2 driver stream and clean */
  status = configureStreamClean(requested_configuration);

  if (status != Status::OK) {
    _hidl_cb(status, configuration);
    return Void();
  }

  /* Find compatible format between streams and V4L2 driver */
  status = configureDriverStream(requested_configuration);

  if (status != Status::OK) {
    _hidl_cb(status, configuration);
    return Void();
  }

  /* Save new stream, update existing one if necessary and complete result. */
  status = configureStreamResult(requested_configuration, &configuration);

  ALOGV("%s: configure stream successfull", __FUNCTION__);

  _hidl_cb(status, configuration);

  return Void();
}

/*
 * @brief This method checks if all conditions are met to configure the given
 *        stream
 *
 * To configure a new stream, all previous captures must be finished and
 * untracked.
 * There must be at least one output stream and at most one input stream to
 * configure.
 * For already configured streams, their properties can't change exept the
 * rotation and usage.
 * Note that for now, the HAL only handle stream without rotation
 * (StreamRotation::ROTATION_0)
 *
 * @param requested_configuration: the streams and their attributes to configure
 *
 * @return Status::OK if the stream can be configured
 *         Status::ILLEGAL_ARGUMENT if the stream can't be configured because of
 *                                  an invalid argument
 *         Status::INTERNAL_ERROR if the stream can't be configured because of
 *                                an internal HAL error
 */
Status CameraDeviceSession::configureStreamsVerification(
        const StreamConfiguration& requested_configuration) {
  Status status = initStatus();

  ALOGV("%s: enter", __FUNCTION__);

  {
    Mutex::Autolock _l(capture_tracker_lock_);
    if (camera_tracker_.hasActiveCapture()) {
      ALOGE("%s: trying to configure stream while there are still inflight"
                " buffers!", __FUNCTION__);
      return Status::INTERNAL_ERROR;
    }
  }

  if (status != Status::OK) {
    return status;
  }

  if (requested_configuration.streams.size() == 0) {
    ALOGE("%s: empty stream configuration array", __FUNCTION__);
    return Status::ILLEGAL_ARGUMENT;
  }

  uint32_t input = 0;
  uint32_t output = 0;

  /* Check streams properties */
  for (const Stream& stream : requested_configuration.streams) {
    // TODO: handle rotation ?
    if (stream.rotation > StreamRotation::ROTATION_0) {
      ALOGE("%s: invalid rotation: %d", __FUNCTION__, stream.rotation);
      return Status::ILLEGAL_ARGUMENT;
    }

    if (stream.streamType == StreamType::OUTPUT) {
      ++output;
    } else {
      ++input;
    }
  }

  if (input > 1 || output < 1) {
    ALOGE("%s: at most 1 input stream and at least 1 output stream are needed",
              __FUNCTION__);
    return Status::ILLEGAL_ARGUMENT;
  }

  /* Check existing streams properties still ok */
  for (const Stream& stream : requested_configuration.streams) {
    capture_tracker_lock_.lock();
    const Stream& s = camera_tracker_.getStream(stream.id);
    capture_tracker_lock_.unlock();

    if (s == CameraTracker::InvalidStream) {
      /* The requested stream is new and not tracked yet */
      continue;
    }

    if (s.streamType != stream.streamType || s.width != stream.width ||
            s.height != stream.height || s.format != stream.format ||
            s.dataSpace != stream.dataSpace) {
      ALOGE("%s: only orientation and usage of used stream can change.",
                                                                  __FUNCTION__);
      ALOGV("%s: old stream: (type: %d, width: %d, height: %d, format: 0x%x, dataspace: %d) "
                "new stream: (type: %d, width: %d, height: %d, format: 0x%x, dataspace: %d)",
            __FUNCTION__, s.streamType, s.width, s.height, s.format, s.dataSpace,
            stream.streamType, stream.width, stream.height, stream.format, stream.dataSpace);
      return Status::ILLEGAL_ARGUMENT;
    }
  }

  return Status::OK;
}

/*
 * @brief This method cleans the driver and allocated objects from a previous
 *        stream
 *
 * The V4L2 driver stream is turned off and mmap buffers released.
 * All previous streams that are now unused are untracked.
 *
 * @param requested_configuration: the streams and their attributes to configure
 *
 * @return Status::OK if all previous stream's objects are successfully cleaned
 *         Status::INTERNAL_ERROR if an internal HAL error occured during the
 *                                stream
 *
 */
Status CameraDeviceSession::configureStreamClean(
                          const StreamConfiguration& requested_configuration) {
  ALOGV("%s: enter", __FUNCTION__);

  /* Make sure the stream is off */
  int res = v4l2_wrapper_->StreamOff();

  if (res) {
    ALOGE("%s: can't turnoff the stream", __FUNCTION__);
    return Status::INTERNAL_ERROR;
  }

  /* Here we free previously allocated and mapped buffer.
   */
  if (v4l2_buffers_.size() > 0) {
    /* destructors will unmap all previously mapped buffers*/
    v4l2_buffers_.clear();

    /* now it is safe to tell V4L2 to free the buffers */
    res = v4l2_wrapper_->RequestBuffers(0, nullptr, nullptr);

    if (res) {
      ALOGE("%s: can't free V4L2 buffers", __FUNCTION__);
      return Status::INTERNAL_ERROR;
    }
  }

  /* Untrack all streams that are now unused */
  std::vector<buffer_handle_t> buffers;
  {
    Mutex::Autolock _l(capture_tracker_lock_);
    camera_tracker_.intersectStreams(requested_configuration.streams, &buffers);
  }

  for (buffer_handle_t buffer : buffers) {
    mapper_helper_.freeBuffer(buffer);
  }

  started_ = false;
  previous_settings_.resize(0);

  return Status::OK;
}

/* @brief This method configures the V4L2 driver with the provided stream.
*
 * It find the best match for the given stream and set the driver format.
 * Then needed buffers are pre allocated with the result size for a stream.
 *
 * @param stream: The streams and their attributes to configure.
 *
 * @return Status::OK if the driver is successfully configured.
 *         Status::INTERNAL_ERROR if the driver can't be configured because of
 *                                an internal HAL/driver error.
 */
Status CameraDeviceSession::configureDriverStream(
                        const StreamConfiguration& requested_configuration) {

  StreamFormat format(0, 0, 0);
  Status status = findBestFitFormat(requested_configuration, &format);

  ALOGV("%s: enter", __FUNCTION__);

  if (status != Status::OK) {
    ALOGE("%s: can't find format for device, return %d", __FUNCTION__, status);
    return status;
  }

  /* Set the driver format */
  int res = v4l2_wrapper_->SetFormat(format);

  if (res) {
    ALOGE("%s: failed to set device to correct format for stream: %d",
                                                            __FUNCTION__, res);
    return Status::INTERNAL_ERROR;
  }

  /* request some buffer to the driver and get the actual allocated buffer and
   * their size
   */
  uint32_t num_done = 0;
  uint32_t buffer_size = 0;

  res = v4l2_wrapper_->RequestBuffers(1, &num_done, &buffer_size);

  if (res || num_done == 0 || buffer_size == 0) {
    ALOGE("%s: Request buffers for new format failed: %d", __FUNCTION__, res);
    return Status::INTERNAL_ERROR;
  }

  int32_t fd = -1;

  for (size_t i = 0; i < num_done; ++i) {
    res = v4l2_wrapper_->ExportBuffer(i, &fd);

    if (res) {
      ALOGE("%s: can't get v4l2 allocated buffer: %d", __FUNCTION__, res);
      return Status::INTERNAL_ERROR;
    }

    ALOGV("%s: export buffer %d: format 0x%x (%dx%d), size: %d",
              __FUNCTION__, i, format.v4l2_pixel_format(), format.width(),
              format.height(), buffer_size);

    std::unique_ptr<arc::V4L2FrameBuffer> v4l2_buffer =
            std::make_unique<arc::V4L2FrameBuffer>(base::unique_fd(fd),
                                                   buffer_size,
                                                   format.width(),
                                                   format.height(),
                                                   format.v4l2_pixel_format());

    v4l2_buffer->Map();

    v4l2_buffers_.push_back(std::move(v4l2_buffer));
  }

  return Status::OK;
}

/*
 * @brief This method try to find a V4L2 driver format that work with the
 *        streams @p requested_configuration.
 *
 * This HAL use a V4L2 driver that only support one stream at a time. However,
 * this limit don't match the HAL requirement which must support multiple
 * streams.
 * To bypass this limitation, if multiple streams are used, the driver is
 * configured with a format that can be converted to the requested streams
 * formats.
 * This method will check if only one format is requested and if it is supported
 * by the driver directly and use it. If not, this method will search a format
 * that can be used to be converted in requested streams
 *
 * @param requested_configuration: the streams and their attributes to configure
 * @param stream_format: a pointer to a StreamFormat that will be set to a
 *                       working V4L2 driver format for the given
 *                       @p requested_configuration
 *
 * @return Status::OK if a working format has been found
 *         Status::ILLEGAL_ARGUMENT if no format can't be found to configure the
 *                                  V4L2 driver to fullfill the
 *                                  @p requested_configuration
 *
 */
Status CameraDeviceSession::findBestFitFormat(
                          const StreamConfiguration& requested_configuration,
                          StreamFormat *stream_format) {
  /* The driver only support 1 stream format at a time
   * If all the requested stream are the same format, try to use it
   * If multiple streams use different format or if the stream format is not
   * supported by the driver, use a qualified format from which
   * we can convert and check that the conversion is ok for all streams formats.
   */
  const Stream *stream = &requested_configuration.streams[0];
  uint32_t format = StreamFormat::HalToV4L2PixelFormat(stream->format,
                                                       implem_defined_format_);
  uint32_t width = stream->width;
  uint32_t height = stream->height;
  std::unordered_set<uint32_t> formats;

  ALOGV("%s: try to find best fit format.", __FUNCTION__);

  formats.insert(format);

  ALOGV("%s: framework requesting format %x = %x (%dx%d)",
           __FUNCTION__, stream->format, format, stream->width, stream->height);

  for (size_t i = 1; i < requested_configuration.streams.size(); ++i) {
    stream = &requested_configuration.streams[i];
    format = StreamFormat::HalToV4L2PixelFormat(stream->format,
                                                implem_defined_format_);

    formats.insert(format);

    ALOGV("%s: framework requesting format %x = %x (%dx%d)",
           __FUNCTION__, stream->format, format, stream->width, stream->height);

    if (height < stream->height) {
      height = stream->height;
    }
    if (width < stream->width) {
      width = stream->width;
    }
  }

  /* unique format requested, try to use it directly by the driver*/
  if (formats.size() == 1) {
    ALOGI("%s: only one stream format needed (0x%x), try to use it",
              __FUNCTION__, format);

    int index = StreamFormat::FindMatchingFormat(supported_formats_,
                                                 format, width, height);

    if (index >= 0) {
      *stream_format = supported_formats_[index];
      return Status::OK;
    }

    ALOGI("%s: the driver doesn't support the needed format (0x%x)",
              __FUNCTION__, format);
  }

  /* The driver can't use the unique format or there is more than one format.
   * Check if all requested format can be converted from YU12.
   */
  for (auto f : formats) {

    /* For now, all conversion will be done though CachedFrame which will
     * imediately convert the qualified format into YU12.
     */
    if (!ImageProcessor::SupportsConversion(V4L2_PIX_FMT_YUV420, f)) {
      ALOGE("%s: conversion between YU12 and 0x%x is not supported",
                                                              __FUNCTION__, f);
      return Status::ILLEGAL_ARGUMENT;
    }
  }

  /* All format can be converted from YU12, find a qualified format */
  ALOGV("%s: more than 1 format or unsupported format detected,"
        "search for a qualified format (%dx%d)", __FUNCTION__, width, height);

  int index = StreamFormat::FindFormatByResolution(qualified_formats_,
                                                   width, height);

  if (index >= 0) {
    *stream_format = qualified_formats_[index];
    ALOGI("%s: found qualified format 0x%x at index %d",
                      __FUNCTION__, stream_format->v4l2_pixel_format(), index);
    return Status::OK;
  }

  ALOGE("%s: no format found to fulfill framework request", __FUNCTION__);

  return Status::ILLEGAL_ARGUMENT;
}

/*
 *
 *
 */
Status CameraDeviceSession::configureStreamResult(
                          const StreamConfiguration& requested_configuration,
                          HalStreamConfiguration *configuration) {
  ALOGV("%s: enter", __FUNCTION__);

  /* Set the result configuration */
  configuration->streams.resize(requested_configuration.streams.size());
  memset(configuration->streams.data(), 0,
                            sizeof(HalStream) * configuration->streams.size());

  for (size_t i = 0; i < configuration->streams.size(); ++i) {
    HalStream *hal_stream = &configuration->streams[i];
    const Stream& stream = requested_configuration.streams[i];

    /* Save streams*/
    capture_tracker_lock_.lock();
    Stream& s = camera_tracker_.trackStream(stream);
    capture_tracker_lock_.unlock();

    hal_stream->id = stream.id;

    if (stream.streamType == StreamType::OUTPUT) {
      hal_stream->consumerUsage = 0; /* Must be 0 for output type */
      hal_stream->producerUsage |= BufferUsage::CPU_WRITE_OFTEN;
    } else {
      hal_stream->consumerUsage |= BufferUsage::CPU_READ_OFTEN;
      hal_stream->producerUsage = 0; /* Must be 0 for input type */
    }

    if (stream.format == PixelFormat::IMPLEMENTATION_DEFINED) {
      hal_stream->overrideFormat = static_cast<PixelFormat>(
                    StreamFormat::V4L2ToHalPixelFormat(implem_defined_format_));
      s.format = hal_stream->overrideFormat;
    } else {
      hal_stream->overrideFormat = stream.format;
    }

    /* Share available buffers between all streams */
    hal_stream->maxBuffers = MAX_BUFFER_NUMBER;

    if (hal_stream->maxBuffers == 0) {
      ALOGW("%s: not enough buffer for all streams, starving may happen...",
                __FUNCTION__);
      hal_stream->maxBuffers = 1;
    }

    s.usage |= (hal_stream->consumerUsage | hal_stream->producerUsage);

    ALOGV("%s: stream %d configured: type: %d, format: 0x%x, w: %d, h: %d"
          ", rotation: %d, usage: 0x%" PRIx64 ", consumerUsage: 0x%" PRIx64
          ", producerUsage: 0x%" PRIx64 ", maxBuffers: %d",
          __FUNCTION__, hal_stream->id, stream.streamType,
          hal_stream->overrideFormat, stream.width, stream.height,
          stream.rotation, stream.usage, hal_stream->consumerUsage,
          hal_stream->producerUsage, hal_stream->maxBuffers);
  }

  return Status::OK;
}

/********** Process Capture Section **********/

/*
 * @brief This method removes all saved buffer matching the ones in the given
 *        list.
 *
 * @param chache_to_remove: the list of previously saved buffer that must now be
 *                          freed.
 *
 */
void CameraDeviceSession::updateBufferCaches(
        const hidl_vec<BufferCache>& caches_to_remove) {
  Mutex::Autolock _l(capture_tracker_lock_);

  ALOGV("%s: enter", __FUNCTION__);

  for (const BufferCache& cache : caches_to_remove) {
    buffer_handle_t buffer;

    if (camera_tracker_.untrackBuffer(cache.streamId,
                                      cache.bufferId, &buffer) < 0) {
      ALOGE("%s: stream %d, buffer %" PRIu64 " is not tracked",
                                  __FUNCTION__, cache.streamId, cache.bufferId);
      continue;
    }

    mapper_helper_.freeBuffer(buffer);
  }
}

/*
 * @brief This method processes one by one all the capture requests given in
 *        parameter. It also clean all old buffers that are not used anymore.
 *
 * @params requests: the capture requests array that must be processed
 * @params caches_to_remove: the list of previously saved buffer that must now
 *                           be freed.
 *
 * @return Status::OK if successfull
 *         Status::INTERNAL_ERROR when unexpected critical error occured
 *         Status::ILLEGAL_ARGUMENT when unexpected error occured because of an
 *                                  invalid argument.
 */
Return<void> CameraDeviceSession::processCaptureRequest(
        const hidl_vec<CaptureRequest>& requests,
        const hidl_vec<BufferCache>& caches_to_remove,
        processCaptureRequest_cb _hidl_cb) {
  updateBufferCaches(caches_to_remove);

  Status res = Status::OK;
  Status s = Status::OK;
  uint32_t num_request_processed = 0;

  ALOGV("%s: enter: nb requests: %d", __FUNCTION__, requests.size());

  for (size_t i = 0; i < requests.size(); ++i) {
    s = processOneCaptureRequest(requests[i]);
    if (s == Status::OK) {
      ++num_request_processed;
    } else {
      res = s;
    }
  }

  _hidl_cb(res, num_request_processed);
  return Void();
}

/*
 * @brief This method takes a single capture request and process it to obtain
 *        an image result.
 *
 * This method will call successively internal methods to process a single
 * capture request and:
 * - verify the request can be captured
 * - process the capture metadata
 * - import the needed data usefull for the capture
 * - enqueue the request to the capture queueu
 *
 * @param request: the capture request that must be processed
 *
 * @return Status::OK if successfull
 *         Status::INTERNAL_ERROR when unexpected critical error occured
 *         Status::ILLEGAL_ARGUMENT when unexpected error occured because of an
 *         invalid argument.
 */
Status CameraDeviceSession::processOneCaptureRequest(
        const CaptureRequest& request) {
  ALOGV("%s: enter: frame: %d, nb output buffers: %d",
              __FUNCTION__, request.frameNumber, request.outputBuffers.size());
  Mutex::Autolock _l(flush_queue_lock_);

  Status status = processCaptureRequestVerification(request);

  if (status != Status::OK) {
    return status;
  }

  if ((status = processCaptureRequestImport(request)) != Status::OK) {
    return status;
  }

  /* Validate and set v4l2 metadata for this request.
   * Save the capture metadata for the capture result.
   */
  std::shared_ptr<helper::CameraMetadata> settings;
  if ((status = processCaptureRequestMetadata(request,
                                              settings)) != Status::OK) {
    return status;
  }

  if ((status = processCaptureRequestEnqueue(request,
                                             settings)) != Status::OK) {
    return status;
  }

  /* TODO: handle inputBuffer ? */

  return status;
}

/* Handle errors happening before capturing any buffer for a request.
 * There is no RequestContext in the tracker
 *
 */
Status CameraDeviceSession::processCaptureRequestError(
        const CaptureRequest& request, ErrorCode error) {

  /* First, send the notification to the client */
  ALOGV("%s: sending error msg for frame %u, error: %d",
                                  __FUNCTION__, request.frameNumber, error);
  NotifyMsg error_msg = {
      MsgType::ERROR,
      {
        .error = {
          request.frameNumber,
          -1, /* streamId is not applicable here */
          error
        }
      }
  };

  callback_->notify({ error_msg });

  switch (error) {
    case ErrorCode::ERROR_DEVICE:
      /* Serious failure occured:
       * drop the request and close the HAL
       */
      {
        Mutex::Autolock _l(capture_tracker_lock_);
        camera_tracker_.clearCaptures();
      }
      close();
      break;
    case ErrorCode::ERROR_REQUEST:
      processCaptureRequestError(request);
      break;
    case ErrorCode::ERROR_RESULT:
      /* nothing special to do, metadata is invalid but buffers remain valid */
      break;
    case ErrorCode::ERROR_BUFFER:
      /* This error should never occur for a request.
       * It is only valid while processing a buffer
       */
      ALOGE("%s: ERROR_BUFFER can't occur for a request but a buffer",
                                                                  __FUNCTION__);
      break;
  }

  return Status::OK;
}

/* @brief This method handles the ERROR_REQUEST error code of a capture request.
 *
 * When an ERROR_REQUEST occur for a capture request, all the stream buffer must
 * be set in the error state and returned to the client with the provided
 * callback
 *
 * @param request: The capture request that have failed with the ERROR_REQUEST
 *                 error code
 *
 */
void CameraDeviceSession::processCaptureRequestError(
                                                const CaptureRequest& request) {
  CaptureResult result;

  result.frameNumber = request.frameNumber;
  result.partialResult = 1;

  result.inputBuffer.buffer = nullptr;
  result.inputBuffer.acquireFence = nullptr;

  if (request.inputBuffer.streamId != -1) {
    result.inputBuffer.streamId = request.inputBuffer.streamId;
    result.inputBuffer.bufferId = request.inputBuffer.bufferId;
    result.inputBuffer.status = BufferStatus::ERROR;
    /* When error, release fence must be set to acquire fence or -1 if already
     * waited on acquire fence
     * On a request error, the acquire fence was not yet waited on.
     */
    result.inputBuffer.releaseFence = request.inputBuffer.acquireFence;
  } else {
    result.inputBuffer.streamId = -1;
    result.inputBuffer.bufferId = 0;
  }

  result.outputBuffers.resize(request.outputBuffers.size());
  for (size_t i = 0; i < request.outputBuffers.size(); ++i) {
    const StreamBuffer& stream_buffer = request.outputBuffers[i];
    result.outputBuffers[i].streamId = stream_buffer.streamId;
    result.outputBuffers[i].bufferId = stream_buffer.bufferId;
    result.outputBuffers[i].buffer = nullptr;
    result.outputBuffers[i].status = BufferStatus::ERROR;
    result.outputBuffers[i].acquireFence = nullptr;
    result.outputBuffers[i].releaseFence = stream_buffer.acquireFence;
  }

  std::vector<CaptureResult> res_vector;
  res_vector.push_back(std::move(result));

  hidl_vec<CaptureResult> results(res_vector);
  callback_->processCaptureResult(results);
}


/* Handle errors happening while capturing a buffer for a request
 * There is a RequestContext tracked by the capture tracker.
 */
Status CameraDeviceSession::processCaptureBufferError(uint32_t capture_id,
                                              StreamBuffer stream_buffer,
                                              ErrorCode error) {

  /* First, send the notification to the application */
  ALOGV("%s: sending error msg for frame %u, stream %d, error: %d",
                __FUNCTION__, capture_id, stream_buffer.streamId, error);
  NotifyMsg error_msg = {
      MsgType::ERROR,
      {
        .error = {
          capture_id,
          static_cast<int32_t>(stream_buffer.streamId),
          error
        }
      }
  };

  callback_->notify({ error_msg });

  switch (error) {
    case ErrorCode::ERROR_DEVICE:
      /* Serious failure occured:
       * drop the request and close the HAL
       */
      {
        Mutex::Autolock _l(capture_tracker_lock_);
        camera_tracker_.clearCaptures();
      }
      close();
      break;
    case ErrorCode::ERROR_REQUEST:
      /* This error should never occur in the buffer processing part of the HAL.
       * Error happening while processing a buffer should be returnd as
       * ERROR_BUFFER.
       * This error is only valid during the request processing.
       */
      ALOGE("%s: ERROR_REQUEST can't occur during the buffer processing",
                                                                  __FUNCTION__);
      break;
    case ErrorCode::ERROR_RESULT:
      /* nothing special to do, metadata is invalid but buffers remain valid */
      break;
    case ErrorCode::ERROR_BUFFER:
      /* This buffer capture is KO but other buffers remain valid */
      stream_buffer.status = BufferStatus::ERROR;
      stream_buffer.releaseFence = std::move(stream_buffer.acquireFence);
      stream_buffer.acquireFence = nullptr;

      /* Call processCaptureResult to send callback to the application if the
       * request is now completed
       */
      processCaptureResult(capture_id, stream_buffer);
      break;
  }

  return Status::OK;
}

/*
 * This function checks if the given capture request is valid.
 *
 * @param request: the capture request that must be verified.
 *
 * @return Status::OK if the request is valid
 *         Status::INTERNAL_ERROR when unexpected critical error occured
 *         Status::ILLEGAL_ARGUMENT when unexpected error occured because of an
 *         invalid argument.
 */
Status CameraDeviceSession::processCaptureRequestVerification(
                                                const CaptureRequest& request) {
  Status status = initStatus();

  ALOGV("%s: enter", __FUNCTION__);

  if (status == Status::INTERNAL_ERROR) {
    ALOGE("%s: camera init failed", __FUNCTION__);
    processCaptureRequestError(request, ErrorCode::ERROR_DEVICE);
    return status;
  } else if (status == Status::CAMERA_DISCONNECTED) {
    ALOGE("%s: camera is disconnected", __FUNCTION__);
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return status;
  }

  /* If it is the first CaptureRequest, the settings field must not be empty */
  if (request.fmqSettingsSize == 0 &&
              previous_settings_.size() == 0 && request.settings.size() == 0) {
    ALOGE("%s: capture request settings must not be empty for first request!",
              __FUNCTION__);
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return Status::ILLEGAL_ARGUMENT;
  }

  if (request.outputBuffers.size() < 1) {
    ALOGE("%s: invalid number of output buffers: %d < 1",
              __FUNCTION__, request.outputBuffers.size());
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return Status::ILLEGAL_ARGUMENT;
  }

  if (request.inputBuffer.buffer != nullptr) {
    ALOGV("%s: requesting capture reprocessing", __FUNCTION__);
  } else {
    ALOGV("%s: requesting new capture", __FUNCTION__);
  }

  return status;
}

/*
 * @brief This method handle the metadata associated to a capture request
 *
 * @return Status::OK if successfull
 *         Status::INTERNAL_ERROR when unexpected critical error occured
 *         Status::ILLEGAL_ARGUMENT when unexpected error occured because of an
 *         invalid argument.
 */
Status CameraDeviceSession::processCaptureRequestMetadata(
                      const CaptureRequest& request,
                      std::shared_ptr<helper::CameraMetadata>& metadata) {
  CameraMetadata settings;
  Status status = Status::OK;

  ALOGV("%s: enter", __FUNCTION__);

  /* Capture Settings */
  if (request.fmqSettingsSize > 0) {
    settings.resize(request.fmqSettingsSize);

    if (!request_metadata_queue_->read(settings.data(), request.fmqSettingsSize)) {
      ALOGE("%s: capture request settings metadata couldn't be read from fmq",
                                                                  __FUNCTION__);
      processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
      return Status::ILLEGAL_ARGUMENT;
    }
  } else if (request.settings.size() > 0) {
    settings = request.settings;
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
    ALOGE("%s: invalid request settings", __FUNCTION__);
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return Status::ILLEGAL_ARGUMENT;
  }


  /* Setting and getting settings are best effort here,
   * since there's no way to know through V4L2 exactly what
   * settings are used for a buffer unless we were to enqueue them
   * one at a time, which would be too slow.
   */
  int res = metadata_->SetRequestSettings(*metadata);

  if (res) {
    ALOGE("%s: failed to set settings: %d", __FUNCTION__, res);
    processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
    return Status::INTERNAL_ERROR;
  }

  res = metadata_->FillResultMetadata(metadata.get());

  if (res) {
    ALOGE("%s: failed to fill result metadata", __FUNCTION__);
    /* Notify the metadata won't be available for this capture and continue */
    processCaptureRequestError(request, ErrorCode::ERROR_RESULT);
  }

  return status;
}

/*
 * This function handles the importation part of the capture request.
 * For all output buffers that must be processed, if the result buffer was never
 * been met, it is saved for any futur use by the client.
 *
 * @param request: the capture request that must be imported
 *
 * @return Status::OK if successfull
 *         Status::INTERNAL_ERROR when unexpected critical error occured
 *         Status::ILLEGAL_ARGUMENT when unexpected error occured because of an
 *         invalid argument.
 */
Status CameraDeviceSession::processCaptureRequestImport(
                                                const CaptureRequest& request) {
  Status s = Status::OK;

  ALOGV("%s: enter", __FUNCTION__);

  /* Import output buffers if needed */
  for (size_t i = 0; i < request.outputBuffers.size(); ++i) {
    const StreamBuffer& stream_buffer = request.outputBuffers[i];
    const buffer_handle_t& buffer = getSavedBuffer(stream_buffer);

    /* save the buffer if it is a new one */
    if (buffer == nullptr) {
      if ((s = saveBuffer(stream_buffer)) != Status::OK) {
        processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
        return s;
      }
    }
  }

  /* Import input buffer if needed */
  /* If no input buffer, stop here */
  if (request.inputBuffer.streamId == -1 && request.inputBuffer.bufferId == 0) {
    return s;
  }

  const buffer_handle_t& buffer = getSavedBuffer(request.inputBuffer);

  if (buffer == nullptr) {
    if ((s = saveBuffer(request.inputBuffer)) != Status::OK) {
      processCaptureRequestError(request, ErrorCode::ERROR_REQUEST);
      return s;
    }
  }

  return s;
}

/*
 * This function handles the queuing part of the capture request into the V4L2
 * driver queue.
 *
 * @param context: the capture request context associated to the capture
 *
 * @return Status::OK if successfull
 *         Status::INTERNAL_ERROR when unexpected critical error occured
 *         Status::ILLEGAL_ARGUMENT when unexpected error occured because of an
 *         invalid argument.
 */
Status CameraDeviceSession::processCaptureRequestEnqueue(
                const CaptureRequest& request,
                const std::shared_ptr<const helper::CameraMetadata>& settings) {
  /* Make sure the stream is on. */
  if (!started_) {
    v4l2_wrapper_->StreamOn();

    /* enqueue all available buffers to get ready for futur requests */
    for (size_t i = 0; i < v4l2_buffers_.size(); ++i) {
      v4l2_wrapper_->EnqueueRequest(i);
    }

    started_ = true;
  }

  /* notify the dequeuer that a capture result will comming soon */
  int id = 0;

  {
    Mutex::Autolock _l(capture_tracker_lock_);
    id = camera_tracker_.trackCapture(request);
  }

  if (id < 0) {
    ALOGE("%s: error: can't track the request", __FUNCTION__);
    processCaptureRequestError(request, ErrorCode::ERROR_DEVICE);
    return Status::INTERNAL_ERROR;
  }

  {
    Mutex::Autolock _l(capture_tracker_lock_);
    camera_tracker_.setCaptureSettings(id, settings);
  }

  {
    Mutex::Autolock _l(capture_wait_lock_);
    capture_active_.signal();
  }

  return Status::OK;
}

/* @brief The thread waits for an available v4l2 buffer when a stream buffer
 *        must be filled for a capture request.
 *
 * When a v4l2 buffer is available, it copies the data into the next stream
 * buffer to be completed and check the capture request status.
 * When all buffers of the capture request are filled, it sends the capture
 * result to the client.
 *
 */
void CameraDeviceSession::captureRequestThread() {
  ALOGI("%s: Capture Result Thread started", __FUNCTION__);

  capture_wait_lock_.lock();

  while (1) {
    if (isClosed()) {
      break;
    }

    int res = 0;
    {
      Mutex::Autolock _l(capture_tracker_lock_);
      res = camera_tracker_.hasActiveCapture();
    }

    if (!res) {
      /* wait for client buffer to complete */
      capture_active_.wait(capture_wait_lock_);
      continue;
    }


    uint32_t index = 0;
    res = v4l2_wrapper_->DequeueRequest(&index);

    if (res == -EAGAIN) {
      /* No v4l2 buffer available yet, continue */
      continue;
    }

    /* We got a result, process it to complete next client buffer */
    capture_wait_lock_.unlock();

    /* We don't want a flush occur during the processing of a result */
    Mutex::Autolock _l(flush_result_lock_);
    Status status = Status::OK;
    int capture_id = 0;
    capture_tracker_lock_.lock();
    StreamBuffer stream_buffer =
                              camera_tracker_.popNextOutputBuffer(&capture_id);
    capture_tracker_lock_.unlock();

    /* Check there is still tracked buffers to complete */
    if (capture_id < 0) {
      /* No buffer available for completion.
       * Should not happen unless a flush occured.
       */
      ALOGE("%s: there is no output buffer to complete ! "
            "Maybe a flush occured ?", __FUNCTION__);
      status = Status::INTERNAL_ERROR;
      capture_wait_lock_.lock();
      continue;
    }

    /* Check the v4l2 driver returned a valid buffer */
    if (res < 0) {
      ALOGE("%s: V4L2 wrapper failed to dequeue buffer", __FUNCTION__);
      status = Status::INTERNAL_ERROR;
    } else {
      /* Convert and copy the buffer into the client buffer */
      std::shared_ptr<const helper::CameraMetadata> settings =
                                camera_tracker_.getCaptureSettings(capture_id,
                                                                   nullptr);
      std::unique_ptr<arc::V4L2FrameBuffer>& v4l2_buffer =
                                                          v4l2_buffers_[index];
      v4l2_buffer->SetDataSize(res);
      status = processCaptureResultConversion(v4l2_buffer, stream_buffer,
                                              settings);

      /* Buffer copied, put it back in the v4l2 driver */
      if (v4l2_wrapper_->EnqueueRequest(index)) {
        ALOGW("%s: can't requeue a buffer in the driver", __FUNCTION__);
      }
    }

    /* Finish the stream buffer processing with error or a valid result */
    if (status != Status::OK) {
      processCaptureBufferError(capture_id,
                                std::move(stream_buffer),
                                ErrorCode::ERROR_BUFFER);
    } else {
      stream_buffer.status = BufferStatus::OK;
      processCaptureResult(capture_id, std::move(stream_buffer));
    }

    capture_wait_lock_.lock();
  }

  capture_wait_lock_.unlock();

  ALOGI("%s: Capture Result Thread ended", __FUNCTION__);
}

/* @brief This method convert if needed and copy a v4l2 buffer into the
 *        requested stream buffer.
 *
 * @param v4l2_buffer: The v4l2 buffer from which the copy and (conversion) must
 *                     be made.
 * @param stream_buffer: The client stream buffer which must be filled with the
 *                       v4l2 buffer.
 * @param settings: the camera metadata settings used for the buffer capture.
 *
 * @return Status::OK if the conversion and copy of the v4l2 buffer into the
 *                    stream buffer was successfull
 *         Status::INTERNAL_ERROR if an unexpected error occured
 *
 */
Status CameraDeviceSession::processCaptureResultConversion(
                  const std::unique_ptr<arc::V4L2FrameBuffer>& v4l2_buffer,
                  StreamBuffer& stream_buffer,
                  std::shared_ptr<const helper::CameraMetadata>& settings) {

  ALOGV("%s: enter", __FUNCTION__);

  capture_tracker_lock_.lock();
  const Stream& stream = camera_tracker_.getStream(stream_buffer.streamId);
  capture_tracker_lock_.unlock();

  if (stream.id == -1) {
    ALOGE("%s: no attached stream found for the capture request", __FUNCTION__);
    return Status::INTERNAL_ERROR;
  }

  uint32_t fourcc = StreamFormat::HalToV4L2PixelFormat(stream.format,
                                                       implem_defined_format_);
  buffer_handle_t buffer = getSavedBuffer(stream_buffer);

  ALOGV("%s: got buffer format: 0x%x (%dx%d), need format 0x%x (%dx%d)",
          __FUNCTION__, v4l2_buffer->GetFourcc(), v4l2_buffer->GetWidth(),
          v4l2_buffer->GetHeight(), fourcc, stream.width, stream.height);

  /* Note that the device buffer length is passed to the output frame. If the
   * GrallocFrameBuffer does not have support for the transformation to
   * |fourcc|, it will assume that the amount of data to lock is based on
   * |v4l2_buffer buffer_size|, otherwise it will use the ImageProcessor::ConvertedSize.
   */
  arc::GrallocFrameBuffer output_frame(buffer, stream.width, stream.height,
                                       fourcc, v4l2_buffer->GetDataSize(),
                                       stream.usage & (
                                          BufferUsage::CPU_READ_MASK |
                                          BufferUsage::CPU_WRITE_MASK),
                                       &mapper_helper_);

  Status status = Status::OK;
  int res = output_frame.Map(stream_buffer.acquireFence);

  stream_buffer.acquireFence = nullptr;

  if (res) {
    ALOGE("%s: failed to map output frame.", __FUNCTION__);
    return Status::INTERNAL_ERROR;
  }

  if (v4l2_buffer->GetFourcc() == fourcc &&
      v4l2_buffer->GetWidth() == stream.width &&
      v4l2_buffer->GetHeight() == stream.height) {
    // If no format conversion needs to be applied, directly copy the data over.
    memcpy(output_frame.GetData(), v4l2_buffer->GetData(),
                                                    v4l2_buffer->GetDataSize());
  } else {
    arc::CachedFrame cached_frame;
    cached_frame.SetSource(v4l2_buffer.get(), 0);
    res = cached_frame.Convert(*settings, &output_frame);

    if (res) {
      ALOGE("%s: conversion failed", __FUNCTION__);
      status = Status::INTERNAL_ERROR;
    }
  }

  res = output_frame.Unmap(&stream_buffer.releaseFence);

  return status;
}

/*
 * @brief This method is called to save the processing of a stream buffer and
 *        complete the result of a capture request.
 *        The result will be send to the client when the capture request will be
 *        fully completed.
 *
 * @param capture_id: the frame number of the capture request being completed.
 * @param stream_buffer: the stream buffer which has been filled and must be saved
 *
 * @return Status::OK
 *
 */
Status CameraDeviceSession::processCaptureResult(uint32_t capture_id,
                                                 StreamBuffer stream_buffer) {
  ALOGV("%s: enter", __FUNCTION__);

  Status status = Status::OK;
  int res = 0;

  {
    Mutex::Autolock _l(capture_tracker_lock_);
    res = camera_tracker_.saveOutputBuffer(std::move(stream_buffer),
                                           capture_id);

    if (res >= 0) {
      res = camera_tracker_.isCaptureCompleted(capture_id);
    }
  }

  if (res < 0) {
    ALOGE("%s: there is no capture id %u to complete",
                                                      __FUNCTION__, capture_id);
    status = Status::INTERNAL_ERROR;
  }
  else if (res > 0) {
    /* The request is fully completed, send all available buffers and untrack
     * the request
     */
    /* Get capture results (output buffers / input buffer / metadata) */
    uint32_t partial_count = 0;
    capture_tracker_lock_.lock();
    std::shared_ptr<const helper::CameraMetadata> settings =
                            camera_tracker_.getCaptureSettings(capture_id,
                                                               &partial_count);

    StreamBuffer input_result = camera_tracker_.popInputResult(capture_id);
    std::vector<StreamBuffer> output_results =
                                 camera_tracker_.popOutputResults(capture_id);
    capture_tracker_lock_.unlock();

    CaptureResult result = {
      capture_id,                 /* frameNumber*/
      0,                          /* fmqSettingsSize */
      CameraMetadata { },         /* result */
      hidl_vec<StreamBuffer> { }, /* outputBuffers */
      std::move(input_result),    /* inputBuffer */
      partial_count               /* partialCount */
    };

    /* Set the output buffers of the result with the saved one */
    result.outputBuffers.setToExternal(output_results.data(),
                                       output_results.size());

    /* Prepare the metadata associated to the capture to send to the client */
    processCaptureResultMetadata(*settings, &result);

    /* Get the current timestamp and notify the client of a new capture result */
    int64_t timestamp = 0;
    MetadataCommon::SingleTagValue(*settings,
                                   ANDROID_SENSOR_TIMESTAMP,
                                   &timestamp);

    /* Now that the request is completed, untrack it */
    {
      Mutex::Autolock _l(capture_tracker_lock_);
      camera_tracker_.untrackCapture(capture_id);
    }

    ALOGV("%s: sending shutter msg for frame %d, timestamp: %" PRIi64,
                                __FUNCTION__, result.frameNumber, timestamp);
    NotifyMsg shutter_msg = {
      MsgType::SHUTTER, {
        .shutter = {
          result.frameNumber, static_cast<uint64_t>(timestamp)
        }
      }
    };
    callback_->notify({ shutter_msg });

    /* here, we don't use the group result capability and we send directly the
     * result to the client.
     * we still need to put the result in a single entry vector to send it.
     */
    std::vector<CaptureResult> res_vector;
    res_vector.push_back(std::move(result));

    hidl_vec<CaptureResult> results;
    results.setToExternal(res_vector.data(), res_vector.size());
    callback_->processCaptureResult(results);
  } else {
    /* The request is not fully completed.
     * We could send available buffers / metadata already but just wait
     * eveything is finished for now.
     */
  }

  return status;
}

/*
 * @brief This method handle a CaptureResult's metadatas to send them to the
 *        client
 *
 * @param settings: The camera metadata settings used for the capture request
 * @param result: The capture result that must be completed with the camera
 *                metadata settings
 *
 * @return Status::OK if metadata are successfully processed
 *         Status::ERROR_RESULT if a metadata error occured but buffers are
 *                              still valids
 */
Status CameraDeviceSession::processCaptureResultMetadata(
                                      const helper::CameraMetadata& settings,
                                      CaptureResult *result) {
  const camera_metadata_t *metadata = settings.getAndLock();
  uint32_t size = get_camera_metadata_size(metadata);
  uint8_t *data = const_cast<uint8_t *>(
                                  reinterpret_cast<const uint8_t *>(metadata));
  bool res = result_metadata_queue_->write(data, size);

  if (!res) {
    ALOGE("%s: failed to write metadata to FMQ", __FUNCTION__);
    result->fmqResultSize = 0;
    result->result.setToExternal(data, size);
  } else {
    result->fmqResultSize = size;
  }

  return Status::OK;
}

Return<void> CameraDeviceSession::getCaptureRequestMetadataQueue(
        getCaptureRequestMetadataQueue_cb _hidl_cb) {
  ALOGV("%s: enter", __FUNCTION__);

  if (!request_metadata_queue_) {
    ALOGE("%s: request metadata queue is not set!", __FUNCTION__);
    return Void();
  }

  _hidl_cb(*request_metadata_queue_->getDesc());

  return Void();
}

Return<void> CameraDeviceSession::getCaptureResultMetadataQueue(
                getCaptureResultMetadataQueue_cb _hidl_cb) {
  ALOGV("%s: enter", __FUNCTION__);

  _hidl_cb(*result_metadata_queue_->getDesc());

  return Void();
}

Return<Status> CameraDeviceSession::flush() {
  Status status = initStatus();
  /* The flush and processCaptureRequest methods can be called concurrently
   * which is not handled by this HAL.
   * This lock ensures that the flush and the processCaptureRequest methods will
   * not doing any concurrently jobs.
   */
  Mutex::Autolock _l(flush_queue_lock_);
  Mutex::Autolock _l2(flush_result_lock_);

  ALOGV("%s: enter", __FUNCTION__);

  if (status == Status::OK) {
    capture_tracker_lock_.lock();
    while (camera_tracker_.hasActiveCapture()) {
      int capture_id = 0;
      StreamBuffer stream_buffer =
                            camera_tracker_.popNextOutputBuffer(&capture_id);
      capture_tracker_lock_.unlock();
      if (capture_id >= 0) {
        processCaptureBufferError(capture_id,
                                  stream_buffer, ErrorCode::ERROR_BUFFER);
      }
      capture_tracker_lock_.lock();
    }
    capture_tracker_lock_.unlock();

    v4l2_wrapper_->StreamOff();
    started_ = false;
  }

  return status;
}

Return<void> CameraDeviceSession::close() {
  ALOGV("%s: enter", __FUNCTION__);

  if (!isClosed()) {
    flush();

    /* Free all allocated v4l2 buffers.
     * This will also unmap buffers
     */
    v4l2_buffers_.clear();

    /* Disconnect from V4L2 device.
     * This will close the device if no more connection are active
     */
    connection_.reset();
    {
      Mutex::Autolock _l(state_lock_);
      disconnected_ = true;
    }

    /* Free imported streams and buffers */
    std::vector<buffer_handle_t> buffers;
    {
      Mutex::Autolock _l(capture_tracker_lock_);
      camera_tracker_.clearBuffers(&buffers);
    }

    for (buffer_handle_t& buffer : buffers) {
      mapper_helper_.freeBuffer(buffer);
    }

    {
      Mutex::Autolock _l(capture_tracker_lock_);
      camera_tracker_.clearStreams();
    }

    Mutex::Autolock _l(state_lock_);
    closed_ = true;
  }

  {
    Mutex::Autolock _l(capture_wait_lock_);
    capture_active_.signal();
  }

  capture_result_thread_->join();

  return Void();
}

bool CameraDeviceSession::isClosed() const {
  Mutex::Autolock _l(state_lock_);
  return closed_;
}

void CameraDeviceSession::dumpState(const native_handle_t* fd) const {
  // TODO implement
  (void)fd;
}

buffer_handle_t CameraDeviceSession::getSavedBuffer(
                                            const StreamBuffer& stream_buffer) {
  Mutex::Autolock _l(capture_tracker_lock_);

  return camera_tracker_.getBuffer(stream_buffer.streamId,
                                   stream_buffer.bufferId);
}

Status CameraDeviceSession::saveBuffer(const StreamBuffer& stream_buffer) {
  buffer_handle_t buffer = stream_buffer.buffer;

  if (buffer == nullptr) {
    ALOGE("%s: bufferId %" PRIu64 " has null buffer handle!",
              __FUNCTION__, stream_buffer.bufferId);
    return Status::ILLEGAL_ARGUMENT;
  }

  bool res = mapper_helper_.importBuffer(buffer);

  if (!res || buffer == nullptr) {
    ALOGE("%s: output buffer %" PRIu64 " is invalid",
                                        __FUNCTION__, stream_buffer.bufferId);
    return Status::INTERNAL_ERROR;
  }

  Mutex::Autolock _l(capture_tracker_lock_);

  if (camera_tracker_.trackBuffer(stream_buffer.streamId,
                                  stream_buffer.bufferId,  buffer) < 0) {
    ALOGE("%s: can't track buffer %" PRIu64 " for stream %d",
                __FUNCTION__, stream_buffer.bufferId, stream_buffer.streamId);
    return Status::INTERNAL_ERROR;
  }

  return Status::OK;
}

}  // namespace implementation
}  // namespace V3_2
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
