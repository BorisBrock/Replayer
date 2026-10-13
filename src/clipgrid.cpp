#include "clipgrid.h"
#include "clipview.h"
#include "playercontroller.h"

#include <QGridLayout>
#include <cmath>

namespace {

// Space a compact tile needs besides the video: header and transport bar.
constexpr int kTileChromeHeight = 96;

} // namespace

ClipGrid::ClipGrid(QWidget* parent) : QWidget(parent) {
    m_layout = new QGridLayout(this);
    m_layout->setSpacing(6);
    m_layout->setContentsMargins(0, 0, 0, 0);
}

void ClipGrid::setViews(const QList<ClipView*>& views) {
    m_views = views;
    const bool tiled = views.size() > 1;
    m_layout->setContentsMargins(tiled ? QMargins(6, 0, 6, 6) : QMargins());
    relayout(true);
}

void ClipGrid::setArrangement(Arrangement arrangement) {
    m_arrangement = arrangement;
    relayout(true);
}

void ClipGrid::setOverlay(bool overlay) {
    if (overlay == m_overlay) return;
    m_overlay = overlay;
    relayout(true);
}

void ClipGrid::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayout(false);
}

int ClipGrid::columnsFor(int count) const {
    if (count <= 1 || m_overlay) return 1;
    switch (m_arrangement) {
    case Arrangement::SideBySide: return count;
    case Arrangement::Stacked:    return 1;
    case Arrangement::Auto:       break;
    }

    // Pick the column count that makes the videos largest. The first clip's
    // aspect ratio stands in for all; action cam clips usually share it.
    double aspect = 16.0 / 9.0;
    const VideoInfo& info = m_views.first()->controller()->videoInfo();
    if (info.width > 0 && info.height > 0) {
        aspect = static_cast<double>(info.width) / info.height;
        if (info.rotation == 90 || info.rotation == 270) aspect = 1.0 / aspect;
    }

    int best = 1;
    double bestHeight = -1.0;
    for (int cols = 1; cols <= count; cols++) {
        const int rows = (count + cols - 1) / cols;
        const double cellWidth  = static_cast<double>(width()) / cols;
        const double cellHeight = static_cast<double>(height()) / rows - kTileChromeHeight;
        const double videoHeight = std::min(cellWidth / aspect, cellHeight);
        if (videoHeight > bestHeight + 0.5) {
            bestHeight = videoHeight;
            best = cols;
        }
    }
    return best;
}

void ClipGrid::relayout(bool force) {
    const int columns = columnsFor(m_views.size());
    if (!force && columns == m_columns) return;
    m_columns = columns;

    // Moving widgets between cells keeps their parent (and GL context).
    while (m_layout->count() > 0) delete m_layout->takeAt(0);
    for (int i = 0; i < m_layout->rowCount(); i++) m_layout->setRowStretch(i, 0);
    for (int i = 0; i < m_layout->columnCount(); i++) m_layout->setColumnStretch(i, 0);

    for (int i = 0; i < m_views.size(); i++) {
        m_layout->addWidget(m_views[i], i / columns, i % columns);
        m_views[i]->show();
    }
    const int rows = (static_cast<int>(m_views.size()) + columns - 1) / std::max(1, columns);
    for (int r = 0; r < rows; r++) m_layout->setRowStretch(r, m_overlay && r > 0 ? 0 : 1);
    for (int c = 0; c < columns; c++) m_layout->setColumnStretch(c, 1);
}
