#include "theme.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTransform>

#include <functional>

namespace {

// Colour tokens shared by the palette and the stylesheet
const QColor kWindow("#1c1d21");      // app background
const QColor kSurface("#25272c");     // cards, transport bar
const QColor kSurfaceHover("#2f3238");
const QColor kBase("#16171a");        // lists, text fields
const QColor kBorder("#34373d");
const QColor kText("#e4e6eb");
const QColor kTextMuted("#9aa0aa");
const QColor kTextDisabled("#62666e");
const QColor kTrack("#3d4047");

QColor g_accent(Theme::kDefaultAccent);

// Shades derived from the user's accent colour
struct AccentColors {
    QColor accent, hover, selection;
    QColor onAccent, onSelection; // readable text on top of them
};

QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QColor textOn(const QColor& background) {
    // Relative luminance (sRGB approximation): dark text on light colours
    const double l = 0.2126 * background.redF() + 0.7152 * background.greenF()
                   + 0.0722 * background.blueF();
    return l > 0.6 ? QColor("#15161a") : QColor(Qt::white);
}

AccentColors accentColors(const QColor& accent) {
    AccentColors c;
    c.accent      = accent;
    c.hover       = mix(accent, Qt::white, 0.18);
    c.selection   = mix(accent, kSurface, 0.45);
    c.onAccent    = textOn(c.accent);
    c.onSelection = textOn(c.selection);
    return c;
}

QPalette darkPalette(const AccentColors& a) {
    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kBase);
    p.setColor(QPalette::AlternateBase, kSurface);
    p.setColor(QPalette::ToolTipBase, kSurface);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kTextMuted);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kSurface);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, kSurfaceHover);
    p.setColor(QPalette::Midlight, kBorder);
    p.setColor(QPalette::Mid, kBorder);
    p.setColor(QPalette::Dark, kBase);
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, a.accent);
    p.setColor(QPalette::HighlightedText, a.onAccent);
    p.setColor(QPalette::Link, a.hover);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, kTextDisabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, kTrack);
    return p;
}

