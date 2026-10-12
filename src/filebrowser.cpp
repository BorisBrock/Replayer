#include "filebrowser.h"

#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "decoder.h"
#include "theme.h"

namespace {

// Container suffixes action cams commonly produce (LRV = GoPro low-res proxy).
const QStringList kVideoSuffixes = {
    "mp4", "m4v", "mov", "mkv", "avi", "ts", "mts", "m2ts", "webm", "lrv", "360",
};

constexpr int kPathRole     = Qt::UserRole;
constexpr int kDurationRole = Qt::UserRole + 1; // seconds; -1: unreadable, unset: pending

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

// Clip icon, file name and duration. The name gets whatever width is left
// and is shortened in the middle, so the extension stays visible.
class ClipDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString name = opt.text;

        // The style (sheet) draws just the hover and selection background.
        opt.text.clear();
        const QWidget* widget = opt.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, widget);

        const bool selected = opt.state.testFlag(QStyle::State_Selected);
        const QColor text = selected ? Theme::selectionText() : opt.palette.color(QPalette::Text);
        QColor muted = selected ? text : opt.palette.color(QPalette::PlaceholderText);
        if (selected) muted.setAlphaF(0.75);

        QRect r = opt.rect.adjusted(kPadding, 0, -kPadding, 0);
        const QRectF iconRect(r.left(), r.top() + (r.height() - kIconSize) / 2.0, kIconSize, kIconSize);
        Theme::drawIcon(*p, Theme::Icon::Clip, iconRect, muted);
        r.setLeft(r.left() + kIconSize + kGap);

        // Fixed column width, so the names line up
        const QFontMetrics fm(opt.font);
        const QString duration = durationText(index.data(kDurationRole));
        const int durationWidth = std::max(fm.horizontalAdvance("88:88"), fm.horizontalAdvance(duration));

        p->save();
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
    static constexpr int kPadding  = 10; // as QListWidget::item in the style sheet
    static constexpr int kIconSize = 16;
    static constexpr int kGap      = 8;
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

    m_folderLabel = new QLabel(this);
    m_folderLabel->setObjectName("MutedLabel");
    m_folderLabel->setAlignment(Qt::AlignHCenter);
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_folderLabel);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformItemSizes(true);
    m_list->setItemDelegate(new ClipDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setToolTip("Ctrl+click or Shift+click to compare several clips");
    connect(m_list, &QListWidget::itemSelectionChanged, this, &FileBrowser::scheduleSelectionChanged);
    connect(m_list, &QListWidget::currentItemChanged, this, &FileBrowser::scheduleSelectionChanged);
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

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const QString& name : names) {
        auto* item = new QListWidgetItem(name, m_list);
        item->setData(kPathRole, dir.absoluteFilePath(name));
        item->setToolTip(QDir::toNativeSeparators(dir.absoluteFilePath(name)));
        updateDuration(m_list->count() - 1);
    }
    selectFiles(selected, current);
}

// Shows the cached duration, or reads it in the background.
void FileBrowser::updateDuration(int row) {
    QListWidgetItem* item = m_list->item(row);
    const QString path = item->data(kPathRole).toString();
    const QFileInfo info(path);
    const auto cached = m_durations.constFind(path);
    if (cached != m_durations.constEnd() && cached->modified == info.lastModified()
        && cached->size == info.size()) {
        item->setData(kDurationRole, cached->duration);
        return;
    }
    item->setData(kDurationRole, QVariant());
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
    const int row = rowOf(path);
    if (row >= 0) updateDuration(row);
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

int FileBrowser::rowOf(const QString& path) const {
    const QString target = QFileInfo(path).absoluteFilePath();
    for (int row = 0; row < m_list->count(); row++) {
        if (m_list->item(row)->data(kPathRole).toString() == target) return row;
    }
    return -1;
}

QStringList FileBrowser::selectedFiles() const {
    QStringList files;
    for (int row = 0; row < m_list->count(); row++) {
        if (m_list->item(row)->isSelected()) files << m_list->item(row)->data(kPathRole).toString();
    }
    return files;
}

QString FileBrowser::currentFile() const {
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(kPathRole).toString() : QString();
}

void FileBrowser::selectFiles(const QStringList& paths, const QString& current) {
    const QSignalBlocker blocker(m_list);
    m_list->clearSelection();
    const int currentRow = rowOf(current.isEmpty() && !paths.isEmpty() ? paths.first() : current);
    if (currentRow >= 0) m_list->setCurrentRow(currentRow, QItemSelectionModel::NoUpdate);
    for (const QString& path : paths) {
        const int row = rowOf(path);
        if (row >= 0) m_list->item(row)->setSelected(true);
    }
    if (currentRow >= 0) m_list->scrollToItem(m_list->item(currentRow));
}

void FileBrowser::deselectFile(const QString& path) {
    const int row = rowOf(path);
    if (row >= 0) m_list->item(row)->setSelected(false);
}

void FileBrowser::selectNext() {
    if (m_list->count() == 0) return;
    m_list->setCurrentRow(std::min(m_list->currentRow() + 1, m_list->count() - 1),
                          QItemSelectionModel::ClearAndSelect);
}

void FileBrowser::selectPrevious() {
    if (m_list->count() == 0) return;
    m_list->setCurrentRow(std::max(m_list->currentRow() - 1, 0),
                          QItemSelectionModel::ClearAndSelect);
}
