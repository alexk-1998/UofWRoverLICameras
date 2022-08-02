# UofWRoverLICameras
This repository contains code for building executables for imaging with a 6-camera setup using the LI-TX1-CB-6CAM board from Leopard Imaging, running on a Jetson TX2.

The first executable, `StreamPreview`, is intended for displaying the 6 camera streams onto a composite frame. This is useful for setting up the cameras into the correct imaging position.

The second executable, `StreamCapture`, is intended for capturing and saving frames repeatedly from each stream, until the process is killed or a time limit is reached.

# Issues
Currently, the second sensor mode is disabled for the device, enabling it will cause run-time bugs, but this is avoided while parsing the supplied options.

The fps is currently lower than expected.

# Setup
Follow the instructions 1-8 in `IMX265-MIPI_R32.3.1_TX2_Six-CB_20200222_Driver_Guide.pdf` for flashing the Jetson TX2 with JetPack version 32.3.1 and installing the LI-TX1-CB-6CAM drivers.

After, run:
```
sudo apt update && sudo apt install nvidia-jetpack -y
```
to install the missing JetPack components.

Then, run:
```
sudo apt update && sudo apt install cmake libgtk-3-dev libjpeg-dev libgles2-mesa-dev libgstreamer1.0-dev v4l-utils libv4l-dev -y
```
to install the missing dependencies.

Note: after installing all dependencies, you may no longer be able to SSH into the TX2. This can be re-enabled with:
```
systemctl enable /opt/nvidia/l4t-usb-device-mode/nv-l4t-usb-device-mode.service
service nv-l4t-usb-device-mode start
```

Note: using the libnvjpeg.so file from JetPack 4.3.1 will likely cause run-time errors. Replace the library with the file discussed in [this](https://forums.developer.nvidia.com/t/streaming-using-jpegenc-halts-after-a-short-delay/109924/5) thread. The patched file can be found [here](https://forums.developer.nvidia.com/uploads/short-url/lG7SzRLCUvmzaEwNGw3jtMbH0YI.zip). The file can easily be replaced by running:
```
sudo cp /home/nvidia/Downloads/libnvjpeg.so /usr/lib/aarch64-linux-gnu/tegra/
```
On a different system, the existing library may not be under the directory `/usr/lib/aarch64-linux-gnu/tegra/` and the actual file path can be found with:
```
sudo find / -name libnvjpeg.so
```

# Build
Clone the repository, a `Makefile` is provided that allows both executables to be built by running the command:
```
make
make install
```
from the root of the cloned repository directory. There will be resultant executables in the base of the repository, as well as symbolic links in the user home folder ~.

# Run
Both executables are intended to be ran from the command line. Either executable can be ran with default options by calling
```
./StreamPreview
./StreamCapture
```
from the base of the repository directory. The `StreamCapture` executable has several options available, and can be displayed to the console with either:
```
./StreamCapture -h
./StreamCapture --help
```
The full list of options and their valid values are provided below.

# Options
Omitting any optional flag will cause the executable to be ran with the default value for that flag. All possible flags are shown below with the following format
```
--long-flag -f
<valid_values>
description
```

```
--capture-mode -m 
<0 or 1>
Capture mode for the IMX265 cameras. [Default: 0]
Mode 0: 2048x1554 @ 38fps
Mode 1: 1936x1106 @ 30fps

--root-directory -r
<any string, must start with alphanumeric character>
Root path of the image directory for storing all images. [Default: system time]
Creates a file structure of the form:
root
  cam0
      image000000.jpg
      image000001.jpg
      ...
  cam1
  ...

--save-every -s
<1-inf>
Save every s frames from the stream. [Default: 1]
Default will save every frame, if s == 2 then every second frame is saved, etc.
This results in an effective frame rate = fps / s.

--capture-time -t
<0-inf>
Recording time in seconds. [Default: 0]
Passing 0 requires the process be killed from an external signal (ctrl+c).

--profile -p
<no value>
Enable encoder profiling.

--help -h
<no value>
Print this help.
```

So, to run the executable with cameras running 2048x1554 @ 38fps, saving to a directory named "foo", saving every 10th frame, and recording until an external signal is received, call either one of:
```
./StreamCapture -m 0 -r foo -s 10 -t 0
./StreamCapture --capture-mode 0 --root-directory foo --save-every 10 --capture-time 0
./StreamCapture -r foo -s 10
./StreamCapture --root-directory foo --save-every 10
```
since the desired mode and durations are the default behaviours. Note that the order of the flags does not matter and long and short form flags can be mixed without error.
