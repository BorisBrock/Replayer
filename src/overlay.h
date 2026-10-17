#ifndef OVERLAY_H
#define OVERLAY_H

// How a second clip (the ghost) is shown on top of a clip, for an exact
// comparison of two takes. A line across the video, dragged by the user,
// sets the mix: the ghost's opacity when blending, or where the first clip
// ends and the ghost begins when split (upright, or turned to run across).
enum class OverlayMode {
    None,
    Blend, // both clips mixed; the line's position sets the ghost's opacity
    Split, // first clip before the line, ghost after it, both fully opaque
};

#endif
