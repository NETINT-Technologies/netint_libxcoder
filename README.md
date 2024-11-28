# NETINT Quadra Libxcoder README

Libxcoder is the software driver for [NETINT Quadra Video Processing Units (VPU)](https://netint.com/technology/codensity-g5/)  
It contains APIs and tools for video processing and Quadra device management.

The Libxcoder API can be interfaced with directly, or accesed via higher-level APIs/aplications such as [NETINT's FFmpeg fork](https://github.com/NETINT-Technologies/netint_ffmpeg).

## Public Libraries

* `ni_device_api.h` API for video processing
* `ni_rsrc_api.h` API for Quadra hardware management

## Tools

* `init_rsrc` is an application to detect Quadra devices and create their resource entries in host's file system
* `ni_rsrc_list` is an application to list info about detected Quadra devices
* `ni_rsrc_mon` is an application to display performance related info about detected Quadra devices
* `ni_rsrc_namespace` is an application to manage NVMe configuration for Quadra devices
* `ni_rsrc_update` is an application to manage detected Quadra resource entries

## Documentation

Libxcoder documentation is availabled at the [NETINT Docs Portal](https://docs.netint.com/quadra/documentation/libxcoder)

Doxygen documentation may be generated with `bash build.sh -d`

## License

NETINT Quadra Libxcoder is MIT-0 licensed for NETINT written code.  
Software not written by NETINT but included in libxcoder:
* `source/ni_bitstream.*`
  * code derived from Kvazaar HEVC encoder for encoded bitstream filtering
  * Copyright (c) 2021, Tampere University
  * BSD-3 License

## Building
### Scripted Build and Install
```bash
bash build.sh
```
See options using `bash build.sh -h`

This will compile libxcoder and install its tools/shared-libraries/headers to the system.

### Manually Build and Install
```bash
bash configure
```
See options using `bash configure -h`

```bash
make
sudo make install
```

Default install location is `/usr/local/lib`, `/usr/local/include`, and `/usr/local/bin`.  
This may be changed via `configure` options:  
```bash
bash configure --libdir=/custom_lib_folder --bindir=/custom_bin_folder \
               --includedir=/custom_include_folder --shareddir=/additional_lib_folder
```

### Uninstall
```bash
sudo make uninstall
```

alternate option

```bash
sudo make uninstall LIBDIR=/custom_lib_folder BINDIR=/custom_bin_folder \
                    INCLUDEDIR=/custom_include_folder SHAREDDIR=/additional_lib_folder
```

<!-- ## xcoderp2p
`xcoderp2p` is a demo app for PCIe peer-to-peer DMA  
Not supported on Windows or MacOS or Linux kernel < 5.10  .
See instructions in NETINT dma-buf project. -->

## xcoder

`xcoder` is a demo app for video decoding/encoding/transcoding using libxcoder.  
See `./build/xcoder -h` for options.

### Example commands:

#### Decoding
```bash
./build/xcoder -c 0 -i test/1280x720p_Basketball.264 -m a2y -o bball.yuv
```

#### Encoding
```bash
./build/xcoder -c 0 -s 1280x720 -i bball.yuv -m y2h -o bball.265
```

#### Transcoding
```bash
./build/xcoder -c 0 -i test/1280x720p_Basketball.264 -m a2h -o bball_xcod.265
```

#### Transcoding with HW frames 
With this command the YUV data remains on Quadra HW.
```bash
./build/xcoder -c 0 -d out=hw -i test/1280x720p_Basketball.264 -m a2h -o bball_xcod_hwframe.265
```

#### Hardware Frame Upload + Encoding
```bash
./build/xcoder -c 0 -s 1280x720 -i bball.yuv -m u2h -o bball_hwframe.265
```

## Integration

Codec library: `libxcoder.a`  
API header: `ni_device_api.h`

1. Add libxcoder.a as one of libraries to link
2. Add ni_device_api.h in source code calling Codec API

### C
```C
#include "ni_device_api.h"
```

### C++
```C++
extern "C" {
#include "ni_device_api.h"
}
```

<!-- ## Forums

https://community.netint.ca/ -->