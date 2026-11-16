#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include "DeviceTreeDiff.h"

namespace DTE {

// Table of differences between two device trees, with type and
// path/property filters and export to JSON, YAML or text.
class DiffViewerWidget : public QWidget {
    Q_OBJECT

public:
    DiffViewerWidget(QWidget* parent = nullptr);

    // baseName/otherName label the two sides (e.g. file names).
    void setDiff(const DeviceTreeDiff& diff, const QString& baseName = {}, const QString& otherName = {});
    void clearDiff();
    bool hasDiff() const { return hasDiff_; }

    // Asks for a file name and exports the diff; the format follows the
    // extension (.json, .yaml/.yml, anything else as text).
    void exportDiff();
    // Writes the diff to `fileName`. Returns false and sets `error` on failure.
    bool exportDiffTo(const QString& fileName, QString* error = nullptr) const;

signals:
    // Emitted when a change is double-clicked.
    void entryActivated(const QString& path, const QString& propertyName);

private slots:
    void onFilterChanged();
    void onCellDoubleClicked(int row, int column);

private:
    QVBoxLayout* layout_;
    QLabel* filesLabel_;
    QHBoxLayout* filterLayout_;
    QComboBox* filterTypeCombo_;
    QLineEdit* filterPathEdit_;
    QPushButton* exportButton_;
    QTableWidget* diffTable_;
    QLabel* statsLabel_;

    DeviceTreeDiff diff_;
    bool hasDiff_ = false;
    void populateDiffTable();
    void updateStats();
};

} // namespace DTE
