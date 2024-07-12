# hardware-camera #

This module contains the STMicroelectronics android.hardware.camera.provider binary source code.

It is part of the STMicroelectronics delivery for Android.

## Description ##

This module implements android.hardware.camera.provider AIDL version 1.
Please see the Android delivery release notes for more details.

It generates libraries to be able to use the camera:
* android.hardware.camera.common@<version>-arc.stm32mpu: converts images from YUV420 to NV21, YUV420, YVU420, BGR32, RGB32, JPEG.
* android.hardware.camera.common@<version>-v4l2.stm32mpu: wrapper on the V4L2 driver.
* android.hardware.camera.common@<version>-helper.stm32mpu: various helper functions used in the camera HAL implementation.
* android.hardware.camera.common@<version>-metadata.stm32mpu: metadata helper functions used in the camera HAL implementation.
* android.hardware.camera.common@<version>-parser.stm32mpu: parser helper functions used in the camera HAL implementation.

All these libraries are used in the android.hardware.camera.provider-service.stm32mpu service, which implements camera.provider API and camera.device API.

Please see the release notes for more details.

## Documentation ##

* The [release notes][] provide information on the release.
[release notes]: https://wiki.st.com/stm32mpu/wiki/STM32_MPU_OpenSTDroid_release_note_-_v5.1.0

## Dependencies ##

This module can not be used alone. It is part of the STMicroelectronics delivery for Android.

```
PRODUCT_PACKAGES += \
	android.hardware.camera.provider-service.stm32mpu
```

## Containing ##

This directory contains the sources and associated Android makefile to generate the camera provider binary.

## License ##

This module is distributed under the Apache License, Version 2.0 found in the [LICENSE](./LICENSE) file.
