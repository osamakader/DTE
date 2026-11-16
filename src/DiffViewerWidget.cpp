#include "DiffViewerWidget.h"
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidgetItem>

namespace DTE {

namespace {

enum Column { ColType, ColPath, ColProperty, ColOld, ColNew, ColumnCount };

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setToolTip(text);
    return item;
}

} // namespace

DiffViewerWidget::DiffViewerWidget(QWidget* parent)
    : QWidget(parent), diff_(nullptr, nullptr) {

    layout_ = new QVBoxLayout(this);

    filesLabel_ = new QLabel("Use Tools > Diff to compare the open device tree with another file.");
    filesLabel_->setObjectName("diffFiles");
    filesLabel_->setWordWrap(true);
    layout_->addWidget(filesLabel_);

    filterLayout_ = new QHBoxLayout();

    filterTypeCombo_ = new QComboBox();
    filterTypeCombo_->setObjectName("diffTypeFilter");
    filterTypeCombo_->addItem("All Changes", -1);
    filterTypeCombo_->addItem("Added", static_cast<int>(DiffType::Added));
    filterTypeCombo_->addItem("Removed", static_cast<int>(DiffType::Removed));
    filterTypeCombo_->addItem("Modified", static_cast<int>(DiffType::Modified));
    filterLayout_->addWidget(new QLabel("Type:"));
    filterLayout_->addWidget(filterTypeCombo_);

    filterPathEdit_ = new QLineEdit();
    filterPathEdit_->setObjectName("diffPathFilter");
    filterPathEdit_->setPlaceholderText("Filter by path or property...");
    filterPathEdit_->setClearButtonEnabled(true);
    filterLayout_->addWidget(new QLabel("Filter:"));
    filterLayout_->addWidget(filterPathEdit_, 1);

    exportButton_ = new QPushButton("Export...");
    exportButton_->setEnabled(false);
    filterLayout_->addWidget(exportButton_);
    layout_->addLayout(filterLayout_);

    diffTable_ = new QTableWidget();
    diffTable_->setObjectName("diffTable");
    diffTable_->setColumnCount(ColumnCount);
    diffTable_->setHorizontalHeaderLabels({"Type", "Path", "Property", "Old value", "New value"});
    diffTable_->setAlternatingRowColors(true);
    diffTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diffTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    diffTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diffTable_->verticalHeader()->setVisible(false);
    auto* header = diffTable_->horizontalHeader();
    header->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColPath, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColProperty, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColOld, QHeaderView::Stretch);
    header->setSectionResizeMode(ColNew, QHeaderView::Stretch);
    layout_->addWidget(diffTable_);

    statsLabel_ = new QLabel();
    statsLabel_->setObjectName("diffStats");
    layout_->addWidget(statsLabel_);

    connect(filterTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiffViewerWidget::onFilterChanged);
    connect(filterPathEdit_, &QLineEdit::textChanged,
            this, &DiffViewerWidget::onFilterChanged);
    connect(exportButton_, &QPushButton::clicked, this, &DiffViewerWidget::exportDiff);
    connect(diffTable_, &QTableWidget::cellDoubleClicked, this, &DiffViewerWidget::onCellDoubleClicked);
}

void DiffViewerWidget::setDiff(const DeviceTreeDiff& diff, const QString& baseName, const QString& otherName) {
    diff_ = diff;
    hasDiff_ = true;
    exportButton_->setEnabled(true);
    if (!baseName.isEmpty() || !otherName.isEmpty()) {
        filesLabel_->setText(QString("Comparing <b>%1</b> (old) with <b>%2</b> (new). "
                                     "Double-click a change to show it in the tree.")
                                 .arg(baseName.toHtmlEscaped(), otherName.toHtmlEscaped()));
    }
    populateDiffTable();
    updateStats();
}

void DiffViewerWidget::clearDiff() {
    diff_ = DeviceTreeDiff(nullptr, nullptr);
    hasDiff_ = false;
    exportButton_->setEnabled(false);
    filesLabel_->setText("Use Tools > Diff to compare the open device tree with another file.");
    diffTable_->setRowCount(0);
    statsLabel_->clear();
}

void DiffViewerWidget::onFilterChanged() {
    populateDiffTable();
}

