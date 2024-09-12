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

// #define LOG_NDEBUG 0

#include "v4l2_stream.h"

#include <log/log.h>

#include <linux/v4l2-subdev.h>

#include <inttypes.h>

#include <arc/image_processor.h>
#include <arc/cached_frame.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using aidl::android::hardware::common::NativeHandle;
using aidl::android::hardware::graphics::common::BufferUsage;

using ::android::hardware::camera::common::V1_0::arc::ImageProcessor;

static const std::map<uint32_t, uint32_t> v4l2_to_bus = {
  { V4L2_PIX_FMT_RGB24, MEDIA_BUS_FMT_RGB888_1X24 },
  { V4L2_PIX_FMT_RGB565, MEDIA_BUS_FMT_RGB565_2X8_LE },
  { V4L2_PIX_FMT_ARGB32, MEDIA_BUS_FMT_ARGB8888_1X32 },
  { V4L2_PIX_FMT_YUYV, MEDIA_BUS_FMT_YUYV8_2X8 },
  { V4L2_PIX_FMT_VYUY, MEDIA_BUS_FMT_VYUY8_2X8 },
  { V4L2_PIX_FMT_UYVY, MEDIA_BUS_FMT_UYVY8_2X8 },
  { V4L2_PIX_FMT_YVYU, MEDIA_BUS_FMT_YVYU8_2X8 },
  { V4L2_PIX_FMT_NV12, MEDIA_BUS_FMT_YUYV8_1_5X8 },
  { V4L2_PIX_FMT_NV21, MEDIA_BUS_FMT_YVYU8_1_5X8 },
  { V4L2_PIX_FMT_NV16, MEDIA_BUS_FMT_YUYV8_1X16 },
  { V4L2_PIX_FMT_NV61, MEDIA_BUS_FMT_YVYU8_1X16 },
  { V4L2_PIX_FMT_YUV420, MEDIA_BUS_FMT_UYVY8_1_5X8 },
  { V4L2_PIX_FMT_YVU420, MEDIA_BUS_FMT_VYUY8_1_5X8 },
};

static bool IsAidlNativeHandleNull(const NativeHandle &handle) {
  return (handle.fds.size() == 0 && handle.ints.size() == 0);
}

static native_handle_t *makeFromAidlIfNotNull(const NativeHandle &handle) {
  if (IsAidlNativeHandleNull(handle)) {
    return nullptr;
  }
  return makeFromAidl(handle);
}
static native_handle_t *dupFromAidlIfNotNull(const NativeHandle &handle) {
  if (IsAidlNativeHandleNull(handle)) {
    return nullptr;
  }
  return dupFromAidl(handle);
}

MapperHelper V4l2Stream::mapper_helper_;

std::shared_ptr<V4l2Stream> V4l2Stream::Create(const V4l2StreamConfig& config,
                                               const Stream &stream,
                                               CallbackInterface *cb) {
  std::shared_ptr<V4l2Stream> res = std::make_shared<V4l2Stream>(config,
                                                                 stream, cb);
  if (res == nullptr) {
    ALOGE("%s (%s): Cannot Create V4l2Stream !", __func__, config.node);
    return nullptr;
  }

  Status status = res->initialize();
  if (status != Status::OK) {
    ALOGE("%s (%s): Initializing V4l2Stream failed !", __func__, config.node);
    return nullptr;
  }

  return res;
}

V4l2Stream::V4l2Stream(const V4l2StreamConfig& config, const Stream &stream,
                       CallbackInterface *cb)
  : config_(config),
    stream_(stream),
    cb_(cb),
    v4l2_wrapper_(new V4L2Wrapper(config.node)),
    connection_(nullptr),
    capture_active_(false),
    started_(false)
{ }

V4l2Stream::~V4l2Stream()
{
  flush();

  /* Free all allocated v4l2 buffers.
   * This will also unmap buffers
   */
  v4l2_buffers_.clear();

  /* Free imported framework buffers */
  {
    std::lock_guard lock(buffer_mutex_);
    for (const auto &p : buffer_map_)
      mapper_helper_.freeBuffer(p.second);

    buffer_map_.clear();
  }

  /* Wake up the result thread */
  {
    std::lock_guard lock(capture_mutex_);
    capture_active_ = false;
  }
  capture_cond_.notify_all();
  if (capture_result_thread_)
    capture_result_thread_->join();
}

