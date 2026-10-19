#include "filebrowser.h"
#include "clipmarks.h"
#include "clipsplits.h"

#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "decoder.h"
#include "theme.h"
#include "timeformat.h"

namespace {

// Container suffixes action cams commonly produce (LRV = GoPro low-res proxy).
const QStringList kVideoSuffixes = {
    "mp4", "m4v", "mov", "mkv", "avi", "ts", "mts", "m2ts", "webm", "lrv", "360",
};

constexpr int kPathRole     = Qt::UserRole;     // ClipRef key
constexpr int kDurationRole = Qt::UserRole + 1; // seconds; -1: unreadable, unset: pending
constexpr int kSplitRole    = Qt::UserRole + 2; // a file that has parts
constexpr int kHiddenRole   = Qt::UserRole + 3; // see ClipMarks
constexpr int kColorRole    = Qt::UserRole + 4; // label colour, unset: none

// Cameras number their clips in recording order, so sorting by name lists
// them oldest first. GoPro puts the chapter of a split recording before the
// clip number (GX010123, GX020123, GX010124; older models GOPR0123, GP010123),
// so it is moved behind the number here to keep chapters together.
QString sortKey(const QString& name) {
    static const QRegularExpression gopro(R"(^(?:G[HXLS](\d{2})|GOPR|GP(\d{2}))(\d{4}))",
                                          QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = gopro.match(name);
    if (!match.hasMatch()) return name;
    QString chapter = match.captured(1) + match.captured(2);
    if (chapter.isEmpty()) chapter = "00"; // GOPR: first chapter
    return QString("GoPro %1 %2 %3").arg(match.captured(3), chapter, name);
}

// "21s", "4:05", "1:02:03"
QString durationText(const QVariant& value) {
    if (!value.isValid()) return {};
    const double seconds = value.toDouble();
    if (seconds < 0.0) return "-";
    const qint64 total = std::llround(seconds);
    if (total < 60) return QString("%1s").arg(total);
    if (total < 3600) return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
    return QString("%1:%2:%3")
        .arg(total / 3600)
        .arg(total / 60 % 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

// Clip icon (scissors for a split file), file name and duration. The name gets whatever width is left
// and is shortened in the middle, so the extension stays visible. A colour label tints the row and
// colours the icon and name; hidden clips (when shown) are faded.
class ClipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString name = opt.text;
        // Parts sit indented below their file (the tree itself doesn't
        // indent, so the selection doesn't spill into the gap).
        if (index.parent().isValid()) opt.rect.setLeft(opt.rect.left() + kIndent);

        // The style (sheet) draws just the hover and selection background.
        opt.text.clear();
        const QWidget* widget = opt.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, widget);

        const bool selected = opt.state.testFlag(QStyle::State_Selected);
        const QColor label = index.data(kColorRole).value<QColor>();
        p->save();
        if (label.isValid() && !selected) {
            // Same shape as the hover and selection background
            QColor tint = label;
            tint.setAlphaF(0.16);
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(tint);
            p->drawRoundedRect(QRectF(opt.rect).adjusted(0, 1, 0, -1), 6, 6);
        }
        if (index.data(kHiddenRole).toBool()) p->setOpacity(0.4);

        QColor text = selected ? Theme::selectionText() : opt.palette.color(QPalette::Text);
        QColor muted = selected ? text : opt.palette.color(QPalette::PlaceholderText);
        if (selected) muted.setAlphaF(0.75);
        const QColor iconColor = label.isValid() ? label : muted;
        if (label.isValid() && !selected) text = label;

        QRect r = opt.rect.adjusted(kPadding, 0, -kPadding, 0);
        const QRectF iconRect(r.left(), r.top() + (r.height() - kIconSize) / 2.0, kIconSize, kIconSize);
        const bool split = index.data(kSplitRole).toBool();
        Theme::drawIcon(*p, split ? Theme::Icon::Cut : Theme::Icon::Clip, iconRect, iconColor);
        r.setLeft(r.left() + kIconSize + kGap);

        // Fixed column width, so the names line up
        const QFontMetrics fm(opt.font);
        const QString duration = durationText(index.data(kDurationRole));
        const int durationWidth = std::max(fm.horizontalAdvance("88:88"), fm.horizontalAdvance(duration));

        p->setFont(opt.font);
        p->setPen(muted);
        p->drawText(r, Qt::AlignRight | Qt::AlignVCenter, duration);
        r.setRight(r.right() - durationWidth - kGap);
        p->setPen(text);
        p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(name, Qt::ElideMiddle, r.width()));
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        // Names are shortened to fit, so the list never scrolls sideways.
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setWidth(2 * kPadding + kIconSize + kGap);
        return size;
    }

private:
    static constexpr int kPadding  = 10; // as QTreeWidget#ClipList::item in the style sheet
    static constexpr int kIconSize = 16;
    static constexpr int kGap      = 8;
    static constexpr int kIndent   = 16;
};

} // namespace

