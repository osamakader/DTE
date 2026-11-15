// Offscreen smoke tests for the main window: load a file and inspect the
// widgets. Set DTE_GUI_SCREENSHOT=<file.png> to save a screenshot.

#include "MainWindow.h"
#include "PropertyTableWidget.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QStandardPaths>
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

TEST_F(GuiTest, Screenshot) {
    const char* out = std::getenv("DTE_GUI_SCREENSHOT");
    if (!out) GTEST_SKIP() << "set DTE_GUI_SCREENSHOT to save one";

    window_->loadDeviceTree(kBoard);
    tree_->expandAll();
    auto* root = tree_->topLevelItem(0);
    tree_->setCurrentItem(findChildItem(findChildItem(root, "soc"), "serial@10000000"));
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
