#include "collapsiblesection.h"
#include "theme.h"

#include <QGroupBox>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString& title, const QString& settingsKey, bool expandedByDefault,
                                       QWidget* parent)
    : QWidget(parent), m_settingsKey("panel/expanded/" + settingsKey) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QToolButton(this);
    m_header->setObjectName("SectionHeader");
    m_header->setText(title);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setIconSize(QSize(14, 14));
    m_header->setCheckable(true);
    m_header->setFocusPolicy(Qt::NoFocus);
    m_header->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_header, 0, Qt::AlignLeft);

    m_body = new QGroupBox(this);
    m_body->setObjectName("SectionBody");
    layout->addWidget(m_body);

    connect(m_header, &QToolButton::toggled, this, [this](bool expanded) {
        setExpanded(expanded);
        QSettings().setValue(m_settingsKey, expanded);
    });
    setExpanded(QSettings().value(m_settingsKey, expandedByDefault).toBool());
}

bool CollapsibleSection::isExpanded() const {
    return m_header->isChecked();
}

void CollapsibleSection::setExpanded(bool expanded) {
    {
        const QSignalBlocker blocker(m_header);
        m_header->setChecked(expanded);
    }
    m_header->setIcon(Theme::icon(expanded ? Theme::Icon::ChevronDown : Theme::Icon::ChevronRight, true));
    m_header->setToolTip(expanded ? "Hide" : "Show");
    m_body->setVisible(expanded);
}