FileBrowser::FileBrowser(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 4, 10);
    layout->setSpacing(8);

    auto* openButton = new QPushButton("Open Folder...", this);
    openButton->setObjectName("PrimaryButton");
    openButton->setToolTip("Open a folder of clips (Ctrl+O)");
    connect(openButton, &QPushButton::clicked, this, &FileBrowser::openFolderClicked);
    layout->addWidget(openButton);

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    m_folderLabel = new QLabel(this);
    m_folderLabel->setObjectName("MutedLabel");
    m_folderLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderRow->addWidget(m_folderLabel, 1);

    m_hiddenButton = new QToolButton(this);
    m_hiddenButton->setIcon(Theme::icon(Theme::Icon::Eye, true));
    m_hiddenButton->setCheckable(true);
    m_hiddenButton->setEnabled(false);
    connect(m_hiddenButton, &QToolButton::toggled, this, &FileBrowser::setShowHidden);
    folderRow->addWidget(m_hiddenButton);
    layout->addLayout(folderRow);

    m_list = new QTreeWidget(this);
    m_list->setObjectName("ClipList");
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false); // parts are always shown, just indented
    m_list->setItemsExpandable(false);
    m_list->setIndentation(0); // see ClipDelegate
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformRowHeights(true);
    m_list->setItemDelegate(new ClipDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setToolTip("Ctrl+click or Shift+click to compare several clips.\n"
                       "Right-click a clip to give it a color or hide it,\n"
                       "or a long recording to split it into parts.");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, &FileBrowser::showContextMenu);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &FileBrowser::scheduleSelectionChanged);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &FileBrowser::scheduleSelectionChanged);
    layout->addWidget(m_list, 1);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileBrowser::refresh);

    m_probePool.setMaxThreadCount(2); // memory cards don't like many readers

    setMinimumWidth(180);
    m_folderLabel->setText("No folder open");
}

FileBrowser::~FileBrowser() {
    m_probePool.clear();
    m_probePool.waitForDone();
}

bool FileBrowser::isVideoFile(const QString& path) {
    return kVideoSuffixes.contains(QFileInfo(path).suffix().toLower());
}

QStringList FileBrowser::nameFilters() {
    QStringList patterns;
    for (const QString& suffix : kVideoSuffixes) {
        patterns << "*." + suffix << "*." + suffix.toUpper();
    }
    return patterns;
}

bool FileBrowser::setFolder(const QString& folder) {
    const QFileInfo info(folder);
    if (!info.isDir()) return false;

    if (!m_folder.isEmpty()) m_watcher->removePath(m_folder);
    m_folder = info.absoluteFilePath();
    m_watcher->addPath(m_folder);

    const QString folderName = info.fileName();
    m_folderLabel->setText(folderName.isEmpty() ? "/" : folderName);
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_folder));
    m_list->clear();
    refresh();
    return true;
}

// Re-read the folder, keeping the selection without re-emitting it.
void FileBrowser::refresh() {
    const QStringList selected = selectedFiles();
    const QString current = currentFile();

    QDir dir(m_folder);
    QStringList names = dir.entryList(QStringList(), QDir::Files | QDir::Readable);
    names.erase(std::remove_if(names.begin(), names.end(),
                               [](const QString& n) { return !isVideoFile(n); }),
                names.end());
    // Oldest first, in natural order: clip2 before clip10
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), [&](const QString& a, const QString& b) {
        return collator.compare(sortKey(a), sortKey(b)) < 0;
    });

    // Hidden clips are left out above, or shown faded.
    int hiddenCount = 0;
    const auto mark = [&](QTreeWidgetItem* item, const QString& clip, const QString& toolTip) {
        const bool hidden = ClipMarks::isHidden(clip);
        item->setData(0, kHiddenRole, hidden);
        const QColor color = ClipMarks::color(clip);
        if (color.isValid()) item->setData(0, kColorRole, color);
        item->setToolTip(0, hidden ? toolTip + "\nHidden" : toolTip);
    };

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const QString& name : names) {
        const QString path = dir.absoluteFilePath(name);
        if (ClipMarks::isHidden(path)) {
            hiddenCount++;
            if (!m_showHidden) continue;
        }
        auto* item = new QTreeWidgetItem(m_list, {name});
        item->setData(0, kPathRole, path);
        mark(item, path, QDir::toNativeSeparators(path));
        updateDuration(item);

        const QList<ClipRef> parts = ClipSplits::parts(path);
        item->setData(0, kSplitRole, !parts.isEmpty());
        for (int i = 0; i < parts.size(); i++) {
            const ClipRef& part = parts[i];
            if (ClipMarks::isHidden(part.key())) {
                hiddenCount++;
                if (!m_showHidden) continue;
            }
            auto* child = new QTreeWidgetItem(item, {QString("Part %1  ·  %2").arg(i + 1).arg(part.startText())});
            child->setData(0, kPathRole, part.key());
            child->setData(0, kDurationRole, part.end - part.start);
            mark(child, part.key(), QString("%1\n%2 to %3").arg(QDir::toNativeSeparators(path),
                                                               formatTime(part.start), formatTime(part.end)));
        }
    }
    m_list->expandAll();
    selectFiles(selected, current);

    // Stays enabled while hidden clips are shown, so they can be put away again.
    m_hiddenButton->setEnabled(hiddenCount > 0 || m_showHidden);
    m_hiddenButton->setToolTip(hiddenCount == 0 ? QString("No hidden clips")
                               : QString("%1 %2 hidden clip%3").arg(m_showHidden ? "Showing" : "Show")
                                     .arg(hiddenCount).arg(hiddenCount == 1 ? "" : "s"));
}