Status V4l2Stream::initialize() {
  /* New connection to the V4L2 camera */
  connection_.reset(new V4L2Wrapper::Connection(v4l2_wrapper_));
  if (connection_->status()) {
    ALOGE("%s (%s): v4l2 connection failure: %d !",
              __func__, config_.node, connection_->status());
    return Status::CAMERA_DISCONNECTED;
  }

  /* Retrieve all supported formats */
  std::set<uint32_t> v4l2_pixel_format;
  if (v4l2_wrapper_->GetFormats(&v4l2_pixel_format)) {
    ALOGE("%s (%s): failed to get formats !", __func__, config_.node);
    return Status::INTERNAL_ERROR;
  }

  if (v4l2_wrapper_->GetSupportedFormats(v4l2_pixel_format,
                                         &supported_formats_)) {
    ALOGE("%s (%s): failed to get supported formats !", __func__, config_.node);
    return Status::INTERNAL_ERROR;
  }

  std::vector<uint32_t> v4l2_qualified_format;
  if (ImageProcessor::GetQualifiedFormats(v4l2_pixel_format,
                                          &v4l2_qualified_format)) {
    ALOGE("%s (%s): can't get qualified formats !", __func__, config_.node);
    return Status::INTERNAL_ERROR;
  }

  qualified_formats_ = StreamFormat::GetQualifiedStreams(v4l2_qualified_format,
                                                         supported_formats_);


  Status status = configureDriver();
  if (status != Status::OK)
    return status;

  /* Launch the capture thread */
  capture_active_ = true;
  capture_result_thread_.reset(
      new std::thread(&V4l2Stream::captureRequestThread, this));

  return Status::OK;
}

