# Third-party notices

Replayer is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License version 3 (see `LICENSE`, or
`LICENSE.txt` in the Windows package). Its source
code is available at the project's GitHub repository.

The Replayer packages also contain the following third-party software. Each
part keeps its own license.

## Qt

- Website: https://www.qt.io
- Source code: https://download.qt.io/official_releases/qt/
- License: GNU Lesser General Public License version 3 (`licenses/LGPL-3.0.txt`,
  which supplements the GNU GPL version 3 in `LICENSE`)
- Copyright: The Qt Company Ltd. and other contributors

Replayer uses the Qt modules Core, Gui, Widgets and OpenGLWidgets and their
plugins. They are linked dynamically and shipped as separate libraries
(`Qt6*.dll` on Windows, `usr/lib/libQt6*.so*` in the AppImage), so they can be
replaced with a compatible, modified version. The Qt version used for a release
is set by `QT_VERSION` in `.github/workflows/build.yml`.

## FFmpeg

- Website: https://ffmpeg.org
- Source code: https://ffmpeg.org/download.html
- Copyright: the FFmpeg developers

Replayer uses the FFmpeg libraries libavcodec, libavformat, libavutil,
libswscale and libswresample. They are linked dynamically and shipped as
separate libraries.

- **Windows:** the LGPL shared build from
  https://github.com/BtbN/FFmpeg-Builds (the archive is set by
  `FFMPEG_ARCHIVE` in `.github/workflows/build.yml`). License: GNU Lesser
  General Public License version 2.1 or later. Its license file is included as
  `licenses/FFmpeg.txt`, and the build's source code is available from the
  BtbN/FFmpeg-Builds releases.
- **Linux (AppImage):** the FFmpeg libraries of Ubuntu 24.04, which are built
  with GPL components (e.g. x264, x265). License: GNU General Public License
  version 2 or later; combined with Replayer they are used under GPL version
  3. The AppImage also bundles the libraries they depend on. Their copyright
  files are included in `usr/share/doc/<package>/copyright`, and their source
  code is available from Ubuntu with `apt-get source <package>` or at
  https://launchpad.net/ubuntu/+source/ffmpeg.

## Microsoft Visual C++ Runtime (Windows only)

The Windows package contains the Visual C++ runtime DLLs (`msvcp140*.dll`,
`vcruntime140*.dll`, `concrt140.dll`), which Microsoft allows to be
redistributed with applications built with Visual Studio.

## Note on video codecs

Video formats such as H.264 and H.265/HEVC may be covered by patents in some
countries. The licenses above do not grant any patent licenses for them.
