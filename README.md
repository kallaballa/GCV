# GCV
OpenGL/OpenCL/VAAPI interop demos (aka. run it on the GPU!) using my 4.x fork of OpenCV (https://github.com/kallaballa/opencv/tree/GCV)

The goal of the demos is to show how to use OpenCL interop in conjunction with OpenCV on Linux to create programs that run mostly (the part the matters) on the GPU. Until the [necessary changes](https://github.com/opencv/opencv/pulls/kallaballa) are pulled into the official repository you need to build my fork of OpenCV 4.x.

* The author of the example video (which is also used for two of the demo videos in this README) is **(c) copyright Blender Foundation | www.bigbuckbunny.org**.
* The right holders of the video used for the optical flow visualization are **https://www.bbtv.com**. I tried to contact them several times to get an opinion on my fair-use for educational purpose. The original video: https://www.youtube.com/watch?v=ItGwXRCcisA

# Requirements
* Support for OpenCL 1.2
* Support for cl_khr_gl_sharing and cl_intel_va_api_media_sharing OpenCL extensions.
* If you are on a recent Intel Platform (Gen8 - Gen12) you need to install an alternative [compute-runtime](https://github.com/kallaballa/compute-runtime)

There are currently five demos (**the preview videos are scaled down and compressed**):
## tetra-demo
Renders a rainbow tetrahedron on blue background using OpenGL and decodes/encodes on the GPU.

https://user-images.githubusercontent.com/287266/200169105-2bb88288-cb07-49bb-97ef-57ac61a0cfb8.mp4

## video-demo
Renders a rainbow tetrahedron on top of a input-video using OpenGL and decodes/encodes on the GPU.

https://user-images.githubusercontent.com/287266/200169164-231cb4d8-db5c-444b-8aff-55c9f1a822cc.mp4

## nanovg-demo
Renders a color wheel on top of a input-video using nanovg (OpenGL) and decodes/encodes on the GPU.

https://user-images.githubusercontent.com/287266/200169216-1ff25db5-f5e0-49d1-92ba-ab7903168754.mp4

## font-demo
Renders a Star Wars like text crawl using nanovg (OpenGL). Encodes on the GPU.

https://user-images.githubusercontent.com/287266/204157553-758adaeb-e9b8-48eb-bc09-8098a5379d2b.mp4

## optflow-demo
My take on a optical flow visualization on top of a video. Uses nanovg for rendering (OpenGL) and decodes/encodes on the GPU.

https://user-images.githubusercontent.com/287266/202174513-331e6f08-8397-4521-969b-24cbc43d27fc.mp4

# Instructions
You need to build the most recent 4.x branch of OpenCV.

## Build OpenCV

```bash
git clone --branch GCV https://github.com/kallaballa/opencv.git
cd opencv
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DOPENCV_ENABLE_EGL_INTEROP=ON -DOPENCV_FFMPEG_ENABLE_LIBAVDEVICE=ON -DWITH_OPENGL=ON -DWITH_VA=ON -DWITH_VA_INTEL=ON -DWITH_QT=ON -DBUILD_PERF_TESTS=OFF -DBUILD_TESTS=OFF ..
make -j8
sudo make install
```

## Build demo code

```bash
git clone https://github.com/kallaballa/GCV.git
cd GCV
make -j2
```
## Download the example file
```bash
wget -O bunny.webm https://upload.wikimedia.org/wikipedia/commons/transcoded/f/f3/Big_Buck_Bunny_first_23_seconds_1080p.ogv/Big_Buck_Bunny_first_23_seconds_1080p.ogv.1080p.vp9.webm
```
## Run the tetra-demo:

```bash
src/tetra/tetra-demo
```

## Run the video-demo:

```bash
src/video/video-demo bunny.webm
```

## Run the nanovg-demo:

```bash
src/nanovg/nanovg-demo bunny.webm
```

## Run the font-demo:

```bash
src/font/font-demo
```

## Run the optflow-demo:

```bash
src/optflow/optflow-demo bunny.webm
```


