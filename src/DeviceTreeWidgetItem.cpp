#include "MainWindow.h"
#include <QTreeWidget>

namespace DTE {

DeviceTreeWidgetItem::DeviceTreeWidgetItem(std::shared_ptr<DeviceTreeNode> node, QTreeWidget* parent)
    : QTreeWidgetItem(parent), node_(std::move(node)) {
    updateDisplay();
}

DeviceTreeWidgetItem::DeviceTreeWidgetItem(std::shared_ptr<DeviceTreeNode> node, QTreeWidgetItem* parent)
    : QTreeWidgetItem(parent), node_(std::move(node)) {
    updateDisplay();
}

void DeviceTreeWidgetItem::updateDisplay() {
    if (!node_) return;
    setText(0, QString::fromStdString(node_->getName()));
    setToolTip(0, QString::fromStdString(node_->getFullPath()));

    // Rebuild children. They must be created with this item as parent: an
    // item created with the tree widget as parent is top-level, and
    // addChild() silently ignores items that already belong to a view.
    while (childCount() > 0) {
        delete takeChild(0);
    }
    for (const auto& child : node_->getChildren()) {
        new DeviceTreeWidgetItem(child, this);
    }
}

} // namespace DTE
