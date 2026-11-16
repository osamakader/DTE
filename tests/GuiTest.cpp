// Offscreen smoke tests for the main window: load a file and inspect the
// widgets. Set DTE_GUI_SCREENSHOT=<file.png> to save a screenshot.

#include "MainWindow.h"
#include "PropertyTableWidget.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include "DiffViewerWidget.h"
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>

#include <cstdlib>

using DTE::MainWindow;
using DTE::PropertyTableWidget;

namespace {

const QString kBoard = QString(TEST_DATA_DIR) + "/board.dts";

QTreeWidgetItem* findChildItem(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0) == name) return parent->child(i);
    }
    return nullptr;
}

class GuiTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifndef DTC_PATH
        GTEST_SKIP() << "dtc not available";
#else
        ::setenv("DTE_DTC", DTC_PATH, 1);
        window_ = std::make_unique<MainWindow>();
        window_->resize(1100, 700);
        tree_ = window_->findChild<QTreeWidget*>();
        props_ = window_->findChild<PropertyTableWidget*>();
        source_ = window_->findChild<QTextEdit*>();
        ASSERT_TRUE(tree_ && props_ && source_);
#endif
    }

    std::unique_ptr<MainWindow> window_;
    QTreeWidget* tree_ = nullptr;
    PropertyTableWidget* props_ = nullptr;
    QTextEdit* source_ = nullptr;
};

} // namespace

TEST_F(GuiTest, StartsEmpty) {
    EXPECT_EQ(tree_->topLevelItemCount(), 0);
    EXPECT_EQ(props_->rowCount(), 0);
    EXPECT_EQ(window_->windowTitle(), "Device Tree Explorer");
}

TEST_F(GuiTest, LoadBuildsNestedTree) {
    window_->loadDeviceTree(kBoard);

    ASSERT_EQ(tree_->topLevelItemCount(), 1);
    auto* root = tree_->topLevelItem(0);
    EXPECT_EQ(root->text(0), "/");
    EXPECT_TRUE(root->isExpanded());
    // chosen, aliases, cpus, memory@80000000, soc
    EXPECT_EQ(root->childCount(), 5);

    auto* cpus = findChildItem(root, "cpus");
    ASSERT_TRUE(cpus);
    EXPECT_EQ(cpus->childCount(), 2);
    auto* soc = findChildItem(root, "soc");
    ASSERT_TRUE(soc);
    auto* serial = findChildItem(soc, "serial@10000000");
    ASSERT_TRUE(serial);
    EXPECT_EQ(serial->toolTip(0), "/soc/serial@10000000");

    EXPECT_TRUE(window_->windowTitle().contains("board.dts"));
}

TEST_F(GuiTest, SelectingNodeShowsItsProperties) {
    window_->loadDeviceTree(kBoard);
    auto* root = tree_->topLevelItem(0);
    ASSERT_TRUE(root);

    // Root is selected after loading
    EXPECT_EQ(props_->rowCount(), 5);
    EXPECT_EQ(props_->item(0, 0)->text(), "compatible");
    EXPECT_EQ(props_->item(0, 1)->text(), "String list");

    auto* cpu0 = findChildItem(findChildItem(root, "cpus"), "cpu@0");
    ASSERT_TRUE(cpu0);
    tree_->setCurrentItem(cpu0);

    ASSERT_EQ(props_->rowCount(), 4);
    EXPECT_EQ(props_->item(3, 0)->text(), "clock-frequency");
    EXPECT_EQ(props_->item(3, 1)->text(), "Cells");
    EXPECT_EQ(props_->item(3, 2)->text(), "<0x47868c00>");

    tree_->clearSelection();
    EXPECT_EQ(props_->rowCount(), 0);
}

TEST_F(GuiTest, PropertyTableIsReadOnly) {
    EXPECT_EQ(props_->editTriggers(), QAbstractItemView::NoEditTriggers);
}

TEST_F(GuiTest, SourceViewShowsDts) {
    window_->loadDeviceTree(kBoard);
    const QString text = source_->toPlainText();
    EXPECT_TRUE(text.startsWith("/dts-v1/;"));
    EXPECT_TRUE(text.contains("/memreserve/ 0x88000000 0x100000;"));
    EXPECT_TRUE(text.contains("clock-frequency = <0x1c2000>;"));
}