Status V4l2Stream::configureDriver() {
  StreamFormat format(0, 0, 0);

  Status status = findBestFitFormat(stream_, &format);
  if (status != Status::OK)
    return status;

  /* Set the driver format */
  status = configurePipeline(format);
  if (status != Status::OK)
    return status;

  /* request some buffer to the driver and get the actual allocated buffer and
   * their size
   */
  uint32_t num_done = 0;
  uint32_t buffer_size = 0;

  int res = v4l2_wrapper_->RequestBuffers(config_.num_buffers,
                                          &num_done, &buffer_size);
  if (res || num_done == 0 || buffer_size == 0) {
    ALOGE("%s (%s): Request buffers for new format failed: %d !",
              __func__, config_.node, res);
    return Status::INTERNAL_ERROR;
  }

  int32_t fd = -1;
  for (size_t i = 0; i < num_done; ++i) {
    res = v4l2_wrapper_->ExportBuffer(i, &fd);
    if (res) {
      ALOGE("%s (%s): can't get v4l2 allocated buffer: %d !",
                __func__, config_.node, res);
      return Status::INTERNAL_ERROR;
    }

    ALOGV("%s (%s): export buffer %zu: format 0x%x (%dx%d), size: %d",
              __func__, config_.node, i, format.v4l2_pixel_format(),
              format.width(), format.height(), buffer_size);

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

Status V4l2Stream::findBestFitFormat(const Stream &stream,
                                     StreamFormat *stream_format) {
  uint32_t format = StreamFormat::HalToV4L2PixelFormat(
                        stream.format, config_.implementation_defined_format);
  uint32_t width = stream.width;
  uint32_t height = stream.height;

  ALOGV("%s (%s): try to find best fit format.", __func__, config_.node);
  ALOGV("%s (%s): framework requesting format %x = %x (%dx%d)",
            __func__, config_.node, stream.format, format, width, height);

  int index = StreamFormat::FindMatchingFormat(supported_formats_,
                                               format, width, height);
  if (index >= 0) {
    *stream_format = supported_formats_[index];
    return Status::OK;
  }

  ALOGI("%s (%s): the driver doesn't support the needed format (0x%x)",
            __func__, config_.node, format);

  /* The driver can't use the format.
   * Check if the requested format can be converted from YU12.
   * For now, all conversion will be done through CachedFrame which will
   * imediately convert the qualified format into YU12.
   */
  if (!ImageProcessor::SupportsConversion(V4L2_PIX_FMT_YUV420, format)) {
    ALOGE("%s (%s): conversion between YU12 and 0x%x is not supported !",
              __func__, config_.node, format);
    return Status::ILLEGAL_ARGUMENT;
  }

  /* The format can be converted from YU12, find a qualified format */
  index = StreamFormat::FindFormatByResolution(qualified_formats_,
                                                   width, height);
  if (index >= 0) {
    *stream_format = qualified_formats_[index];
    ALOGI("%s (%s): found qualified format 0x%x at index %d", __func__,
              config_.node, stream_format->v4l2_pixel_format(), index);
    return Status::OK;
  }

  ALOGE("%s (%s): no format found to fullfill framework request !",
            __func__, config_.node);

  return Status::ILLEGAL_ARGUMENT;
}

Status V4l2Stream::configurePipeline(const StreamFormat &format) {
  if (v4l2_to_bus.find(format.v4l2_pixel_format()) == v4l2_to_bus.cend()) {
    ALOGE("%s (%s): cannot find media bus code for v4l2 pixel format %x",
              __func__, config_.node, format.v4l2_pixel_format());
    return Status::INTERNAL_ERROR;
  }

  int res = v4l2_wrapper_->SetFormat(format);
  if (res) {
    ALOGE("%s (%s): failed to set device to correct format for stream: %d !",
              __func__, config_.node, res);
    return Status::INTERNAL_ERROR;
  }

  std::string dev(config_.node);
  std::string subdev = "/dev/v4l-subdev" + dev.substr(10);
  int fd = open(subdev.c_str(), O_RDWR);
  if (fd == -1) {
    ALOGE("%s (%s): cannot open v4l2 sub-device %s (%d)",
              __func__, config_.node, subdev.c_str(), errno);
    return Status::INTERNAL_ERROR;
  }

  v4l2_subdev_selection sel;
  memset(&sel, 0, sizeof(sel));
  sel.pad = 0;
  sel.target = V4L2_SEL_TGT_COMPOSE;
  sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  sel.r.left = 0;
  sel.r.top = 0;
  sel.r.width = format.width();
  sel.r.height = format.height();
  res = ioctl(fd, VIDIOC_SUBDEV_S_SELECTION, &sel);
  if (res != 0) {
    ALOGE("%s (%s): SUBDEV_S_SELECTION failed: %d", __func__, config_.node, res);
    close(fd);
    return Status::INTERNAL_ERROR;
  }

  v4l2_subdev_format new_format;
  new_format.pad = 1;
  new_format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
  res = ioctl(fd, VIDIOC_SUBDEV_G_FMT, &new_format);
  if (res != 0) {
    ALOGE("%s (%s): SUBDEV_G_FMT failed: %d", __func__, config_.node, res);
    close(fd);
    return Status::INTERNAL_ERROR;
  }

  new_format.format.width = format.width();
  new_format.format.height = format.height();
  new_format.format.code = v4l2_to_bus.at(format.v4l2_pixel_format());
  res = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &new_format);
  if (res != 0) {
    ALOGE("%s (%s): SUBDEV_S_FMT failed: %d", __func__, config_.node, res);
    close(fd);
    return Status::INTERNAL_ERROR;
  }

  close(fd);

  return Status::OK;
}

bool V4l2Stream::isCompatible(const Stream& stream) {
  return stream.streamType == stream_.streamType &&
          stream.width == stream_.width &&
          stream.height == stream_.height &&
          stream.format == stream_.format &&
          stream.dataSpace == stream_.dataSpace;
}

Status V4l2Stream::update(const Stream& stream) {
  (void)(stream);

  return Status::OK;
}

Status V4l2Stream::processCaptureBuffer(
    int32_t frame_number,
    const StreamBuffer &sb,
    const std::shared_ptr<const helper::CameraMetadata> &settings) {
  Status status = importBuffer(sb);
  if (status != Status::OK)
    return status;

  std::lock_guard l(v4l2_buffer_mutex_);
  /* Make sure the stream is on. */
  if (!started_) {
    v4l2_wrapper_->StreamOn();

    /* queue all buffers to the available list */
    for (size_t i = 0; i < v4l2_buffers_.size(); ++i)
      available_buffers_.push(i);

    started_ = true;
  }

  if (available_buffers_.empty()) {
    ALOGE("%s (%s): no v4l2 buffer available",
              __func__, config_.node);
    return Status::INTERNAL_ERROR;
  }

  int buffer_id = available_buffers_.front();
  available_buffers_.pop();
  if (v4l2_wrapper_->EnqueueRequest(buffer_id)) {
    ALOGE("%s (%s): can't requeue a buffer in the driver",
              __func__, config_.node);
    return Status::INTERNAL_ERROR;
  }

  TrackedStreamBuffer tsb = {
    .frame_number = frame_number,
    .stream_id = sb.streamId,
    .buffer_id = sb.bufferId,
    .status = BufferStatus::OK,
    .acquire_fence = dupFromAidlIfNotNull(sb.acquireFence),
    .release_fence = nullptr,
    .settings = settings
  };

  {
    std::lock_guard l2(capture_mutex_);
    capture_queue_.push(std::move(tsb));
  }
  capture_cond_.notify_one();

  return Status::OK;
}

void V4l2Stream::captureRequestThread() {
  ALOGI("%s (%s): Capture Result Thread started", __func__, config_.node);

  while (1) {
    std::unique_lock<std::mutex> capture_lock(capture_mutex_);
    capture_cond_.wait(capture_lock, [this](){
      return !capture_active_ || !capture_queue_.empty();
    });

    if (!capture_active_)
      break;

    capture_lock.unlock();

    uint32_t index = 0;
    int res = v4l2_wrapper_->DequeueRequest(&index);
    if (res == -EAGAIN) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      /* No v4l2 buffer available yet, continue */
      continue;
    }

    /* We don't want a flush occur during the processing of a result */
    std::lock_guard flush_lock(flush_mutex_);

    /* Check there is still tracked buffers to complete */
    capture_lock.lock();
    if (capture_queue_.empty()) {
      /* No buffer available for completion.
       * Should not happen unless a flush occured.
       */
      ALOGE("%s (%s): there is no output buffer to complete ! "
            "Maybe a flush occured ?", __func__, config_.node);
      continue;
    }

    TrackedStreamBuffer tsb = capture_queue_.front();
    capture_queue_.pop();
    capture_lock.unlock();

    Status status = Status::OK;
    if (res < 0) {
      ALOGE("%s (%s): V4L2 wrapped failed to dequeue buffer !",
                __func__, config_.node);
      status = Status::INTERNAL_ERROR;
    } else {
      /* Convert and copy the buffer into the client buffer */
      std::unique_ptr<arc::V4L2FrameBuffer> &v4l2_buffer = v4l2_buffers_[index];
      v4l2_buffer->SetDataSize(res);

      status = processCaptureResultConversion(v4l2_buffer, tsb);

      /* Set the buffer index back to the available buffer list */
      {
        std::lock_guard l(v4l2_buffer_mutex_);
        available_buffers_.push(index);
      }
    }

    /* Finish the buffer processing with error or a valid result */
    if (status != Status::OK) {
      cb_->processCaptureBufferError(tsb);
    } else {
      cb_->processCaptureBufferResult(tsb);
    }

    if (tsb.acquire_fence) {
      native_handle_close(tsb.acquire_fence);
      native_handle_delete(tsb.acquire_fence);
    }
    if (tsb.release_fence) {
      native_handle_close(tsb.release_fence);
      native_handle_delete(tsb.release_fence);
    }
  }

  ALOGI("%s (%s): Capture Result Thread ended", __func__, config_.node);
}

