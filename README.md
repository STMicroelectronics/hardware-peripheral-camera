# hardware-camera #

This module contains the STMicroelectronics Camera HAL source code.
It is part of the STMicroelectronics delivery for Android (see the [delivery][] for more information).

[delivery]: https://wiki.st.com/stm32mpu/wiki/STM32MP15_distribution_for_Android_release_note_-_v1.1.0

## Description ##

This is the first version of the module for stm32mp1.

It generates libraries to be able to use the camera:
* android.hardware.camera.common@<version>-arc.stm32mp1: converts images from YUV420 to NV21, YUV420, YVU420, BGR32, RGB32, JPEG.
* android.hardware.camera.common@<version>-v4l2.stm32mp1: wrapper on the V4L2 driver.
* android.hardware.camera.common@<version>-helper.stm32mp1: various helper functions used in the camera HAL implementation.
* android.hardware.camera.common@<version>-metadata.stm32mp1: metadata helper functions used in the camera HAL implementation.

All these libraries are used in the android.hardware.camera.provider@<version>-service.stm32mp1 service, which implements Camera.Provider API v2.4 and Camera.Device API v3.2.

Please see the release notes for more details.

## Documentation ##

* The [release notes][] provide information on the release.
* The [distribution package][] provides detailed information on how to use this delivery.

[release notes]: https://wiki.st.com/stm32mpu/wiki/STM32MP15_distribution_for_Android_release_note_-_v1.1.0
[distribution package]: https://wiki.st.com/stm32mpu/wiki/STM32MP1_Distribution_Package_for_Android

## Dependencies ##

This module can not be used alone. It is part of the STMicroelectronics delivery for Android.
You need to add the HAL implementation "android.hardware.camera.provider@<version>-service.stm32mp1" in the device.mk as follow:
```
PRODUCT_PACKAGES += \
	android.hardware.camera.provider@<version>-service.stm32mp1
```

## Contents ##

This directory contains the sources and the associated Android makefile to generate the android.hardware.camera.provider@2.4-service.stm32mp1 service.

## License ##

This module is distributed under the Apache License, Version 2.0 found in the [LICENSE](./LICENSE) file.
