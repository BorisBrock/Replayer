#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QIcon>
#include <QList>
#include <QString>

class QApplication;
class QPainter;
class QRectF;

// The app always uses its own dark theme, independent of the desktop's
// setting (native file dialogs still follow the desktop, see main.cpp).
namespace Theme {

inline constexpr const char* kDefaultAccent = "#3b8eea";

void apply(QApplication& app, const QColor& accent);

// Re-styles the running application; hover/selection shades are derived.
void setAccent(const QColor& accent);
QColor accent();

struct Preset {
    QString name;
    QColor  color;
};
QList<Preset> accentPresets();

// Round colour swatch for menus
QIcon swatch(const QColor& color);

enum class Icon {
    Play, Pause, Stop, StepBackward, StepForward, Loop,
    Close, LayoutGrid, LayoutSideBySide, LayoutStacked, Clip,
    Line, Arrow, Freehand, Circle, Ruler, Measure, Undo, Redo, Trash, MarkA, MarkB,
    GhostBlend, GhostSplit, GhostSplitHorizontal, ChevronDown, ChevronRight, Cut, Eye, EyeOff,
};

// Vector icons drawn at runtime, so they stay crisp at any scale factor.
QIcon icon(Icon which, bool muted = false); // muted: in the colour of secondary text
// Paints an icon directly, e.g. from an item delegate.
void drawIcon(QPainter& p, Icon which, const QRectF& rect, const QColor& color);

// Text colour on selected list items
QColor selectionText();

} // namespace Theme

#endif