Status V4l2Stream::processCaptureResultConversion(
    const std::unique_ptr<arc::V4L2FrameBuffer> &v4l2_buffer,
    TrackedStreamBuffer &tsb) {
  ALOGV("%s (%s): enter", __func__, config_.node);

  uint32_t width = stream_.width;
  uint32_t height = stream_.height;
  uint32_t fourcc = StreamFormat::HalToV4L2PixelFormat(
      stream_.format, config_.implementation_defined_format);
  buffer_handle_t buffer = getSavedBuffer(tsb.buffer_id);

  ALOGV("%s (%s): got buffer format: 0x%x (%dx%d), need format 0x%x (%dx%d)",
            __func__, config_.node,
            v4l2_buffer->GetFourcc(), v4l2_buffer->GetWidth(),
            v4l2_buffer->GetHeight(), fourcc, width, height);

  /* Note that the device buffer length is passed to the output frame. If the
   * GrallocFrameBuffer does not have support for the transformation to
   * [fourcc|, it will assume that the amount of data to lock is based on
   * |v4l2_buffer buffer_size|, otherwise it will use the
   * ImageProcessor::ConvertedSize.
   */
  arc::GrallocFrameBuffer output_frame(
      buffer, stream_.width, stream_.height, fourcc, v4l2_buffer->GetDataSize(),
      static_cast<int32_t>(stream_.usage) & (
          static_cast<int32_t>(BufferUsage::CPU_READ_MASK) |
          static_cast<int32_t>(BufferUsage::CPU_WRITE_MASK)
      ), &mapper_helper_);

  Status status = Status::OK;
  int res = output_frame.Map(tsb.acquire_fence);
  if (res) {
    ALOGE("%s (%s): failed to map output frame !", __func__, config_.node);
    return Status::INTERNAL_ERROR;
  }

  if (v4l2_buffer->GetFourcc() == fourcc &&
      v4l2_buffer->GetWidth() == width &&
      v4l2_buffer->GetHeight() == height) {
    // If no format conversion needs to be applied, directly copy the data over.
    memcpy(output_frame.GetData(),
           v4l2_buffer->GetData(), v4l2_buffer->GetDataSize());
  } else {
    std::lock_guard l(convert_mutex_);
    arc::CachedFrame cached_frame;
    cached_frame.SetSource(v4l2_buffer.get(), 0);
    res = cached_frame.Convert(*(tsb.settings), &output_frame);
    if (res) {
      ALOGE("%s (%s): conversion failed !", __func__, config_.node);
      status = Status::INTERNAL_ERROR;
    }
  }

  hidl_handle handle;
  res = output_frame.Unmap(&handle);
  if (handle != nullptr)
    tsb.release_fence = native_handle_clone(handle);

  return status;
}

