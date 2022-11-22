# rt5663
Realtek ALC 5663 I2C Codec driver

Supports:
* Jack Detection
* Headphone output
* Sleep/Wake

Note:
* Intel SST proprietary drivers do NOT have documented interfaces, so this driver will not work with them.
* Using this driver on chromebooks with this audio chip will require using CoolStar SST Audio or CoolStar SOF Audio

Tested on Asus Chromebox 3 (Core i7-8650U)