void DiffViewerWidget::onCellDoubleClicked(int row, int /*column*/) {
    auto* path = diffTable_->item(row, ColPath);
    auto* prop = diffTable_->item(row, ColProperty);
    if (path) {
        emit entryActivated(path->text(), prop ? prop->text() : QString());
    }
}

void DiffViewerWidget::exportDiff() {
    if (!hasDiff_) {
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Diff", "",
        "JSON Files (*.json);;YAML Files (*.yaml *.yml);;Text Files (*.txt);;All Files (*)");
    if (fileName.isEmpty()) {
        return;
    }
    QString error;
    if (exportDiffTo(fileName, &error)) {
        QMessageBox::information(this, "Export", "Diff exported to " + fileName);
    } else {
        QMessageBox::critical(this, "Export Error", error);
    }
}

bool DiffViewerWidget::exportDiffTo(const QString& fileName, QString* error) const {
    std::string content;
    if (fileName.endsWith(".json")) {
        content = diff_.exportAsJSON();
    } else if (fileName.endsWith(".yaml") || fileName.endsWith(".yml")) {
        content = diff_.exportAsYAML();
    } else {
        content = diff_.exportAsPatch();
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(content.data(), static_cast<qint64>(content.size())) != static_cast<qint64>(content.size())) {
        if (error) *error = "Failed to write file: " + fileName;
        return false;
    }
    return true;
}

void DiffViewerWidget::populateDiffTable() {
    const QSignalBlocker blocker(diffTable_);
    diffTable_->setRowCount(0);
    if (!hasDiff_) {
        return;
    }

    const int filterType = filterTypeCombo_->currentData().toInt();
    const QString filter = filterPathEdit_->text();

    std::vector<DiffEntry> rows;
    for (const auto& change : diff_.generateDiff()) {
        if (filterType != -1 && static_cast<int>(change.type) != filterType) {
            continue;
        }
        if (!filter.isEmpty() &&
            !QString::fromStdString(change.path).contains(filter, Qt::CaseInsensitive) &&
            !QString::fromStdString(change.propertyName).contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        rows.push_back(change);
    }

    diffTable_->setRowCount(static_cast<int>(rows.size()));
    QFont bold = diffTable_->font();
    bold.setBold(true);

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& change = rows[i];
        const bool isNode = change.propertyName.empty();

        // Colored bold text stays readable on light and dark palettes,
        // unlike the dark cell backgrounds used before.
        QString typeStr;
        QColor color;
        switch (change.type) {
        case DiffType::Added:
            typeStr = isNode ? "Added node" : "Added";
            color = QColor(0x2e, 0x9d, 0x3a);
            break;
        case DiffType::Removed:
            typeStr = isNode ? "Removed node" : "Removed";
            color = QColor(0xd3, 0x2f, 0x2f);
            break;
        case DiffType::Modified:
            typeStr = "Modified";
            color = QColor(0xe0, 0x7b, 0x00);
            break;
        default:
            typeStr = "Unchanged";
            color = diffTable_->palette().color(QPalette::Text);
            break;
        }

        auto* typeItem = readOnlyItem(typeStr);
        typeItem->setForeground(color);
        typeItem->setFont(bold);
        diffTable_->setItem(i, ColType, typeItem);
        diffTable_->setItem(i, ColPath, readOnlyItem(QString::fromStdString(change.path)));
        diffTable_->setItem(i, ColProperty, readOnlyItem(QString::fromStdString(change.propertyName)));
        diffTable_->setItem(i, ColOld, readOnlyItem(QString::fromStdString(change.oldValue)));
        diffTable_->setItem(i, ColNew, readOnlyItem(QString::fromStdString(change.newValue)));
    }
}

void DiffViewerWidget::updateStats() {
    if (!hasDiff_) {
        statsLabel_->clear();
        return;
    }
    const size_t total = diff_.getTotalChanges();
    if (total == 0) {
        statsLabel_->setText("No differences.");
        return;
    }
    statsLabel_->setText(QString("Total changes: %1 | Added: %2 | Removed: %3 | Modified: %4")
                             .arg(total)
                             .arg(diff_.getAddedCount())
                             .arg(diff_.getRemovedCount())
                             .arg(diff_.getModifiedCount()));
}

} // namespace DTE
