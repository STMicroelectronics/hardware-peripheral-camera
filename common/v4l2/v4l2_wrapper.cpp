/*
 * Copyright 2019 The Android Open Source Project
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

#define LOG_TAG "android.hardware.camera.common@1.0-v4l2.stm32mpu"
// #define LOG_NDEBUG 0

#include "utils/Log.h"

#include "v4l2_wrapper.h"

#include <sys/mman.h>

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace v4l2 {

const int32_t kStandardSizes[][2] = {
  {2592, 1944}, // MAX (IMX335) (4:3)
  {1920, 1080}, // 1080p (16:9)
  {1600, 1200}, // UXGA (4:3)
  {1280,  960}, // SXGA (4:3)
  {1280,  720}, // 720p (16:9)
  {1024,  768}, // XGA (4:3)
  { 640,  480}, // VGA (4:3)
  { 320,  240}  // QVGA (4:3)
};

V4L2Wrapper::V4L2Wrapper(const std::string device_path)
    : device_path_(std::move(device_path)),
      connection_count_(0) {}

V4L2Wrapper::~V4L2Wrapper() {}

int V4L2Wrapper::IsV4L2VideoDevice(int fd, v4l2_capability *cap) {
  if (TEMP_FAILURE_RETRY(ioctl(fd, VIDIOC_QUERYCAP, cap)) != 0) {
    ALOGE("%s: VIDIOC_QUERY_CAP fail: %s", __FUNCTION__, strerror(errno));
    return -errno;
  }
  if (!(cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    ALOGE("%s: device is not a V4L2 video capture device", __FUNCTION__);
    return -ENODEV;
  }

  return 0;
}

const std::string V4L2Wrapper::getDevicePath() const {
  return device_path_;
}

int V4L2Wrapper::Connect() {
  ALOGV("%s: enter", __FUNCTION__);
  std::lock_guard lock(connection_lock_);

  if (Connected()) {
    ALOGV("%s: Camera device %s is already connected.",
              __FUNCTION__, device_path_.c_str());
    ++connection_count_;
    return 0;
  }

  ALOGV("%s: open camera device %s",
              __FUNCTION__, device_path_.c_str());

  /* Open in nonblocking mode (DQBUF may return EAGAIN). */
  int fd = TEMP_FAILURE_RETRY(open(device_path_.c_str(), O_RDWR | O_NONBLOCK));
  if (fd < 0) {
    ALOGE("%s: failed to open %s (%s)",
              __FUNCTION__, device_path_.c_str(), strerror(errno));
    return -ENODEV;
  }
  device_fd_.reset(fd);
  ++connection_count_;

  /* Check if this connection has the extended control query capability. */
  v4l2_query_ext_ctrl query;
  query.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
  extended_query_supported_ = (ioctlLocked(VIDIOC_QUERY_EXT_CTRL, &query) == 0);

  // TODO(b/29185945): confirm this is a supported device.
  // This is checked by the HAL, but the device at device_path_ may
  // not be the same one that was there when the HAL was loaded.
  // (Alternatively, better hotplugging support may make this unecessary
  // by disabling cameras that get disconnected and checking newly connected
  // cameras, so Connect() is never called on an unsupported camera)

  return 0;
}

void V4L2Wrapper::Disconnect() {
  ALOGV("%s: enter", __FUNCTION__);
  std::lock_guard lock(connection_lock_);

  if (connection_count_ == 0) {
    // Not connected.
    ALOGE("%s: Camera device %s is not connected, cannot disconnect.",
              __FUNCTION__, device_path_.c_str());
    return;
  }

  --connection_count_;
  if (connection_count_ > 0) {
    ALOGV("%s: Disconnected from camera device %s. %d connections remain.",
              __FUNCTION__, device_path_.c_str(), connection_count_);
    return;
  }

  device_fd_.reset(-1);  // Includes close().
  format_.reset();
}

/* Helper function. Should be used instead of ioctl throughout this class. */
template <typename T>
int V4L2Wrapper::ioctlLocked(int request, T data) {
  /* Potentially called so many times logging entry is a bad idea. */
  std::lock_guard lock(device_lock_);

  if (!Connected()) {
    ALOGE("%s: Device %s not connected.", __FUNCTION__, device_path_.c_str());
    return -ENODEV;
  }
  return TEMP_FAILURE_RETRY(ioctl(device_fd_.get(), request, data));
}