namespace {
QAction* actionNamed(QWidget* w, const QString& text) {
    for (auto* a : w->findChildren<QAction*>()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}
} // namespace

TEST_F(GuiTest, FindJumpsToFirstMatchAndCounts) {
    window_->loadDeviceTree(kBoard);
    auto* edit = window_->findChild<QLineEdit*>("findEdit");
    auto* status = window_->findChild<QLabel*>("findStatus");
    ASSERT_TRUE(edit && status);

    edit->setText("cpu@");
    ASSERT_TRUE(tree_->currentItem());
    EXPECT_EQ(tree_->currentItem()->text(0), "cpu@0");
    EXPECT_EQ(status->text(), "1 of 2");
    EXPECT_TRUE(tree_->currentItem()->parent()->isExpanded());

    auto* next = actionNamed(window_.get(), "Find &Next");
    auto* prev = actionNamed(window_.get(), "Find &Previous");
    ASSERT_TRUE(next && prev);
    next->trigger();
    EXPECT_EQ(tree_->currentItem()->text(0), "cpu@1");
    EXPECT_EQ(status->text(), "2 of 2");
    next->trigger(); // wraps around
    EXPECT_EQ(tree_->currentItem()->text(0), "cpu@0");
    prev->trigger(); // and backwards
    EXPECT_EQ(tree_->currentItem()->text(0), "cpu@1");

    edit->setText("no-such-node");
    EXPECT_EQ(status->text(), "No matches");
    edit->clear();
    EXPECT_TRUE(status->text().isEmpty());
}

TEST_F(GuiTest, FindByPathAndInProperties) {
    window_->loadDeviceTree(kBoard);
    auto* edit = window_->findChild<QLineEdit*>("findEdit");
    auto* inProps = window_->findChild<QCheckBox*>("findInProperties");
    ASSERT_TRUE(edit && inProps);

    edit->setText("/soc/eth");
    ASSERT_TRUE(tree_->currentItem());
    EXPECT_EQ(tree_->currentItem()->text(0), "ethernet@20000000");

    edit->setText("ns16550");
    EXPECT_EQ(window_->findChild<QLabel*>("findStatus")->text(), "No matches");

    inProps->setChecked(true);
    ASSERT_TRUE(tree_->currentItem());
    EXPECT_EQ(tree_->currentItem()->text(0), "serial@10000000");
    auto rows = props_->selectionModel()->selectedRows();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(props_->item(rows[0].row(), 0)->text(), "compatible");
}

TEST_F(GuiTest, FindIsRefreshedWhenAnotherFileIsLoaded) {
    window_->loadDeviceTree(kBoard);
    auto* edit = window_->findChild<QLineEdit*>("findEdit");
    edit->setText("serial");
    EXPECT_EQ(window_->findChild<QLabel*>("findStatus")->text(), "1 of 1");

    window_->loadDeviceTree(QString(TEST_DATA_DIR) + "/preprocessed/board.dts");
    EXPECT_EQ(window_->findChild<QLabel*>("findStatus")->text(), "1 of 1");
    ASSERT_TRUE(tree_->currentItem());
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/soc/serial@10000000");
}

namespace {
// board.dts with: model changed, cpu@1 removed, a property added to the
// serial node and a new node under soc.
QString writeChangedBoard(const QTemporaryDir& dir) {
    QFile in(kBoard);
    EXPECT_TRUE(in.open(QIODevice::ReadOnly));
    QString text = QString::fromUtf8(in.readAll());
    text.replace("model = \"ACME Widget Board\";", "model = \"ACME Widget Board rev B\";");
    const int cpu1 = text.indexOf("\t\tcpu@1 {");
    const int cpu1End = text.indexOf("\t\t};\n", cpu1) + 5;
    text.remove(cpu1, cpu1End - cpu1);
    text.replace("\t\t\tstatus = \"okay\";", "\t\t\tstatus = \"okay\";\n\t\t\tcurrent-speed = <115200>;");
    text.replace("\t\tethernet@20000000 {", "\t\twatchdog@30000000 {\n\t\t\treg = <0x30000000 0x100>;\n\t\t};\n\n\t\tethernet@20000000 {");

    const QString path = dir.filePath("board-b.dts");
    QFile out(path);
    EXPECT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(text.toUtf8());
    return path;
}

QStringList column(QTableWidget* t, int col) {
    QStringList out;
    for (int r = 0; r < t->rowCount(); ++r) out << (t->item(r, col) ? t->item(r, col)->text() : QString());
    return out;
}
} // namespace

TEST_F(GuiTest, DiffShowsChangesWithValues) {
    QTemporaryDir dir;
    window_->loadDeviceTree(kBoard);
    window_->openDiffFile(writeChangedBoard(dir));

    auto* viewer = window_->findChild<DTE::DiffViewerWidget*>();
    auto* table = window_->findChild<QTableWidget*>("diffTable");
    ASSERT_TRUE(viewer && table);
    EXPECT_TRUE(viewer->hasDiff());
    EXPECT_EQ(window_->findChild<QTabWidget*>()->currentWidget(), viewer);

    EXPECT_EQ(column(table, 0), (QStringList{"Modified", "Removed node", "Removed", "Removed", "Removed",
                                             "Removed", "Added", "Added node", "Added"}));
    EXPECT_EQ(column(table, 1).first(), "/");
    EXPECT_EQ(column(table, 2).first(), "model");
    EXPECT_EQ(column(table, 3).first(), "\"ACME Widget Board\"");
    EXPECT_EQ(column(table, 4).first(), "\"ACME Widget Board rev B\"");
    EXPECT_EQ(column(table, 1)[6], "/soc/serial@10000000");
    EXPECT_EQ(column(table, 4)[6], "<0x1c200>");

    EXPECT_EQ(window_->findChild<QLabel*>("diffStats")->text().toStdString(),
              "Total changes: 9 | Added: 3 | Removed: 5 | Modified: 1");
    EXPECT_TRUE(window_->findChild<QLabel*>("diffFiles")->text().contains("board-b.dts"));
}

TEST_F(GuiTest, DiffFilters) {
    QTemporaryDir dir;
    window_->loadDeviceTree(kBoard);
    window_->openDiffFile(writeChangedBoard(dir));
    auto* table = window_->findChild<QTableWidget*>("diffTable");
    auto* type = window_->findChild<QComboBox*>("diffTypeFilter");
    auto* filter = window_->findChild<QLineEdit*>("diffPathFilter");

    type->setCurrentIndex(type->findData(static_cast<int>(DTE::DiffType::Added)));
    EXPECT_EQ(column(table, 0), (QStringList{"Added", "Added node", "Added"}));

    type->setCurrentIndex(0);
    filter->setText("speed"); // matches a property name
    EXPECT_EQ(column(table, 2), (QStringList{"current-speed"}));
    filter->setText("cpu@1"); // matches a path
    EXPECT_EQ(table->rowCount(), 5);
}

TEST_F(GuiTest, DoubleClickingAChangeSelectsItsNode) {
    QTemporaryDir dir;
    window_->loadDeviceTree(kBoard);
    window_->openDiffFile(writeChangedBoard(dir));
    auto* table = window_->findChild<QTableWidget*>("diffTable");

    // Row 1: removed node /cpus/cpu@1 (exists in the open tree)
    emit table->cellDoubleClicked(1, 0);
    ASSERT_TRUE(tree_->currentItem());
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/cpus/cpu@1");

    // Row 0: modified property on the root, selects its row
    emit table->cellDoubleClicked(0, 0);
    EXPECT_EQ(tree_->currentItem()->text(0), "/");
    auto rows = props_->selectionModel()->selectedRows();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(props_->item(rows[0].row(), 0)->text(), "model");

    // Row 7: added node, only in the other file: selection stays put
    emit table->cellDoubleClicked(7, 0);
    EXPECT_EQ(tree_->currentItem()->text(0), "/");
}

TEST_F(GuiTest, DiffExportAndReset) {
    QTemporaryDir dir;
    window_->loadDeviceTree(kBoard);
    window_->openDiffFile(writeChangedBoard(dir));
    auto* viewer = window_->findChild<DTE::DiffViewerWidget*>();

    const QString out = dir.filePath("diff.json");
    QString error;
    ASSERT_TRUE(viewer->exportDiffTo(out, &error)) << error.toStdString();
    QFile f(out);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_TRUE(f.readAll().contains("\"total_changes\": 9"));

    EXPECT_FALSE(viewer->exportDiffTo("/nonexistent/dir/diff.json", &error));
    EXPECT_FALSE(error.isEmpty());

    // Loading another file discards the diff
    window_->loadDeviceTree(kBoard);
    EXPECT_FALSE(viewer->hasDiff());
    EXPECT_EQ(window_->findChild<QTableWidget*>("diffTable")->rowCount(), 0);
}

TEST_F(GuiTest, DiffWithIdenticalTree) {
    window_->loadDeviceTree(kBoard);
    window_->openDiffFile(kBoard);
    EXPECT_EQ(window_->findChild<QTableWidget*>("diffTable")->rowCount(), 0);
    EXPECT_EQ(window_->findChild<QLabel*>("diffStats")->text().toStdString(), "No differences.");
}

TEST_F(GuiTest, Screenshot) {
    const char* out = std::getenv("DTE_GUI_SCREENSHOT");
    if (!out) GTEST_SKIP() << "set DTE_GUI_SCREENSHOT to save one";

    window_->loadDeviceTree(kBoard);
    tree_->expandAll();
    auto* root = tree_->topLevelItem(0);
    tree_->setCurrentItem(findChildItem(findChildItem(root, "soc"), "serial@10000000"));
    if (const char* other = std::getenv("DTE_GUI_SCREENSHOT_DIFF")) {
        window_->openDiffFile(other);
    }
    window_->show();
    QApplication::processEvents();
    EXPECT_TRUE(window_->grab().save(out));
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true); // keep QSettings away from the user's config
    QApplication app(argc, argv);
    QApplication::setOrganizationName("DTE Project Tests");
    QApplication::setApplicationName("DeviceTreeExplorerTests");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
