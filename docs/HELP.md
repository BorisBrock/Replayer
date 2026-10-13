# Replayer user guide

This guide explains every part of Replayer and how to use it. For a quick
overview of what Replayer can do, see the [README](../README.md).

- [The main window](#the-main-window)
- [Opening clips](#opening-clips)
- [Playing a clip](#playing-a-clip)
- [Slow motion](#slow-motion)
- [Comparing clips](#comparing-clips)
- [Zoom and pan](#zoom-and-pan)
- [Drawing](#drawing)
- [Measuring distances](#measuring-distances)
- [Measuring speed](#measuring-speed)
- [Exporting a frame](#exporting-a-frame)
- [Full screen](#full-screen)
- [Settings](#settings)
- [Keyboard and mouse](#keyboard-and-mouse)
- [Troubleshooting](#troubleshooting)

---

## The main window

![Replayer main window](images/main-window.png)

The window has three columns:

| Column     | Contents                                                                 |
|------------|--------------------------------------------------------------------------|
| Left       | The **clip list**: the videos in the open folder, with their durations   |
| Centre     | The **video** with its seek bar and playback buttons; several when comparing |
| Right      | The **side panel**: video info, slow motion, zoom, drawing and measurement |

Drag the borders between the columns to resize them. The side panel scrolls
when the window is too low to show all of it.

Messages such as errors appear for a few seconds in the status bar at the
bottom of the window.

---

## Opening clips

There are several ways to open videos:

- **Open a folder** with *File → Open Folder...* (Ctrl+O) or the *Open
  Folder...* button above the clip list. All videos in the folder are listed.
- **Open a single file** with *File → Open File...* (Ctrl+Shift+O). Replayer
  opens the file's folder and selects the file.
- **Drag and drop** a folder or a video file onto the window.
- **From the command line:** `replayer <folder or file>`.

Replayer lists files ending in `.mp4`, `.m4v`, `.mov`, `.mkv`, `.avi`, `.ts`,
`.mts`, `.m2ts`, `.webm`, `.lrv` and `.360`. Any codec FFmpeg can decode
plays, e.g. H.264, H.265/HEVC, AV1 and VP9.

### The clip list

- Clips are sorted by name in natural order (`clip2` before `clip10`), which
  for action cams is recording order.
- **GoPro chapters stay together:** a long GoPro recording is split into
  chapters like `GX010123`, `GX020123`, `GX030123`. Replayer lists them one
  after the other, instead of mixed with other recordings.
- Each clip shows its duration. A `-` means the file cannot be read.
- The list updates by itself when files are added to or removed from the
  folder, e.g. while copying from the camera.
- Hover over a clip to see its full path.

Click a clip to play it, or use the Down and Up arrow keys (or Page Down and
Page Up) to go to the next or previous clip.

### Picking up where you left off

- **Each clip remembers its position** while Replayer is running. Switch to
  another clip and back, and it continues where you left it.
- **The session is restored on start:** when you start Replayer without a
  folder or file, it reopens the last folder with the same clips selected,
  paused.

---

## Playing a clip

Below the video is the **transport bar**:

| Control             | Function                                         | Key       |
|---------------------|--------------------------------------------------|-----------|
| Previous frame      | Step one frame back (pauses playback)            | `,`       |
| Play / Pause        | Start or pause playback                          | Space     |
| Stop                | Pause and rewind to the start                    | S         |
| Next frame          | Step one frame forward (pauses playback)         | `.`       |
| Loop                | Start again from the beginning at the end        | L         |
| Seek bar            | Click anywhere to jump there, or drag to scrub   |           |

The transport bar also shows the current time and duration and the frame
number, e.g. *Frame 312 / 1800*.

With the keyboard you can also jump:

| Key                | Function                 |
|--------------------|--------------------------|
| Right / Left       | 1 second forward / back  |
| Shift+Right / Left | 5 seconds forward / back |

All playback commands are also in the *Playback* menu.

### Video info

The **VIDEO** card in the side panel shows the clip's file name, codec and
decoder, resolution, frame rate and duration. The decoder tells you whether
the graphics card decodes the video (e.g. `d3d11va`, `vaapi`, `cuda`) or the
CPU does (`software`).

---

## Slow motion

The **SLOW MOTION** slider in the side panel slows playback down from real
time (1.0x) to 20 times slower (20.0x), in steps of 0.1.

The label below the slider shows the factor and how many frames per second
you then see, e.g. *Factor 4.0x, 60 FPS* for a 240 fps clip. A good rule:
divide the clip's frame rate by the speed you want to see. For a 240 fps clip,
factor 8.0x shows 30 frames per second – every frame, smooth to the eye.

The slow-motion factor applies to all clips, so compared clips always run at
the same speed.

---

## Comparing clips

![Several clips compared side by side](images/split-screen.png)

Select several clips in the clip list to show them side by side:

- **Ctrl+click** adds a clip to the selection or removes it.
- **Shift+click** selects all clips between the current one and the clicked
  one.

Up to **six clips** can be compared at once. If you select more, the first six
are shown.

Each tile has a title bar with the clip's name and an **×** button that removes
the clip from the comparison. The tile you clicked last is outlined; the side
panel shows its info, and drawing, measuring and Ctrl+Z act on it.

### Lining clips up

Each tile has its own transport bar. Use it to bring every clip to the key
moment, e.g. the take-off of a jump: drag its seek bar, then step frame by
frame with its own buttons until the clips match.

### The comparison bar

When comparing, a bar above the clips acts on all of them together:

| Control             | Function                                                       |
|---------------------|----------------------------------------------------------------|
| Play all            | Plays or pauses all clips together (also Space)                |
| Sync seek bar       | Moves all clips by the same amount, keeping their offsets      |
| Grid                | Arranges the clips automatically to use the space best         |
| Side by side        | Puts all clips in one row                                      |
| Stacked             | Puts all clips in one column                                   |

The sync seek bar follows the outlined clip. It stops where any clip would run
past its start or end, so all clips stay in sync.

All keyboard shortcuts for playback (Space, S, L, `.`, `,`, the arrow keys)
act on **all** clips at once. Line the clips up first, then step through the
action together.

---

## Zoom and pan

![Zooming in on the video](images/zoom-pan.png)

Zoom in up to 8x:

- **Mouse wheel** over the video zooms in or out around the mouse pointer.
- **`+` / `-`** zoom in or out around the centre of the view; **`0`** shows
  the whole frame again.
- The **ZOOM** card in the side panel has a zoom slider and a *Reset* button.

When zoomed in, **drag the video** with the left mouse button to move around.
While a drawing tool is active, drag with the **middle mouse button** instead.

The **navigator** in the ZOOM card shows the whole frame with a rectangle
around the part you see. Click or drag in it to move that rectangle, or use
the mouse wheel on it to zoom.

Zoom is shared by all compared clips, since they usually come from the same
camera position.

---

## Drawing

![Drawing on a clip](images/drawing.png)

The **DRAWING** card in the side panel holds the tools to mark things on the
video:

| Tool      | How to draw                                                |
|-----------|------------------------------------------------------------|
| Line      | Drag from start to end                                     |
| Arrow     | Drag from the tail to the tip                              |
| Freehand  | Drag along the path you want                               |
| Circle    | Drag from the centre outwards                              |

Click a tool to pick it, and click it again (or press Esc) to put it away.
While no tool is picked, the mouse pans the video again.

Below the tools:

- **Colour:** red, yellow, green, blue, pink or white.
- **Line width:** the slider sets the width of new lines.
- **Undo** (Ctrl+Z), **Redo** (Ctrl+Y or Ctrl+Shift+Z) and **Clear**
  (Ctrl+Delete), which removes all drawings and measurements from the current
  clip.

Good to know:

- Each clip has its **own drawings**. They stay when you switch to another
  clip and back, but are not saved when Replayer closes.
- Drawings are attached to the video, not the window: they **follow zoom and
  pan** and keep their place on the picture.
- Drawings are not tied to a frame. They stay visible while the clip plays.

---

## Measuring distances

![Measuring a height](images/measure.png)

The **MEASUREMENT** card measures real lengths in the video. Measuring works
in two steps:

1. **Draw a reference line.** Pick the *Reference* tool and draw a line along
   something whose length you know: a wheel, a ramp, a measuring tape, a door.
   Replayer asks for its real length. Type a number with a unit, e.g.
   `1.25 m`, `80 cm`, `29 in` or `6 ft`. Without a unit, metres are used.
2. **Measure.** Pick the *Measure* tool and draw lines. Each one shows its real
   length, in the unit of the reference.

Each clip has one reference line. Drawing a new one replaces the old one. The
card shows the current reference, e.g. *Reference: 1.25 m*. Without a
reference, measuring lines show their length in video pixels.

Units Replayer understands (and can convert to speeds): `m`, `cm`, `mm`,
`km`, `in`, `ft`, `yd`, also written out (`metres`, `inches`, `feet`, ...).
Any other unit, e.g. `bike lengths`, works for measuring too.

**Tips for accurate results:**

- The reference must be about as far from the camera as the thing you measure.
  Something twice as far away looks half as long.
- Lengths are measured in the picture plane. Lines pointing towards or away
  from the camera appear shorter than they are.
- Wide-angle lenses (as on most action cams) bend the picture towards the
  edges. Measure near the centre of the frame where you can.
- Zoom in to place line ends more precisely.

---

## Measuring speed

Replayer computes a speed from the distance something moves between two
frames:

1. Draw a **reference line** first (see above).
2. Go to the first frame, pick the **marker A** tool and click a point that is
   easy to find again, e.g. the hub of the front wheel.
3. Step forward a few frames with `.`, pick the **marker B** tool and click
   the same point again.

The MEASUREMENT card shows the speed in km/h or mph, and below it the distance,
the time and the number of frames between the markers, e.g. *2.35 m in 0.083 s
(20 frames)*. Each marker shows its letter and the time of its frame on the
video.

Setting a marker again moves it. If the reference unit is not one Replayer
knows, the speed is shown per second in that unit, e.g. *3.20 bike lengths/s*.

Choose between km/h and mph in *View → Units*.

**Tips for accurate results:**

- Use clips with a high frame rate and, if possible, a fast shutter, so the
  point is sharp in both frames.
- Leave enough frames between the markers so the movement is clearly larger
  than your clicking error, but not so many that the speed changes much.
- The same rules as for measuring distances apply: the movement should be
  across the picture, at the distance of the reference.

---

## Exporting a frame

Right-click the video and choose **Export frame as .png...** to save the frame
under the mouse pointer at full video resolution, including its drawings and
measurements. Replayer suggests a file name with the clip name and time, e.g.
`GX010123_00-12.345.png`, in the clip's folder.

---

## Full screen

Press **F** or **F11**, double-click the video, or use *View → Full Screen*
for full screen. The clip list, side panel and menu bar are hidden; all
keyboard shortcuts keep working. Press **Esc**, F or F11 to go back.

While a drawing tool is picked, double-clicks draw instead of switching to
full screen.

---

## Settings

Replayer remembers these settings between sessions:

| Setting             | Where                                                    |
|---------------------|----------------------------------------------------------|
| Accent colour       | *View → Accent Color*: a preset, or *Custom...* for any colour |
| Speed units         | *View → Units*: metric (km/h) or imperial (mph)          |
| Clip arrangement    | Comparison bar: grid, side by side or stacked            |
| Last folder, clips  | Restored on start                                        |

![Selectable accent colours](images/accent-colors.png)

---

## Keyboard and mouse

### Keyboard

| Key                     | Action                                     |
|-------------------------|--------------------------------------------|
| Ctrl+O                  | Open folder                                |
| Ctrl+Shift+O            | Open a single file (and its folder)        |
| Ctrl+Q                  | Quit                                       |
| Down / Up, PgDn / PgUp  | Next / previous clip                       |
| Space                   | Play / pause                               |
| S                       | Stop (rewind to start)                     |
| L                       | Loop clip on / off                         |
| `.` / `,`               | Next / previous frame                      |
| Right / Left            | Seek forward / back 1 s                    |
| Shift+Right / Left      | Seek forward / back 5 s                    |
| `+` (or `=`) / `-`      | Zoom in / out                              |
| `0`                     | Reset zoom                                 |
| F, F11                  | Toggle full screen                         |
| Esc                     | Leave full screen, or put the drawing tool away |
| Ctrl+Z                  | Undo drawing                               |
| Ctrl+Y, Ctrl+Shift+Z    | Redo drawing                               |
| Ctrl+Delete             | Clear drawings of the current clip         |

When comparing clips, the playback keys act on all of them at once.

### Mouse

| Where              | Action                         | Function                               |
|--------------------|--------------------------------|----------------------------------------|
| Clip list          | Click                          | Play that clip                         |
| Clip list          | Ctrl+click, Shift+click        | Select clips to compare                |
| Video              | Mouse wheel                    | Zoom in / out at the pointer           |
| Video              | Drag (left button)             | Pan when zoomed in (no tool picked)    |
| Video              | Drag (middle button)           | Pan when zoomed in (any time)          |
| Video              | Drag with a tool picked        | Draw or measure                        |
| Video              | Double-click                   | Toggle full screen (no tool picked)    |
| Video              | Right-click                    | Export the frame as .png               |
| Seek bar           | Click or drag                  | Jump to that position                  |
| Zoom navigator     | Click, drag or mouse wheel     | Move the zoomed region, zoom           |
| Window             | Drop a folder or file          | Open it                                |

---

## Troubleshooting

**A clip shows `-` instead of a duration, or does not play.**
The file is damaged or incomplete (e.g. still being copied), or uses a format
FFmpeg cannot read. The status bar shows the error.

**Playback stutters with large clips.**
Check the decoder in the VIDEO card. If it says `software`, the graphics card
does not decode that codec, or its driver doesn't support it. Updating the
graphics driver may help. Comparing several 4K clips at once also needs a lot
of decoding power.

**Choosing the decoder.**
Set the environment variable `REPLAYER_HWDEC` before starting Replayer:
`none` forces software decoding, and a name such as `d3d11va`, `cuda`,
`vaapi` or `videotoolbox` selects that hardware decoder.

**Something else?**
Please open an issue on [GitHub](https://github.com/BorisBrock/Replayer/issues).
