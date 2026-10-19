#ifndef COLLAPSIBLESECTION_H
#define COLLAPSIBLESECTION_H

#include <QString>
#include <QWidget>

class QGroupBox;
class QToolButton;

// A card of the side panel with a header that folds it away. Whether it is
// open is remembered across sessions, under settingsKey.
class CollapsibleSection : public QWidget {
    Q_OBJECT

public:
    CollapsibleSection(const QString& title, const QString& settingsKey, bool expandedByDefault,
                       QWidget* parent = nullptr);

    QGroupBox* body() const { return m_body; } // the card; holds the section's widgets
    bool isExpanded() const;
    void setExpanded(bool expanded);

    // As wide as the open card even while folded, so the panel keeps one
    // minimum width.
    QSize minimumSizeHint() const override;

private:
    QToolButton* m_header;
    QGroupBox*   m_body;
    QString      m_settingsKey;
};

#endif