QString styleSheet(const AccentColors& a) {
    QString qss = R"(
        QMainWindow, QSplitter { background: @window; }
        QSplitter::handle { background: @window; }
        QSplitter::handle:horizontal { width: 6px; }

        QToolTip {
            background: @surface; color: @text;
            border: 1px solid @border; padding: 4px 8px;
        }

        /* Buttons */
        QPushButton {
            background: @surface; border: 1px solid @border; border-radius: 8px;
            padding: 8px 16px; min-height: 18px;
        }
        QPushButton:hover { background: @surfaceHover; }
        QPushButton:pressed { background: @border; }
        QPushButton#PrimaryButton {
            background: @accent; border-color: @accent; color: @onAccent; font-weight: 600;
        }
        QPushButton#PrimaryButton:hover { background: @accentHover; border-color: @accentHover; }

        QToolButton {
            background: transparent; border: none; border-radius: 8px; padding: 6px;
        }
        QToolButton:hover { background: @surfaceHover; }
        QToolButton:pressed { background: @border; }
        QToolButton:checked { background: @selection; }
        /* Split button (click: default action, arrow: menu) */
        QToolButton[popupMode="1"] { padding-right: 18px; }
        QToolButton::menu-button { border: none; width: 16px; }

        QFrame#Separator { background: @border; border: none; min-height: 1px; max-height: 1px; }

        /* Sliders: thin track, large round handle, tall hit area */
        QSlider:horizontal { min-height: 28px; }
        QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: @track; }
        QSlider::sub-page:horizontal { border-radius: 3px; background: @accent; }
        QSlider::add-page:horizontal { border-radius: 3px; background: @track; }
        QSlider::handle:horizontal {
            width: 18px; height: 18px; margin: -6px 0; border-radius: 9px;
            background: white; border: 2px solid @accent;
        }
        QSlider::handle:horizontal:hover { border-color: @accentHover; background: #eef4fd; }
        QSlider::sub-page:horizontal:disabled { background: @track; }
        QSlider::handle:horizontal:disabled { background: @textDisabled; border-color: @textDisabled; }

        /* Clip list (parts of split clips are indented below their clip) */
        QTreeWidget#ClipList {
            background: @base; border: 1px solid @border; border-radius: 10px;
            padding: 4px; outline: none;
        }
        QTreeWidget#ClipList::item { padding: 8px 10px; border-radius: 6px; margin: 1px 0; }
        QTreeWidget#ClipList::item:hover { background: @surface; }
        QTreeWidget#ClipList::item:selected { background: @selection; color: @onSelection; }

        /* Collapsible side panel cards: the header above takes the title's place */
        QGroupBox {
            background: @surface; border: none; border-radius: 10px;
            margin-top: 0; padding: 14px 12px 12px 12px;
        }
        QToolButton#SectionHeader {
            color: @textMuted; font-weight: 600; padding: 4px 6px 4px 0; margin: 4px 0 2px 0;
        }
        QToolButton#SectionHeader:hover { background: transparent; color: @text; }
        QToolButton#SectionHeader:checked { background: transparent; }

        QLabel#MutedLabel { color: @textMuted; }
        QLabel#TitleLabel { font-weight: 600; font-size: 11pt; }
        QWidget#TransportBar { background: @surface; }

        /* Comparison tiles; the current one is outlined in the accent colour */
        QFrame#ClipTile { background: @surface; border: 2px solid @surface; border-radius: 10px; }
        QFrame#ClipTile[current="true"] { border-color: @accent; }
        QFrame#ClipTile[compact="false"] { background: @surface; border: none; border-radius: 0; }
        QFrame#ClipTile QWidget#TransportBar { background: transparent; }
        QLabel#TileTitle { font-weight: 600; }
        QWidget#CompareBar { background: @window; }
        QScrollArea#SidePanel, QScrollArea#SidePanel > QWidget > QWidget { background: @window; }

        /* Menus */
        QMenuBar { background: @window; padding: 2px 4px; }
        QMenuBar::item { padding: 6px 10px; border-radius: 6px; background: transparent; }
        QMenuBar::item:selected { background: @surface; }
        QMenu {
            background: @surface; border: 1px solid @border; border-radius: 8px; padding: 6px;
        }
        QMenu::item { padding: 7px 28px 7px 14px; border-radius: 6px; }
        QMenu::icon { padding-left: 10px; }
        QMenu::item:checked { background: @selection; color: @onSelection; }
        QMenu::item:selected { background: @accent; color: @onAccent; }
        QMenu::item:disabled { color: @textDisabled; }
        QMenu::separator { height: 1px; background: @border; margin: 6px 8px; }

        /* Slim scroll bars */
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle { background: @track; border-radius: 3px; }
        QScrollBar::handle:hover { background: #50545c; }
        QScrollBar::handle:vertical { min-height: 32px; }
        QScrollBar::handle:horizontal { min-width: 32px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }

        QStatusBar { background: @window; color: @textMuted; }
        QStatusBar::item { border: none; }
    )";

    // Longest names first so "@surfaceHover" isn't clobbered by "@surface".
    const std::pair<const char*, QColor> tokens[] = {
        {"@surfaceHover", kSurfaceHover}, {"@textDisabled", kTextDisabled},
        {"@onSelection", a.onSelection},  {"@accentHover", a.hover},
        {"@onAccent", a.onAccent},        {"@textMuted", kTextMuted},
        {"@selection", a.selection},      {"@surface", kSurface},
        {"@window", kWindow},             {"@border", kBorder},
        {"@accent", a.accent},            {"@track", kTrack},
        {"@base", kBase},                 {"@text", kText},
    };
    for (const auto& [name, color] : tokens) qss.replace(name, color.name());
    return qss;
}

// A little ghost for the overlay modes: round head, wavy hem, two eyes
// (holes, by the odd-even fill)
QPainterPath ghostPath(bool withEyes) {
    QPainterPath ghost;
    ghost.moveTo(4.5, 20.5);
    ghost.lineTo(4.5, 11);
    ghost.arcTo(QRectF(4.5, 3.5, 15, 15), 180, -180);
    ghost.lineTo(19.5, 20.5);
    for (int i = 0; i < 3; i++) {
        const double x = 19.5 - 5.0 * i;
        ghost.quadTo(x - 2.5, 16.5, x - 5.0, 20.5);
    }
    ghost.closeSubpath();
    if (withEyes) {
        ghost.addEllipse(QPointF(9.5, 11), 1.5, 1.8);
        ghost.addEllipse(QPointF(14.5, 11), 1.5, 1.8);
    }
    return ghost;
}