bool FileBrowser::contains(const QString& clip) const {
    return itemOf(clip) != nullptr;
}

// Shows the cached duration, or reads it in the background.
void FileBrowser::updateDuration(QTreeWidgetItem* item) {
    const QString path = item->data(0, kPathRole).toString();
    const QFileInfo info(path);
    const auto cached = m_durations.constFind(path);
    if (cached != m_durations.constEnd() && cached->modified == info.lastModified()
        && cached->size == info.size()) {
        item->setData(0, kDurationRole, cached->duration);
        return;
    }
    item->setData(0, kDurationRole, QVariant());
    if (m_probing.contains(path)) return; // checked again when the result arrives

    m_probing.insert(path);
    const QDateTime modified = info.lastModified();
    const qint64 size = info.size();
    m_probePool.start([this, path, modified, size] {
        const double duration = Decoder::probeDuration(path);
        QMetaObject::invokeMethod(this, [=] { onDurationProbed(path, modified, size, duration); },
                                  Qt::QueuedConnection);
    });
}

void FileBrowser::onDurationProbed(const QString& path, const QDateTime& modified, qint64 size,
                                   double duration) {
    m_probing.remove(path);
    m_durations.insert(path, {modified, size, duration});
    // Probes again if the file changed meanwhile, e.g. while still being copied.
    if (QTreeWidgetItem* item = itemOf(path)) updateDuration(item);
}

// Several selection signals fire for one click (e.g. Shift+click); report once.
void FileBrowser::scheduleSelectionChanged() {
    if (m_changePending) return;
    m_changePending = true;
    QTimer::singleShot(0, this, [this] {
        m_changePending = false;
        emit selectionChanged();
    });
}

