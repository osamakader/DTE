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
#include <QDir>
#include "DeviceTreeParser.h"
#include <QStatusBar>
#include <QListWidget>
#include <QUndoStack>
#include <QTemporaryDir>
#include <QFile>
#include "DiffViewerWidget.h"
#include "PropertyDialog.h"
#include "DtsValue.h"
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

TEST_F(GuiTest, OnlyValueColumnIsEditable) {
    window_->loadDeviceTree(kBoard);
    ASSERT_GT(props_->rowCount(), 0);
    EXPECT_FALSE(props_->item(0, 0)->flags() & Qt::ItemIsEditable);
    EXPECT_FALSE(props_->item(0, 1)->flags() & Qt::ItemIsEditable);
    EXPECT_TRUE(props_->item(0, 2)->flags() & Qt::ItemIsEditable);
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

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

namespace {
std::shared_ptr<DTE::DeviceTreeNode> nodeAt(MainWindow& w, const std::string& path) {
    return w.deviceTree()->findNodeByPath(path);
}
std::vector<std::string> childNames(const std::shared_ptr<DTE::DeviceTreeNode>& n) {
    std::vector<std::string> out;
    for (const auto& c : n->getChildren()) out.push_back(c->getName());
    return out;
}
std::vector<std::string> propNames(const std::shared_ptr<DTE::DeviceTreeNode>& n) {
    std::vector<std::string> out;
    for (const auto& p : n->getProperties()) out.push_back(p.getName());
    return out;
}
} // namespace

TEST_F(GuiTest, AddNodeUndoRedo) {
    window_->loadDeviceTree(kBoard);
    auto* stack = window_->undoStack();
    auto soc = nodeAt(*window_, "/soc");
    EXPECT_FALSE(window_->isModified());

    ASSERT_EQ(window_->applyAddNode(soc, "watchdog@3000").toStdString(), "");
    EXPECT_EQ(childNames(soc).back(), "watchdog@3000");
    ASSERT_TRUE(tree_->currentItem());
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/soc/watchdog@3000");
    EXPECT_TRUE(window_->isModified());
    EXPECT_TRUE(window_->windowTitle().endsWith("*"));
    EXPECT_EQ(stack->undoText(), "Add node 'watchdog@3000'");
    EXPECT_TRUE(source_->toPlainText().contains("watchdog@3000 {"));

    stack->undo();
    EXPECT_EQ(childNames(soc), (std::vector<std::string>{"interrupt-controller@1000", "serial@10000000",
                                                         "ethernet@20000000"}));
    EXPECT_FALSE(window_->isModified()); // back at the clean state
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/soc");

    stack->redo();
    EXPECT_EQ(childNames(soc).back(), "watchdog@3000");
    EXPECT_TRUE(window_->isModified());
}

TEST_F(GuiTest, InvalidEditsAreRejectedWithoutTouchingHistory) {
    window_->loadDeviceTree(kBoard);
    auto* stack = window_->undoStack();
    auto root = window_->deviceTree()->getRoot();
    auto soc = nodeAt(*window_, "/soc");

    EXPECT_NE(window_->applyAddNode(soc, "bad name").indexOf("invalid character"), -1);
    EXPECT_NE(window_->applyAddNode(soc, "serial@10000000").indexOf("already exists"), -1);
    EXPECT_NE(window_->applyRemoveNode(root).indexOf("root node cannot be removed"), -1);
    EXPECT_NE(window_->applyRenameNode(root, "x").indexOf("root node cannot be renamed"), -1);
    EXPECT_NE(window_->applyRenameNode(nodeAt(*window_, "/cpus/cpu@0"), "cpu@1").indexOf("already exists"), -1);
    EXPECT_NE(window_->applySetProperty(soc, "", "reg", "<1 2").indexOf("Missing '>'"), -1);
    EXPECT_NE(window_->applySetProperty(soc, "", "compatible", "\"x\"").indexOf("already exists"), -1);
    EXPECT_NE(window_->applySetProperty(soc, "", "has space", "<1>").indexOf("invalid character"), -1);
    EXPECT_NE(window_->applyRemoveProperty(soc, "nope").indexOf("does not exist"), -1);

    // No-ops do not create history either
    EXPECT_EQ(window_->applyRenameNode(soc, "soc").toStdString(), "");
    EXPECT_EQ(window_->applySetProperty(soc, "compatible", "compatible", "\"simple-bus\"").toStdString(), "");

    EXPECT_EQ(stack->count(), 0);
    EXPECT_FALSE(window_->isModified());
}

TEST_F(GuiTest, RemoveNodeRestoresSubtreeInPlace) {
    window_->loadDeviceTree(kBoard);
    auto cpus = nodeAt(*window_, "/cpus");
    auto cpu0 = nodeAt(*window_, "/cpus/cpu@0");
    const auto root = window_->deviceTree()->getRoot();
    const auto before = childNames(root);

    ASSERT_EQ(window_->applyRemoveNode(cpus).toStdString(), "");
    EXPECT_EQ(root->findChild("cpus"), nullptr);
    EXPECT_EQ(tree_->currentItem()->text(0), "/");

    window_->undoStack()->undo();
    EXPECT_EQ(childNames(root), before);                 // same position
    EXPECT_EQ(nodeAt(*window_, "/cpus/cpu@0"), cpu0);    // same objects
    EXPECT_EQ(cpu0->getProperties().size(), 4u);
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/cpus");
}

TEST_F(GuiTest, RenameNodeUpdatesPaths) {
    window_->loadDeviceTree(kBoard);
    auto serial = nodeAt(*window_, "/soc/serial@10000000");
    ASSERT_EQ(window_->applyRenameNode(serial, "uart@10000000").toStdString(), "");
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/soc/uart@10000000");
    EXPECT_TRUE(window_->deviceTree()->findNodeByPath("/soc/uart@10000000"));

    window_->undoStack()->undo();
    EXPECT_EQ(serial->getName(), "serial@10000000");
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/soc/serial@10000000");
}

TEST_F(GuiTest, SetPropertyAddEditRenameAndUndo) {
    window_->loadDeviceTree(kBoard);
    auto serial = nodeAt(*window_, "/soc/serial@10000000");
    const auto original = propNames(serial);

    // Add
    ASSERT_EQ(window_->applySetProperty(serial, "", "current-speed", "<115200>").toStdString(), "");
    EXPECT_EQ(propNames(serial).back(), "current-speed");
    EXPECT_EQ(props_->selectedPropertyName(), "current-speed");

    // Edit keeps position
    ASSERT_EQ(window_->applySetProperty(serial, "reg", "reg", "<0x10000000 0x2000>").toStdString(), "");
    EXPECT_EQ(serial->propertyIndex("reg"), 1);
    EXPECT_EQ(serial->findProperty("reg")->getValueAsCells(), (std::vector<uint32_t>{0x10000000, 0x2000}));

    // Rename keeps position
    ASSERT_EQ(window_->applySetProperty(serial, "status", "status-x", "\"disabled\"").toStdString(), "");
    EXPECT_EQ(serial->propertyIndex("status-x"), 4);
    EXPECT_EQ(window_->undoStack()->undoText(), "Rename property 'status' to 'status-x'");

    // Remove
    ASSERT_EQ(window_->applyRemoveProperty(serial, "compatible").toStdString(), "");
    EXPECT_EQ(serial->findProperty("compatible"), nullptr);

    // Undo everything: original names, order and values
    while (window_->undoStack()->canUndo()) window_->undoStack()->undo();
    EXPECT_EQ(propNames(serial), original);
    EXPECT_EQ(serial->findProperty("reg")->getValueAsCells(), (std::vector<uint32_t>{0x10000000, 0x1000}));
    EXPECT_EQ(serial->findProperty("status")->getValueAsString(), "\"okay\"");
    EXPECT_FALSE(window_->isModified());
}

TEST_F(GuiTest, InlineValueEdit) {
    window_->loadDeviceTree(kBoard);
    auto cpu0 = nodeAt(*window_, "/cpus/cpu@0");
    ASSERT_TRUE(window_->applyAddNode(cpu0, "tmp").isEmpty()); // select something under cpu@0
    window_->undoStack()->undo();                               // selects cpu@0 again
    ASSERT_EQ(props_->node(), cpu0);

    const int row = cpu0->propertyIndex("clock-frequency");
    props_->item(row, 2)->setText("<1000000000>"); // what committing an in-place edit does
    QApplication::processEvents();
    EXPECT_EQ(cpu0->findProperty("clock-frequency")->getValueAsCells(), std::vector<uint32_t>{1000000000});
    EXPECT_EQ(props_->item(row, 2)->text(), "<0x3b9aca00>");

    // Invalid input is reverted and explained in the status bar
    props_->item(row, 2)->setText("<oops>");
    QApplication::processEvents();
    EXPECT_EQ(cpu0->findProperty("clock-frequency")->getValueAsCells(), std::vector<uint32_t>{1000000000});
    EXPECT_EQ(props_->item(row, 2)->text(), "<0x3b9aca00>");
    EXPECT_NE(window_->statusBar()->currentMessage().indexOf("Invalid value for 'clock-frequency'"), -1);
}

TEST_F(GuiTest, EditsKeepExpansionAndRefreshSearchAndDiff) {
    QTemporaryDir dir;
    window_->loadDeviceTree(kBoard);
    window_->openDiffFile(kBoard);
    EXPECT_EQ(window_->findChild<QTableWidget*>("diffTable")->rowCount(), 0);

    auto* cpusItem = findChildItem(tree_->topLevelItem(0), "cpus");
    cpusItem->setExpanded(true);
    auto* edit = window_->findChild<QLineEdit*>("findEdit");
    edit->setText("cpu@");

    ASSERT_TRUE(window_->applyAddNode(nodeAt(*window_, "/cpus"), "cpu@2").isEmpty());
    EXPECT_TRUE(findChildItem(tree_->topLevelItem(0), "cpus")->isExpanded());
    EXPECT_EQ(window_->findChild<QLabel*>("findStatus")->text().toStdString(), "3 matches");
    // The Diff tab is recomputed against the other file
    EXPECT_EQ(window_->findChild<QTableWidget*>("diffTable")->rowCount(), 1);
    EXPECT_EQ(window_->findChild<QTableWidget*>("diffTable")->item(0, 1)->text(), "/cpus/cpu@2");
}

TEST_F(GuiTest, EditActionsFollowSelection) {
    auto action = [&](const QString& text) { return actionNamed(window_.get(), text); };
    window_->loadDeviceTree(kBoard);
    // Root selected: cannot remove or rename it
    EXPECT_TRUE(action("Add &Node...")->isEnabled());
    EXPECT_FALSE(action("Remove &Node")->isEnabled());
    EXPECT_FALSE(action("Re&name Node...")->isEnabled());
    EXPECT_FALSE(action("&Edit Property...")->isEnabled());

    tree_->setCurrentItem(findChildItem(tree_->topLevelItem(0), "cpus"));
    EXPECT_TRUE(action("Remove &Node")->isEnabled());
    props_->selectRow(0);
    EXPECT_TRUE(action("&Edit Property...")->isEnabled());
    EXPECT_TRUE(action("Remove &Property")->isEnabled());
}

TEST_F(GuiTest, SaveActionWritesEditsAndMarksClean) {
    // Work on a copy so Save (no dialog for a named file) can overwrite it
    QTemporaryDir dir;
    const QString file = dir.filePath("board.dts");
    ASSERT_TRUE(QFile::copy(kBoard, file));
    window_->loadDeviceTree(file);

    ASSERT_TRUE(window_->applySetProperty(window_->deviceTree()->getRoot(), "model", "model",
                                          "\"Edited Board\"").isEmpty());
    ASSERT_TRUE(window_->applyAddNode(nodeAt(*window_, "/soc"), "watchdog@3000").isEmpty());
    EXPECT_TRUE(window_->isModified());

    actionNamed(window_.get(), "&Save")->trigger();
    EXPECT_FALSE(window_->isModified());
    EXPECT_FALSE(window_->windowTitle().endsWith("*"));

    DTE::DeviceTree reloaded;
    ASSERT_TRUE(reloaded.loadFromFile(file.toStdString())) << reloaded.getLoadError();
    EXPECT_EQ(reloaded.getRoot()->findProperty("model")->getValueAsString(), "\"Edited Board\"");
    EXPECT_TRUE(reloaded.findNodeByPath("/soc/watchdog@3000"));

    // Undoing past the save point makes the document modified again
    window_->undoStack()->undo();
    EXPECT_TRUE(window_->isModified());
    window_->undoStack()->setClean(); // avoid the unsaved-changes prompt on teardown
}

TEST_F(GuiTest, ValidateFillsProblemsTabAndNavigates) {
    window_->loadDeviceTree(kBoard);
    auto* problems = window_->findChild<QTableWidget*>("problemsTable");
    auto* tabs = window_->findChild<QTabWidget*>();
    ASSERT_TRUE(problems && tabs);

    actionNamed(window_.get(), "&Validate")->trigger();
    EXPECT_EQ(tabs->currentWidget(), problems);
    EXPECT_EQ(problems->rowCount(), 0);
    EXPECT_NE(window_->statusBar()->currentMessage().indexOf("no problems found"), -1);

    // An edit that breaks reg is reported right away, while the tab is in use
    auto serial = nodeAt(*window_, "/soc/serial@10000000");
    ASSERT_TRUE(window_->applySetProperty(serial, "reg", "reg", "<0x10000000>").isEmpty());
    ASSERT_EQ(problems->rowCount(), 1);
    EXPECT_EQ(problems->item(0, 0)->text(), "Error");
    EXPECT_EQ(problems->item(0, 1)->text(), "/soc/serial@10000000");
    EXPECT_EQ(problems->item(0, 2)->text(), "reg");
    EXPECT_EQ(tabs->tabText(tabs->indexOf(problems)), "Problems (1)");

    // Double-click jumps to the node and property
    tree_->setCurrentItem(tree_->topLevelItem(0));
    emit problems->cellDoubleClicked(0, 3);
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/soc/serial@10000000");
    EXPECT_EQ(props_->selectedPropertyName(), "reg");

    window_->undoStack()->undo();
    EXPECT_EQ(problems->rowCount(), 0);
    EXPECT_EQ(tabs->tabText(tabs->indexOf(problems)), "Problems");
}

TEST_F(GuiTest, PropertyTableShowsAndFollowsReferences) {
    window_->loadDeviceTree(QString(TEST_DATA_DIR) + "/refs.dts");
    auto serial = nodeAt(*window_, "/serial@4000");
    ASSERT_TRUE(serial);
    tree_->setCurrentItem(findChildItem(tree_->topLevelItem(0), "serial@4000"));

    const int clocksRow = serial->propertyIndex("clocks");
    const int col = PropertyTableWidget::DetailsColumn;
    EXPECT_EQ(props_->item(clocksRow, col)->text().toStdString(), "/clock-controller@2000 <0x3>; /oscillator");
    EXPECT_EQ(props_->item(serial->propertyIndex("reset-gpios"), col)->text().toStdString(),
              "/gpio@3000 <0x5 0x1>; /gpio@3000 <0x7 0x0>");
    EXPECT_EQ(props_->item(serial->propertyIndex("reg"), col)->text().toStdString(), "0x4000 size 0x100");
    EXPECT_TRUE(props_->item(serial->propertyIndex("clock-names"), col)->text().isEmpty());

    // Double-clicking the reference goes to its (first) target
    emit props_->cellDoubleClicked(clocksRow, col);
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/clock-controller@2000");
}

TEST_F(GuiTest, ReferencedByListNavigates) {
    window_->loadDeviceTree(QString(TEST_DATA_DIR) + "/refs.dts");
    auto* list = window_->findChild<QListWidget*>("referencedByList");
    ASSERT_TRUE(list);

    tree_->setCurrentItem(findChildItem(tree_->topLevelItem(0), "interrupt-controller@1000"));
    ASSERT_EQ(list->count(), 2);
    EXPECT_EQ(list->item(0)->text(), "/ : interrupt-parent");
    EXPECT_EQ(list->item(1)->text(), "/serial@4000 : interrupts-extended");

    emit list->itemActivated(list->item(1));
    EXPECT_EQ(tree_->currentItem()->toolTip(0), "/serial@4000");
    EXPECT_EQ(props_->selectedPropertyName(), "interrupts-extended");
    EXPECT_EQ(list->count(), 0); // nothing refers to the serial node
}

TEST_F(GuiTest, BrokenReferenceIsFlaggedAfterEdit) {
    window_->loadDeviceTree(QString(TEST_DATA_DIR) + "/refs.dts");
    auto serial = nodeAt(*window_, "/serial@4000");
    ASSERT_TRUE(window_->applySetProperty(serial, "clocks", "clocks", "<0x99>").isEmpty());
    const QString text = props_->item(serial->propertyIndex("clocks"), PropertyTableWidget::DetailsColumn)->text();
    EXPECT_TRUE(text.startsWith("⚠ refers to phandle 0x99")) << text.toStdString();
}

TEST_F(GuiTest, IncomingReferencesForRemovalWarnings) {
    window_->loadDeviceTree(QString(TEST_DATA_DIR) + "/refs.dts");
    auto pinmux = nodeAt(*window_, "/pinmux");
    // The child is referenced, so removing the parent would break it
    EXPECT_TRUE(window_->incomingReferences(pinmux, false).isEmpty());
    EXPECT_EQ(window_->incomingReferences(pinmux, true),
              QStringList{"/serial@4000 : pinctrl-0 -> /pinmux/uart"});
    EXPECT_TRUE(window_->incomingReferences(nodeAt(*window_, "/serial@4000"), true).isEmpty());
}

TEST_F(GuiTest, ApplyOverlayIsOneUndoableStep) {
    const QString dir = QString(TEST_DATA_DIR) + "/overlay";
    window_->loadDeviceTree(dir + "/base.dts");
    auto* stack = window_->undoStack();

    // An edit before the overlay
    ASSERT_TRUE(window_->applyAddNode(window_->deviceTree()->getRoot(), "before").isEmpty());
    auto beforeRoot = window_->deviceTree()->getRoot();

    ASSERT_EQ(window_->applyOverlayFile(dir + "/overlay.dtso").toStdString(), "");
    EXPECT_EQ(stack->undoText(), "Apply overlay 'overlay.dtso'");
    EXPECT_TRUE(window_->deviceTree()->findNodeByPath("/soc/sensor@4000"));
    EXPECT_TRUE(window_->deviceTree()->findNodeByPath("/before")); // earlier edit kept
    EXPECT_NE(window_->statusBar()->currentMessage().indexOf("Applied overlay.dtso"), -1);
    EXPECT_TRUE(findChildItem(findChildItem(tree_->topLevelItem(0), "soc"), "sensor@4000"));
    EXPECT_TRUE(source_->toPlainText().contains("sensor@4000 {"));

    stack->undo();
    EXPECT_EQ(window_->deviceTree()->getRoot(), beforeRoot);
    EXPECT_FALSE(window_->deviceTree()->findNodeByPath("/soc/sensor@4000"));
    stack->undo(); // the edit made before the overlay still undoes cleanly
    EXPECT_FALSE(window_->deviceTree()->findNodeByPath("/before"));
    EXPECT_FALSE(window_->isModified());

    stack->redo();
    stack->redo();
    EXPECT_TRUE(window_->deviceTree()->findNodeByPath("/soc/sensor@4000"));
    stack->setClean();
}

TEST_F(GuiTest, ApplyOverlayErrorsLeaveTreeUnchanged) {
    window_->loadDeviceTree(kBoard);
    auto root = window_->deviceTree()->getRoot();
    EXPECT_NE(window_->applyOverlayFile(kBoard).indexOf("is not an overlay"), -1);
    EXPECT_EQ(window_->deviceTree()->getRoot(), root);
    EXPECT_EQ(window_->undoStack()->count(), 0);
}

TEST_F(GuiTest, OpenSystemDeviceTreeAction) {
    auto* action = actionNamed(window_.get(), "Open S&ystem Device Tree");
    ASSERT_TRUE(action);
    EXPECT_EQ(action->isEnabled(), !DTE::systemDeviceTreePath().empty());
}

TEST_F(GuiTest, LoadsDeviceTreeDirectory) {
    QTemporaryDir dir;
    const QString dt = dir.filePath("device-tree");
    QDir().mkpath(dt + "/soc");
    QFile model(dt + "/model");
    ASSERT_TRUE(model.open(QIODevice::WriteOnly));
    model.write(QByteArray("Live board\0", 11));
    model.close();

    window_->loadDeviceTree(dt);
    ASSERT_TRUE(window_->deviceTree());
    EXPECT_EQ(tree_->topLevelItem(0)->childCount(), 1);
    EXPECT_EQ(props_->item(0, 2)->text(), "\"Live board\"");
}

TEST_F(GuiTest, DetailsColumnDecodesRegAndRanges) {
    window_->loadDeviceTree(QString(TEST_DATA_DIR) + "/addresses.dts");
    auto* edit = window_->findChild<QLineEdit*>("findEdit");
    const int col = PropertyTableWidget::DetailsColumn;

    edit->setText("timer@100");
    auto timer = nodeAt(*window_, "/soc@100000000/bridge@20000000/timer@100");
    EXPECT_EQ(props_->item(timer->propertyIndex("reg"), col)->text().toStdString(),
              "0x100 size 0x20 -> CPU 0x120000100; 0x200 size 0x20 -> CPU 0x120000200");

    edit->setText("soc@");
    auto soc = nodeAt(*window_, "/soc@100000000");
    EXPECT_EQ(props_->item(soc->propertyIndex("ranges"), col)->text().toStdString(), "0x0..0x3fffffff -> 0x100000000");

    edit->setText("eeprom");
    auto eeprom = nodeAt(*window_, "/soc@100000000/i2c@3000/eeprom@50");
    EXPECT_EQ(props_->item(eeprom->propertyIndex("reg"), col)->text().toStdString(),
              "0x50 (not memory-mapped: /soc@100000000/i2c@3000 has no 'ranges')");
}

TEST_F(GuiTest, Screenshot) {
    const char* out = std::getenv("DTE_GUI_SCREENSHOT");
    if (!out) GTEST_SKIP() << "set DTE_GUI_SCREENSHOT to save one";

    const char* file = std::getenv("DTE_GUI_SCREENSHOT_FILE");
    const char* select = std::getenv("DTE_GUI_SCREENSHOT_SELECT");
    window_->loadDeviceTree(file ? QString(file) : kBoard);
    tree_->expandAll();
    auto* root = tree_->topLevelItem(0);
    if (select) {
        window_->findChild<QLineEdit*>("findEdit")->setText(select);
    } else {
        tree_->setCurrentItem(findChildItem(findChildItem(root, "soc"), "serial@10000000"));
    }
    if (const char* other = std::getenv("DTE_GUI_SCREENSHOT_DIFF")) {
        window_->openDiffFile(other);
    }
    window_->show();
    QApplication::processEvents();
    EXPECT_TRUE(window_->grab().save(out));

    if (const char* dialogOut = std::getenv("DTE_GUI_SCREENSHOT_DIALOG")) {
        DTE::PropertyDialog dialog("Edit Property", "/soc/serial@10000000", "reg", "<0x10000000 0x1000",
                                   [](const QString&, const QString& value) {
                                       DTE::PropertyValue v;
                                       std::string error;
                                       return DTE::parseDtsValue(value.toStdString(), v, &error)
                                                  ? QString()
                                                  : QString::fromStdString(error);
                                   });
        dialog.show();
        QApplication::processEvents();
        EXPECT_TRUE(dialog.grab().save(dialogOut));
    }
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
