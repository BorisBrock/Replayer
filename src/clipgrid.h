#ifndef CLIPGRID_H
#define CLIPGRID_H

#include <QList>
#include <QWidget>

class ClipView;
class QGridLayout;

// Arranges one or more ClipViews: a single view fills the area, several are
// tiled.
class ClipGrid : public QWidget {
    Q_OBJECT

public:
    enum class Arrangement { Auto, SideBySide, Stacked };

    explicit ClipGrid(QWidget* parent = nullptr);

    // The views must be children of this widget.
    void setViews(const QList<ClipView*>& views);

    Arrangement arrangement() const { return m_arrangement; }
    void setArrangement(Arrangement arrangement);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout(bool force);
    int columnsFor(int count) const;

    QGridLayout*     m_layout;
    QList<ClipView*> m_views;
    Arrangement      m_arrangement = Arrangement::Auto;
    int              m_columns     = 0;
};

#endif