void FileBrowser::showContextMenu(const QPoint& pos) {
    const QTreeWidgetItem* item = m_list->itemAt(pos);
    if (!item) return;
    // Colour and hiding act on the whole selection when it was right-clicked.
    const QStringList clips = item->isSelected() ? selectedFiles()
                                                 : QStringList{item->data(0, kPathRole).toString()};
    // Splitting on a part: the file it belongs to
    const QTreeWidgetItem* file = item->parent() ? item->parent() : item;
    const QString path = file->data(0, kPathRole).toString();
    const bool split = file->data(0, kSplitRole).toBool();

    QMenu menu(this);
    QAction* splitAction = menu.addAction(Theme::icon(Theme::Icon::Cut), split ? "Edit Split..." : "Split Clip...");
    connect(splitAction, &QAction::triggered, this, [this, path] { emit splitRequested(path); });
    if (split) {
        QAction* removeAction = menu.addAction("Remove Split");
        connect(removeAction, &QAction::triggered, this, [this, path] { removeSplit(path); });
    }
    menu.addSeparator();

    QMenu* colorMenu = menu.addMenu("Color");
    const QColor current = clips.size() == 1 ? ClipMarks::color(clips.first()) : QColor();
    for (const ClipMarks::Label& label : ClipMarks::labels()) {
        QAction* action = colorMenu->addAction(Theme::swatch(label.color), label.name);
        action->setCheckable(true);
        action->setChecked(label.color == current);
        connect(action, &QAction::triggered, this, [this, clips, id = label.id] { setLabel(clips, id); });
    }
    colorMenu->addSeparator();
    QAction* noColor = colorMenu->addAction("No Color");
    noColor->setEnabled(std::any_of(clips.begin(), clips.end(),
                                    [](const QString& clip) { return ClipMarks::color(clip).isValid(); }));
    connect(noColor, &QAction::triggered, this, [this, clips] { setLabel(clips, {}); });

    const bool allHidden = std::all_of(clips.begin(), clips.end(), ClipMarks::isHidden);
    const QString count = clips.size() == 1 ? QString("Clip") : QString("%1 Clips").arg(clips.size());
    QAction* hideAction = menu.addAction(Theme::icon(allHidden ? Theme::Icon::Eye : Theme::Icon::EyeOff),
                                         (allHidden ? "Show " : "Hide ") + count);
    connect(hideAction, &QAction::triggered, this, [this, clips, allHidden] { setHidden(clips, !allHidden); });

    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void FileBrowser::setHidden(const QStringList& clips, bool hidden) {
    for (const QString& clip : clips) ClipMarks::setHidden(clip, hidden);
    const QStringList selected = selectedFiles();
    refresh();
    // Hidden clips leave the selection.
    if (selectedFiles() != selected) scheduleSelectionChanged();
}

void FileBrowser::setLabel(const QStringList& clips, const QString& id) {
    for (const QString& clip : clips) ClipMarks::setLabel(clip, id);
    refresh();
}

void FileBrowser::setShowHidden(bool show) {
    m_showHidden = show;
    const QStringList selected = selectedFiles();
    refresh();
    if (selectedFiles() != selected) scheduleSelectionChanged();
}

// Joins the parts again; selected parts give way to the whole file.
void FileBrowser::removeSplit(const QString& path) {
    QStringList selected = selectedFiles();
    QString current = currentFile();
    const auto isPart = [&](const QString& clip) {
        const ClipRef ref = ClipRef::fromKey(clip);
        return ref.isPart() && ref.path == path;
    };
    const bool partSelected = std::any_of(selected.begin(), selected.end(), isPart);
    selected.erase(std::remove_if(selected.begin(), selected.end(), isPart), selected.end());
    if (partSelected && !selected.contains(path)) selected << path;
    if (isPart(current)) current = path;

    ClipSplits::setParts(path, {});
    refresh();
    selectFiles(selected, current);
    if (partSelected) scheduleSelectionChanged();
}

QList<QTreeWidgetItem*> FileBrowser::allItems() const {
    QList<QTreeWidgetItem*> items;
    for (int row = 0; row < m_list->topLevelItemCount(); row++) {
        QTreeWidgetItem* item = m_list->topLevelItem(row);
        items << item;
        for (int i = 0; i < item->childCount(); i++) items << item->child(i);
    }
    return items;
}

QTreeWidgetItem* FileBrowser::itemOf(const QString& clip) const {
    ClipRef ref = ClipRef::fromKey(clip);
    ref.path = QFileInfo(ref.path).absoluteFilePath();
    const QString target = ref.key();
    for (QTreeWidgetItem* item : allItems()) {
        if (item->data(0, kPathRole).toString() == target) return item;
    }
    return nullptr;
}

QStringList FileBrowser::selectedFiles() const {
    QStringList files;
    for (const QTreeWidgetItem* item : allItems()) {
        if (item->isSelected()) files << item->data(0, kPathRole).toString();
    }
    return files;
}

QString FileBrowser::currentFile() const {
    const QTreeWidgetItem* item = m_list->currentItem();
    return item ? item->data(0, kPathRole).toString() : QString();
}

void FileBrowser::selectFiles(const QStringList& paths, const QString& current) {
    const QSignalBlocker blocker(m_list);
    m_list->clearSelection();
    QTreeWidgetItem* currentItem = itemOf(current.isEmpty() && !paths.isEmpty() ? paths.first() : current);
    if (currentItem) m_list->setCurrentItem(currentItem, 0, QItemSelectionModel::NoUpdate);
    for (const QString& path : paths) {
        if (QTreeWidgetItem* item = itemOf(path)) item->setSelected(true);
    }
    if (currentItem) m_list->scrollToItem(currentItem);
}

void FileBrowser::deselectFile(const QString& path) {
    if (QTreeWidgetItem* item = itemOf(path)) item->setSelected(false);
}

void FileBrowser::selectNext() {
    QTreeWidgetItem* current = m_list->currentItem();
    QTreeWidgetItem* next = current ? m_list->itemBelow(current) : m_list->topLevelItem(0);
    if (!next) next = current;
    if (next) m_list->setCurrentItem(next, 0, QItemSelectionModel::ClearAndSelect);
}

void FileBrowser::selectPrevious() {
    QTreeWidgetItem* current = m_list->currentItem();
    QTreeWidgetItem* previous = current ? m_list->itemAbove(current) : m_list->topLevelItem(0);
    if (!previous) previous = current;
    if (previous) m_list->setCurrentItem(previous, 0, QItemSelectionModel::ClearAndSelect);
}