// Draws one icon on a 24x24 design grid.
void paintIcon(QPainter& p, Theme::Icon which) {
    QPainterPath path;
    // Outlines instead of filled shapes, for line-art icons
    auto stroke = [&p](const QPainterPath& outline, double width = 2.0) {
        p.strokePath(outline, QPen(p.brush().color(), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    };
    switch (which) {
    case Theme::Icon::Play:
        path.moveTo(8, 5); path.lineTo(19, 12); path.lineTo(8, 19); path.closeSubpath();
        break;
    case Theme::Icon::Pause:
        path.addRoundedRect(QRectF(6.5, 5, 4, 14), 1.2, 1.2);
        path.addRoundedRect(QRectF(13.5, 5, 4, 14), 1.2, 1.2);
        break;
    case Theme::Icon::Stop:
        path.addRoundedRect(QRectF(6.5, 6.5, 11, 11), 2, 2);
        break;
    case Theme::Icon::StepForward:
        path.moveTo(6, 6.5); path.lineTo(14.5, 12); path.lineTo(6, 17.5); path.closeSubpath();
        path.addRoundedRect(QRectF(15.5, 6.5, 2.6, 11), 1, 1);
        break;
    case Theme::Icon::StepBackward:
        path.moveTo(18, 6.5); path.lineTo(9.5, 12); path.lineTo(18, 17.5); path.closeSubpath();
        path.addRoundedRect(QRectF(5.9, 6.5, 2.6, 11), 1, 1);
        break;
    case Theme::Icon::Close:
        p.strokePath([] {
            QPainterPath x;
            x.moveTo(7, 7); x.lineTo(17, 17);
            x.moveTo(17, 7); x.lineTo(7, 17);
            return x;
        }(), QPen(p.brush().color(), 2.2, Qt::SolidLine, Qt::RoundCap));
        break;
    case Theme::Icon::LayoutGrid:
        for (double x : {4.0, 13.0})
            for (double y : {4.0, 13.0}) path.addRoundedRect(QRectF(x, y, 7, 7), 1.5, 1.5);
        break;
    case Theme::Icon::LayoutSideBySide:
        path.addRoundedRect(QRectF(4, 5, 7, 14), 1.5, 1.5);
        path.addRoundedRect(QRectF(13, 5, 7, 14), 1.5, 1.5);
        break;
    case Theme::Icon::LayoutStacked:
        path.addRoundedRect(QRectF(4, 4, 16, 7), 1.5, 1.5);
        path.addRoundedRect(QRectF(4, 13, 16, 7), 1.5, 1.5);
        break;
    case Theme::Icon::Clip:
        // Video frame with a play symbol cut out (odd-even fill)
        path.addRoundedRect(QRectF(3, 5, 18, 14), 3, 3);
        path.moveTo(10, 8.8); path.lineTo(15.5, 12); path.lineTo(10, 15.2); path.closeSubpath();
        break;
    case Theme::Icon::Loop: {
        // "Repeat": two rounded strokes with arrowheads pointing round the loop
        QPainterPath strokes;
        strokes.moveTo(5, 12.5);
        strokes.lineTo(5, 10);
        strokes.quadTo(5, 7, 8, 7);
        strokes.lineTo(16.5, 7);
        strokes.moveTo(19, 11.5);
        strokes.lineTo(19, 14);
        strokes.quadTo(19, 17, 16, 17);
        strokes.lineTo(7.5, 17);
        p.strokePath(strokes, QPen(p.brush().color(), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        path.moveTo(20, 7); path.lineTo(16, 3.8); path.lineTo(16, 10.2); path.closeSubpath();
        path.moveTo(4, 17); path.lineTo(8, 13.8); path.lineTo(8, 20.2); path.closeSubpath();
        break;
    }
    case Theme::Icon::Line:
        stroke([] {
            QPainterPath s;
            s.moveTo(5, 19); s.lineTo(19, 5);
            return s;
        }());
        break;
    case Theme::Icon::Arrow:
        stroke([] {
            QPainterPath s;
            s.moveTo(5, 19); s.lineTo(15, 9);
            return s;
        }());
        path.moveTo(19.5, 4.5); path.lineTo(17.8, 12.3); path.lineTo(11.7, 6.2); path.closeSubpath();
        break;
    case Theme::Icon::Freehand:
        stroke([] {
            QPainterPath s;
            s.moveTo(4, 17);
            s.cubicTo(6, 6, 10, 5, 12, 12);
            s.cubicTo(14, 19, 18, 18, 20, 7);
            return s;
        }());
        break;
    case Theme::Icon::Circle:
        stroke([] {
            QPainterPath s;
            s.addEllipse(QRectF(5, 5, 14, 14));
            return s;
        }());
        break;
    case Theme::Icon::Ruler:
        // A ruler with tick marks: sets the reference length
        stroke([] {
            QPainterPath s;
            s.addRoundedRect(QRectF(3, 8, 18, 8), 1.5, 1.5);
            for (double x : {7.0, 11.0, 15.0}) {
                s.moveTo(x, 8); s.lineTo(x, x == 11.0 ? 12.5 : 11);
            }
            return s;
        }(), 1.6);
        break;
    case Theme::Icon::Measure:
        // A dimension line: |<->|
        stroke([] {
            QPainterPath s;
            s.moveTo(4, 7); s.lineTo(4, 17);
            s.moveTo(20, 7); s.lineTo(20, 17);
            s.moveTo(7, 12); s.lineTo(17, 12);
            return s;
        }());
        path.moveTo(5.5, 12); path.lineTo(9, 9.3); path.lineTo(9, 14.7); path.closeSubpath();
        path.moveTo(18.5, 12); path.lineTo(15, 9.3); path.lineTo(15, 14.7); path.closeSubpath();
        break;
    case Theme::Icon::ChevronDown:
    case Theme::Icon::ChevronRight: {
        QPainterPath s;
        if (which == Theme::Icon::ChevronDown) {
            s.moveTo(6, 9); s.lineTo(12, 15); s.lineTo(18, 9);
        } else {
            s.moveTo(9, 6); s.lineTo(15, 12); s.lineTo(9, 18);
        }
        stroke(s, 2.5);
        break;
    }
    case Theme::Icon::Undo:
    case Theme::Icon::Redo: {
        // A curved arrow back to the left; redo is its mirror image
        QPainterPath s;
        s.moveTo(8, 9); s.lineTo(14, 9);
        s.cubicTo(20.5, 9, 20.5, 19, 14, 19);
        s.lineTo(9, 19);
        QPainterPath head;
        head.moveTo(3.5, 9); head.lineTo(9, 4.8); head.lineTo(9, 13.2); head.closeSubpath();
        const QTransform mirror = which == Theme::Icon::Redo ? QTransform(-1, 0, 0, 1, 24, 0) : QTransform();
        stroke(mirror.map(s));
        path = mirror.map(head);
        break;
    }
    case Theme::Icon::Trash:
        stroke([] {
            QPainterPath s;
            s.moveTo(4.5, 7); s.lineTo(19.5, 7);
            s.moveTo(9.5, 7); s.lineTo(10, 4.5); s.lineTo(14, 4.5); s.lineTo(14.5, 7);
            s.moveTo(6.5, 7); s.lineTo(7.5, 19.5); s.lineTo(16.5, 19.5); s.lineTo(17.5, 7);
            s.moveTo(10.5, 10.5); s.lineTo(10.5, 16);
            s.moveTo(13.5, 10.5); s.lineTo(13.5, 16);
            return s;
        }(), 1.8);
        break;
    case Theme::Icon::Cut:
        // Scissors: two finger rings, blades crossing above them
        stroke([] {
            QPainterPath s;
            s.addEllipse(QPointF(7, 17.5), 3, 3);
            s.addEllipse(QPointF(17, 17.5), 3, 3);
            s.moveTo(9, 15.3); s.lineTo(17.5, 4);
            s.moveTo(15, 15.3); s.lineTo(6.5, 4);
            return s;
        }(), 1.8);
        break;
    case Theme::Icon::Eye:
    case Theme::Icon::EyeOff:
        // Almond outline with the iris; struck through when off
        stroke([which] {
            QPainterPath s;
            s.moveTo(3, 12);
            s.quadTo(12, 3, 21, 12);
            s.quadTo(12, 21, 3, 12);
            s.addEllipse(QPointF(12, 12), 3, 3);
            if (which == Theme::Icon::EyeOff) { s.moveTo(5, 19); s.lineTo(19, 5); }
            return s;
        }(), 1.8);
        break;
    case Theme::Icon::MarkA:
    case Theme::Icon::MarkB: {
        // A marker ring with its letter
        stroke([] {
            QPainterPath s;
            s.addEllipse(QRectF(3, 3, 18, 18));
            return s;
        }(), 1.8);
        QFont font;
        font.setPixelSize(12);
        font.setBold(true);
        path.addText(0, 0, font, which == Theme::Icon::MarkA ? "A" : "B");
        path.translate(QPointF(12, 12) - path.boundingRect().center());
        break;
    }
    case Theme::Icon::GhostBlend: {
        // Half solid, half a dotted outline: the two clips mixed
        const QColor color = p.brush().color();
        p.save();
        p.setClipRect(QRectF(0, 0, 12, 24));
        p.setPen(Qt::NoPen);
        p.drawPath(ghostPath(true));
        p.restore();
        p.save();
        p.setClipRect(QRectF(12, 0, 12, 24));
        QPen dots(color, 1.7, Qt::CustomDashLine, Qt::RoundCap);
        dots.setDashPattern({0.01, 1.9});
        p.strokePath(ghostPath(false), dots);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(14.5, 11), 1.1, 1.3);
        p.restore();
        break;
    }
    case Theme::Icon::GhostSplit:
    case Theme::Icon::GhostSplitHorizontal: {
        // The ghost cut in two, with the split line running through the gap
        const bool horizontal = which == Theme::Icon::GhostSplitHorizontal;
        const QRectF gap = horizontal ? QRectF(0, 14.3, 24, 2.6) : QRectF(10.7, 0, 2.6, 24);
        QPainterPath body;
        body.addRect(QRectF(0, 0, 24, 24));
        body = body.subtracted([&] {
            QPainterPath g;
            g.addRect(gap);
            return g;
        }());
        p.save();
        p.setClipPath(body);
        p.setPen(Qt::NoPen);
        p.drawPath(ghostPath(true));
        p.restore();
        QPainterPath split;
        if (horizontal) {
            split.moveTo(1.5, gap.center().y()); split.lineTo(22.5, gap.center().y());
        } else {
            split.moveTo(12, 1.5); split.lineTo(12, 22.5);
        }
        p.strokePath(split, QPen(p.brush().color(), 1.1, Qt::SolidLine, Qt::RoundCap));
        break;
    }
    }
    p.setPen(QPen(p.brush().color(), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

// Renders a vector icon at exactly the device pixel size it is shown at.
// Pre-rendered pixmaps get rescaled by Qt whenever the icon size or the
// screen's scale factor (e.g. 125 %, 150 %) doesn't match, which blurs them.
class VectorIconEngine : public QIconEngine {
public:
    // Paints the icon into a rect of the given logical size
    using Painter = std::function<void(QPainter&, const QSizeF&, QIcon::Mode)>;

    explicit VectorIconEngine(Painter painter) : m_painter(std::move(painter)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal scale = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    // size is in logical pixels (Qt 6.8)
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        QPixmap pixmap(QSize(qRound(size.width() * scale), qRound(size.height() * scale)));
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        m_painter(p, pixmap.deviceIndependentSize(), mode);
        return pixmap;
    }

    QIconEngine* clone() const override { return new VectorIconEngine(m_painter); }

private:
    Painter m_painter;
};

} // namespace

namespace Theme {

void apply(QApplication& app, const QColor& accent) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Tells the platform too, e.g. for a dark title bar on Windows
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    app.setStyle(QStyleFactory::create("Fusion"));
    setAccent(accent);
}

void setAccent(const QColor& accent) {
    g_accent = accent.isValid() ? accent : QColor(kDefaultAccent);
    const AccentColors colors = accentColors(g_accent);
    qApp->setPalette(darkPalette(colors));
    qApp->setStyleSheet(styleSheet(colors));
}

QColor accent() {
    return g_accent;
}

QList<Preset> accentPresets() {
    return {
        {"Blue",   QColor(kDefaultAccent)},
        {"Teal",   QColor("#14b8a6")},
        {"Green",  QColor("#22c55e")},
        {"Yellow", QColor("#eab308")},
        {"Orange", QColor("#f97316")},
        {"Red",    QColor("#ef4444")},
        {"Pink",   QColor("#ec4899")},
        {"Purple", QColor("#8b5cf6")},
    };
}

QIcon swatch(const QColor& color) {
    return QIcon(new VectorIconEngine([color](QPainter& p, const QSizeF& size, QIcon::Mode) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(QPointF(1, 1), size - QSizeF(2, 2)));
    }));
}

void drawIcon(QPainter& p, Icon which, const QRectF& rect, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 24.0, rect.height() / 24.0);
    p.setBrush(color);
    paintIcon(p, which);
    p.restore();
}

QColor selectionText() {
    return accentColors(g_accent).onSelection;
}

QIcon icon(Icon which, bool muted) {
    return QIcon(new VectorIconEngine([which, muted](QPainter& p, const QSizeF& size, QIcon::Mode mode) {
        const QColor color = mode == QIcon::Active   ? QColor(Qt::white)
                           : mode == QIcon::Disabled ? kTextDisabled
                           : muted                   ? kTextMuted
                                                     : kText;
        drawIcon(p, which, QRectF(QPointF(0, 0), size), color);
    }));
}

} // namespace Theme