int V4L2Wrapper::StreamOn() {
  if (!format_) {
    ALOGE("%s: Stream format must be set before turning on stream.", __FUNCTION__);
    return -EINVAL;
  }

  int32_t type = format_->type();
  if (ioctlLocked(VIDIOC_STREAMON, &type) < 0) {
    ALOGE("%s: STREAMON fails (%d): %s", __FUNCTION__, errno, strerror(errno));
    return -ENODEV;
  }

  ALOGV("%s: Stream turned on.", __FUNCTION__);
  return 0;
}

int V4L2Wrapper::StreamOff() {
  if (!format_) {
    /* Can't have turned on the stream without format being set,
     * so nothing to turn off here.
     */
    return 0;
  }

  int32_t type = format_->type();
  int res = ioctlLocked(VIDIOC_STREAMOFF, &type);

  if (res < 0) {
    ALOGE("%s: STREAMOFF fails: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  ALOGV("%s: Stream turned off.", __FUNCTION__);

  return 0;
}

int V4L2Wrapper::QueryControl(uint32_t control_id,
                              v4l2_query_ext_ctrl* result) {
  int res = 0;

  memset(result, 0, sizeof(*result));

  if (extended_query_supported_) {
    result->id = control_id;
    res = ioctlLocked(VIDIOC_QUERY_EXT_CTRL, result);

    /* Assuming the operation was supported (not ENOTTY), no more to do. */
    if (errno != ENOTTY) {
      if (res) {
        ALOGE("%s: QUERY_EXT_CTRL fails: %s", __FUNCTION__, strerror(errno));
        return -ENODEV;
      }

      return 0;
    }
  }

  /* Extended control querying not supported, fall back to basic control query. */
  v4l2_queryctrl query;

  query.id = control_id;

  if (ioctlLocked(VIDIOC_QUERYCTRL, &query)) {
    ALOGE("%s: QUERYCTRL fails: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  /* Convert the basic result to the extended result. */
  result->id = query.id;
  result->type = query.type;
  memcpy(result->name, query.name, sizeof(query.name));
  result->minimum = query.minimum;

  if (query.type == V4L2_CTRL_TYPE_BITMASK) {
    /* According to the V4L2 documentation, when type is BITMASK,
     * max and default should be interpreted as __u32. Practically,
     * this means the conversion from 32 bit to 64 will pad with 0s not 1s.
     */
    result->maximum = static_cast<uint32_t>(query.maximum);
    result->default_value = static_cast<uint32_t>(query.default_value);
  } else {
    result->maximum = query.maximum;
    result->default_value = query.default_value;
  }

  result->step = static_cast<uint32_t>(query.step);
  result->flags = query.flags;
  result->elems = 1;

  switch (result->type) {
    case V4L2_CTRL_TYPE_INTEGER64:
      result->elem_size = sizeof(int64_t);
      break;
    case V4L2_CTRL_TYPE_STRING:
      result->elem_size = result->maximum + 1;
      break;
    default:
      result->elem_size = sizeof(int32_t);
      break;
  }

  return 0;
}

int V4L2Wrapper::GetControl(uint32_t control_id, int32_t* value) {
  /* For extended controls (any control class other than "user"),
   * G_EXT_CTRL must be used instead of G_CTRL.
   */
  if (V4L2_CTRL_ID2CLASS(control_id) != V4L2_CTRL_CLASS_USER) {
    v4l2_ext_control control;
    v4l2_ext_controls controls;
    memset(&control, 0, sizeof(control));
    memset(&controls, 0, sizeof(controls));

    control.id = control_id;
    controls.ctrl_class = V4L2_CTRL_ID2CLASS(control_id);
    controls.count = 1;
    controls.controls = &control;

    if (ioctlLocked(VIDIOC_G_EXT_CTRLS, &controls) < 0) {
      ALOGE("%s: G_EXT_CTRLS fails: %s", __FUNCTION__, strerror(errno));
      return -ENODEV;
    }
    *value = control.value;
  } else {
    v4l2_control control{control_id, 0};

    if (ioctlLocked(VIDIOC_G_CTRL, &control) < 0) {
      ALOGE("%s: G_CTRL fails: %s", __FUNCTION__, strerror(errno));
      return -ENODEV;
    }
    *value = control.value;
  }
  return 0;
}

int V4L2Wrapper::SetControl(uint32_t control_id,
                            int32_t desired,
                            int32_t* result) {
  int32_t result_value = 0;

  /* TODO(b/29334616): When async, this may need to check if the stream
   * is on, and if so, lock it off while setting format. Need to look
   * into if V4L2 supports adjusting controls while the stream is on.
   *
   * For extended controls (any control class other than "user"),
   * S_EXT_CTRL must be used instead of S_CTRL.
   */
  if (V4L2_CTRL_ID2CLASS(control_id) != V4L2_CTRL_CLASS_USER) {
    v4l2_ext_control control;
    v4l2_ext_controls controls;
    memset(&control, 0, sizeof(control));
    memset(&controls, 0, sizeof(controls));

    control.id = control_id;
    control.value = desired;
    controls.ctrl_class = V4L2_CTRL_ID2CLASS(control_id);
    controls.count = 1;
    controls.controls = &control;

    if (ioctlLocked(VIDIOC_S_EXT_CTRLS, &controls) < 0) {
      ALOGE("%s: S_EXT_CTRLS fails: %s", __FUNCTION__, strerror(errno));
      return -ENODEV;
    }

    result_value = control.value;
  } else {
    v4l2_control control{control_id, desired};

    if (ioctlLocked(VIDIOC_S_CTRL, &control) < 0) {
      ALOGE("%s: S_CTRL fails: %s", __FUNCTION__, strerror(errno));
      return -ENODEV;
    }

    result_value = control.value;
  }

  /* If the caller wants to know the result, pass it back. */
  if (result != nullptr) {
    *result = result_value;
  }
  return 0;
}

int V4L2Wrapper::GetSupportedFormats(const std::set<uint32_t>& v4l2_formats,
                                                       StreamFormats *formats) {
  ALOGV("%s: enter", __FUNCTION__);
  std::set<std::array<int32_t, 2>> frame_sizes;

  for (uint32_t v4l2_format : v4l2_formats) {
    frame_sizes.clear();

    if (GetFormatFrameSizes(v4l2_format, &frame_sizes)) {
      ALOGE("%s: failed to get frame sizes for format: 0x%x",
                __FUNCTION__, v4l2_format);
      continue;
    }

    for (auto frame_size : frame_sizes) {
      formats->emplace_back(v4l2_format, frame_size[0], frame_size[1]);
    }
  }

  return 0;
}

int V4L2Wrapper::GetFormats(std::set<uint32_t>* v4l2_formats) {
  ALOGV("%s: enter", __FUNCTION__);

  v4l2_fmtdesc format_query;

  memset(&format_query, 0, sizeof(format_query));
  // TODO(b/30000211): multiplanar support.
  format_query.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  while (ioctlLocked(VIDIOC_ENUM_FMT, &format_query) >= 0) {
    v4l2_formats->insert(format_query.pixelformat);
    ++format_query.index;
  }

  if (errno != EINVAL) {
    ALOGE("%s: ENUM_FMT fails at index %d: %s", __FUNCTION__,
              format_query.index, strerror(errno));
    return -ENODEV;
  }
  return 0;
}

int V4L2Wrapper::GetFormatFrameSizes(uint32_t v4l2_format,
                                     std::set<std::array<int32_t, 2>>* sizes) {
  v4l2_frmsizeenum size_query;

  memset(&size_query, 0, sizeof(size_query));
  size_query.pixel_format = v4l2_format;

  if (ioctlLocked(VIDIOC_ENUM_FRAMESIZES, &size_query) < 0) {
    ALOGE("%s: ENUM_FRAMESIZES failed: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  if (size_query.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
    /* Discrete: enumerate all sizes using VIDIOC_ENUM_FRAMESIZES.
     * Assuming that a driver with discrete frame sizes has a reasonable number
     * of them.
     */
    do {
      sizes->insert({{{static_cast<int32_t>(size_query.discrete.width),
                       static_cast<int32_t>(size_query.discrete.height)}}});
      ++size_query.index;
    } while (ioctlLocked(VIDIOC_ENUM_FRAMESIZES, &size_query) >= 0);

    if (errno != EINVAL) {
      ALOGE("%s: ENUM_FRAMESIZES fails at index %d: %s", __FUNCTION__,
                size_query.index, strerror(errno));
      return -ENODEV;
    }
  } else {
    /* Continuous/Step-wise: based on the stepwise struct returned by the query.
     * Fully listing all possible sizes, with large enough range/small enough
     * step size, may produce far too many potential sizes. Instead, find the
     * closest to a set of standard sizes.
     */
    for (const auto size : kStandardSizes) {
      // Find the closest size, rounding up.
      uint32_t desired_width = size[0];
      uint32_t desired_height = size[1];

      if (desired_width < size_query.stepwise.min_width ||
          desired_height < size_query.stepwise.min_height) {
        ALOGV("%s: Standard size %u x %u is too small for format 0x%x",
                  __FUNCTION__, desired_width, desired_height, v4l2_format);
        continue;
      } else if (desired_width > size_query.stepwise.max_width ||
                 desired_height > size_query.stepwise.max_height) {
        ALOGV("%s: Standard size %u x %u is too big for format 0x%x",
                  __FUNCTION__, desired_width, desired_height, v4l2_format);
        continue;
      }

      // Round up.
      uint32_t width_steps = (desired_width - size_query.stepwise.min_width +
                              size_query.stepwise.step_width - 1) /
                             size_query.stepwise.step_width;
      uint32_t height_steps = (desired_height - size_query.stepwise.min_height +
                               size_query.stepwise.step_height - 1) /
                              size_query.stepwise.step_height;
      sizes->insert(
          {{{static_cast<int32_t>(size_query.stepwise.min_width +
                                  width_steps * size_query.stepwise.step_width),
             static_cast<int32_t>(size_query.stepwise.min_height +
                                  height_steps *
                                      size_query.stepwise.step_height)}}});
    }
  }
  return 0;
}

// Converts a v4l2_fract with units of seconds to an int64_t with units of ns.
inline int64_t fractToNs(const v4l2_fract& fract) {
  return (1000000000LL * fract.numerator) / fract.denominator;
}

int V4L2Wrapper::GetFormatFrameDurationRange(
    uint32_t v4l2_format,
    const std::array<int32_t, 2>& size,
    std::array<int64_t, 2>* duration_range) {

  v4l2_frmivalenum duration_query;

  memset(&duration_query, 0, sizeof(duration_query));
  duration_query.pixel_format = v4l2_format;
  duration_query.width = size[0];
  duration_query.height = size[1];

  if (ioctlLocked(VIDIOC_ENUM_FRAMEINTERVALS, &duration_query) < 0) {
    ALOGE("%s: ENUM_FRAMEINTERVALS failed: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  int64_t min = std::numeric_limits<int64_t>::max();
  int64_t max = std::numeric_limits<int64_t>::min();

  if (duration_query.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
    // Discrete: enumerate all durations using VIDIOC_ENUM_FRAMEINTERVALS.
    do {
      min = std::min(min, fractToNs(duration_query.discrete));
      max = std::max(max, fractToNs(duration_query.discrete));
      ++duration_query.index;
    } while (ioctlLocked(VIDIOC_ENUM_FRAMEINTERVALS, &duration_query) >= 0);

    if (errno != EINVAL) {
      ALOGE("%s: ENUM_FRAMEINTERVALS fails at index %d: %s",
                __FUNCTION__, duration_query.index, strerror(errno));
      return -ENODEV;
    }
  } else {
    // Continuous/Step-wise: simply convert the given min and max.
    min = fractToNs(duration_query.stepwise.min);
    max = fractToNs(duration_query.stepwise.max);
  }
  (*duration_range)[0] = min;
  (*duration_range)[1] = max;

  return 0;
}

int V4L2Wrapper::SetFormat(const StreamFormat& resolved_format) {
  ALOGV("%s: enter", __FUNCTION__);
  if (format_ && resolved_format == *format_) {
    ALOGV("%s: Already in correct format, skipping format setting.", __FUNCTION__);
    return 0;
  }

  // Set the camera to the new format.
  v4l2_format new_format;

  resolved_format.FillFormatRequest(&new_format);

  // TODO(b/29334616): When async, this will need to check if the stream
  // is on, and if so, lock it off while setting format.
  if (ioctlLocked(VIDIOC_S_FMT, &new_format) < 0) {
    ALOGE("%s: S_FMT failed: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  // Check that the driver actually set to the requested values.
  if (resolved_format != new_format) {
    ALOGE("%s: Device doesn't support desired stream config.", __FUNCTION__);
    return -EINVAL;
  }

  // Keep track of our new format.
  format_.reset(new StreamFormat(new_format));
  buffer_size_ = new_format.fmt.pix.sizeimage;

  return 0;
}

int V4L2Wrapper::RequestBuffers(uint32_t num_requested, uint32_t *num_done, uint32_t *buffer_size) {
  v4l2_requestbuffers req_buffers;
  int res = 0;

  ALOGV("%s: requesting %d buffers", __FUNCTION__, num_requested);

  if (!format_) {
    ALOGE("%s: requesting buffer but no format was set", __FUNCTION__);
    return -EPERM;
  }

  /* Request new buffers */
  memset(&req_buffers, 0, sizeof(req_buffers));
  req_buffers.type = format_->type();
  req_buffers.memory = V4L2_MEMORY_MMAP;
  req_buffers.count = num_requested;

  res = ioctlLocked(VIDIOC_REQBUFS, &req_buffers);

  /* Calling REQBUFS releases all queued buffers back to the user. */
  if (res < 0) {
    ALOGE("%s: REQBUFS failed: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  /* V4L2 will set req_buffers.count to a number of buffers it can handle. */
  if (num_requested > 0 && req_buffers.count < 1) {
    ALOGE("%s: REQBUFS claims it can't handle any buffers.", __FUNCTION__);
    return -ENODEV;
  }

  if (num_done) {
    *num_done = req_buffers.count;
  }

  if (buffer_size) {
    *buffer_size = buffer_size_;
  }

  return 0;
}

int V4L2Wrapper::ExportBuffer(uint32_t index, int32_t *fd) {
  struct v4l2_exportbuffer expbuf;

  if (!format_) {
    ALOGE("%s: requesting export buf but no format was set", __FUNCTION__);
    return -EPERM;
  }

  memset(&expbuf, 0, sizeof(expbuf));
  expbuf.type = format_->type();
  expbuf.index = index;

  int res = ioctlLocked(VIDIOC_EXPBUF, &expbuf);

  if (res < 0) {
    ALOGE("%s: QUERYBUF failed: %s", __FUNCTION__, strerror(res));
    return res;
  }

  if (fd) {
    *fd = expbuf.fd;
  }

  return 0;
}

/*
 *
 */
int V4L2Wrapper::EnqueueRequest(uint32_t index) {
  if (!format_) {
    ALOGE("%s: Stream format must be set before enqueuing buffers.", __FUNCTION__);
    return -ENODEV;
  }

  /* Set up a v4l2 buffer struct. */
  v4l2_buffer device_buffer;

  memset(&device_buffer, 0, sizeof(device_buffer));
  device_buffer.type = format_->type();
  device_buffer.index = index;

  /* Use QUERYBUF to ensure our buffer/device is in good shape,
   * and fill out remaining fields.
   */
  if (ioctlLocked(VIDIOC_QUERYBUF, &device_buffer) < 0) {
    ALOGE("%s: QUERYBUF fails: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  if (device_buffer.flags & V4L2_BUF_FLAG_QUEUED) {
    ALOGE("%s: buffer already queued", __FUNCTION__);
    return -EAGAIN;
  }

  /* Pass the buffer to the camera. */
  if (ioctlLocked(VIDIOC_QBUF, &device_buffer) < 0) {
    ALOGE("%s: QBUF fails: %s", __FUNCTION__, strerror(errno));
    return -ENODEV;
  }

  return 0;
}

/*
 *  This method asks to the V4L2 driver for a completed capture buffer.
 *
 *  @return 0 on success
 *          -EAGAIN if no buffer were available
 *          -ENODEV if unexptected error occured
 *
 */
int V4L2Wrapper::DequeueRequest(uint32_t *index) {
  if (!format_) {
    ALOGV("%s: Format not set, so stream can't be on, so no buffers available "
            "for dequeueing", __FUNCTION__);
    return -EAGAIN;
  }

  v4l2_buffer device_buffer;

  memset(&device_buffer, 0, sizeof(device_buffer));
  device_buffer.type = format_->type();
  device_buffer.memory = V4L2_MEMORY_MMAP;

  int res = ioctlLocked(VIDIOC_DQBUF, &device_buffer);

  if (res) {
    if (errno == EAGAIN) {
      // Expected failure.
      return -EAGAIN;
    } else {
      // Unexpected failure.
      ALOGE("%s: DQBUF fails: %s", __FUNCTION__, strerror(errno));
      return -ENODEV;
    }
  }

  if (index != nullptr) {
    *index = device_buffer.index;
  }

  return device_buffer.bytesused;
}

} // v4l2
} // V1_0
} // common
} // camera
} // hardware
} // android
