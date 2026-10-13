#ifndef CLIPGRID_H
#define CLIPGRID_H

#include <QList>
#include <QWidget>

class ClipView;
class QGridLayout;

// Arranges one or more ClipViews: a single view fills the area, several are
// tiled. In overlay mode the first view (showing both clips) takes the room,
// and the second one, reduced to its controls, sits below it.
class ClipGrid : public QWidget {
    Q_OBJECT

public:
    enum class Arrangement { Auto, SideBySide, Stacked };

    explicit ClipGrid(QWidget* parent = nullptr);

    // The views must be children of this widget.
    void setViews(const QList<ClipView*>& views);

    Arrangement arrangement() const { return m_arrangement; }
    void setArrangement(Arrangement arrangement);
    // Overrides the arrangement while on
    void setOverlay(bool overlay);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout(bool force);
    int columnsFor(int count) const;

    QGridLayout*     m_layout;
    QList<ClipView*> m_views;
    Arrangement      m_arrangement = Arrangement::Auto;
    bool             m_overlay     = false;
    int              m_columns     = 0;
};

#endif