void V4l2Stream::flush() {
  std::lock_guard flush_lock(flush_mutex_);
  std::unique_lock capture_lock(capture_mutex_);

  while (!capture_queue_.empty()) {
    TrackedStreamBuffer tsb = capture_queue_.front();
    capture_queue_.pop();
    capture_lock.unlock();

    cb_->processCaptureBufferError(tsb);

    if (tsb.acquire_fence) {
      native_handle_close(tsb.acquire_fence);
      native_handle_delete(tsb.acquire_fence);
    }
    if (tsb.release_fence) {
      native_handle_close(tsb.release_fence);
      native_handle_delete(tsb.release_fence);
    }

    capture_lock.lock();
  }

  {
    std::lock_guard l(v4l2_buffer_mutex_);
    available_buffers_ = std::queue<int>();
  }
  v4l2_wrapper_->StreamOff();
  started_ = false;
}

Status V4l2Stream::importBuffer(const StreamBuffer &stream_buffer) {
  Status status = Status::OK;
  const buffer_handle_t buffer = getSavedBuffer(stream_buffer.bufferId);
  if (buffer == nullptr)
    status = saveBuffer(stream_buffer);

  return status;
}

Status V4l2Stream::saveBuffer(const StreamBuffer &stream_buffer) {
  buffer_handle_t buffer = makeFromAidlIfNotNull(stream_buffer.buffer);
  if (buffer == nullptr) {
    ALOGE("%s (%s): bufferId %" PRIu64 " has null buffer handle !",
              __func__, config_.node, stream_buffer.bufferId);
    return Status::ILLEGAL_ARGUMENT;
  }

  bool res = mapper_helper_.importBuffer(buffer);
  if (!res || buffer == nullptr) {
    ALOGE("%s (%s): output buffer %" PRIu64 " is invalid",
              __func__, config_.node, stream_buffer.bufferId);
    return Status::INTERNAL_ERROR;
  }

  std::lock_guard lock(buffer_mutex_);
  buffer_map_[stream_buffer.bufferId] = buffer;

  return Status::OK;
}

buffer_handle_t V4l2Stream::getSavedBuffer(int64_t buffer_id) {
  std::lock_guard lock(buffer_mutex_);

  decltype(buffer_map_)::const_iterator it = buffer_map_.find(buffer_id);
  if (it == buffer_map_.cend())
    return nullptr;

  return it->second;
}

void V4l2Stream::freeBuffer(int64_t id) {
  std::lock_guard lock(buffer_mutex_);

  decltype(buffer_map_)::const_iterator it = buffer_map_.find(id);
  if (it == buffer_map_.cend()) {
    ALOGE("%s (%s): the buffer id  %" PRIu64 " is not tracked !",
              __func__, config_.node, id);
    return;
  }

  mapper_helper_.freeBuffer(it->second);
  buffer_map_.erase(it);
}

} // implementation
} // device
} // camera
} // hardware
} // android

