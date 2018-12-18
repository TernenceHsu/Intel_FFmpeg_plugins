FFmpeg README
=============

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* [ffserver](https://ffmpeg.org/ffserver.html) is a multimedia streaming server
  for live broadcasts.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.

---

### 编译
先安装 intel media code 环境；
```
./build.sh
./config.sh
```

### 测试
```
ffmpeg -hide_banner -codecs | grep h264
 DEV.LS h264                 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (decoders: h264 h264_qsv h264_cuvid ) (encoders: h264_nvenc h264_qsv h264_vaapi nvenc nvenc_h264 )
```

### 转码
```
ffmpeg.exe -hwaccel qsv -c:v h264_qsv -i "1.mp4" -c:v h264_qsv -preset veryslow -b:v 20M -g 60 -r 60 -s 1920x1080  -c:a aac  -ab 192k -ar 48000 -ac 2  -f mp4 "2.mp4" -y
```
分析：因为修改分辨率没有配置在 gpu 里面完成 filter ，所以报错；去掉 -s 就可以
```
ffmpeg.exe -c:v h264_qsv -i "1.mp4" -c:v h264_qsv -preset veryslow -b:v 20M -g 60 -r 60 -s 1920x1080  -c:a aac  -ab 192k -ar 48000 -ac 2  -f mp4 "2.mp4" -y
```





