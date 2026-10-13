#ifndef OVERLAY_H
#define OVERLAY_H

// How a second clip (the ghost) is shown on top of a clip, for an exact
// comparison of two takes. A line across the video, dragged by the user,
// sets the mix: at its left the first clip only, at its right the ghost only.
enum class OverlayMode {
    None,
    Blend, // both clips mixed; the line's position sets the ghost's opacity
    Split, // first clip before the line, ghost after it, both fully opaque
};

#endif
