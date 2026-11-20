#include "EditCommands.h"

namespace DTE {

namespace {

QString q(const std::string& s) {
    return QString::fromStdString(s);
}

void callNotify(const EditNotify& notify, const std::shared_ptr<DeviceTreeNode>& node,
                const std::string& property = {}) {
    if (notify) notify(node, q(property));
}

} // namespace

// AddNodeCommand

AddNodeCommand::AddNodeCommand(std::shared_ptr<DeviceTreeNode> parent, std::shared_ptr<DeviceTreeNode> child,
                               EditNotify notify)
    : parent_(std::move(parent)), child_(std::move(child)), index_(parent_->getChildren().size()),
      notify_(std::move(notify)) {
    setText(QString("Add node '%1'").arg(q(child_->getName())));
}

void AddNodeCommand::redo() {
    parent_->insertChild(index_, child_);
    callNotify(notify_, child_);
}

void AddNodeCommand::undo() {
    parent_->removeChild(child_);
    callNotify(notify_, parent_);
}

// RemoveNodeCommand

RemoveNodeCommand::RemoveNodeCommand(std::shared_ptr<DeviceTreeNode> node, EditNotify notify)
    : parent_(node->getParent()), node_(std::move(node)), notify_(std::move(notify)) {
    index_ = static_cast<size_t>(std::max(0, parent_->childIndex(node_.get())));
    setText(QString("Remove node '%1'").arg(q(node_->getName())));
}

void RemoveNodeCommand::redo() {
    parent_->removeChild(node_);
    callNotify(notify_, parent_);
}

void RemoveNodeCommand::undo() {
    parent_->insertChild(index_, node_);
    callNotify(notify_, node_);
}

// RenameNodeCommand

RenameNodeCommand::RenameNodeCommand(std::shared_ptr<DeviceTreeNode> node, std::string newName, EditNotify notify)
    : node_(std::move(node)), oldName_(node_->getName()), newName_(std::move(newName)),
      notify_(std::move(notify)) {
    setText(QString("Rename node '%1' to '%2'").arg(q(oldName_), q(newName_)));
}

void RenameNodeCommand::redo() {
    node_->setName(newName_);
    callNotify(notify_, node_);
}

void RenameNodeCommand::undo() {
    node_->setName(oldName_);
    callNotify(notify_, node_);
}

// SetPropertyCommand

SetPropertyCommand::SetPropertyCommand(std::shared_ptr<DeviceTreeNode> node, const std::string& oldName,
                                       DeviceTreeProperty property, EditNotify notify)
    : node_(std::move(node)), index_(node_->getProperties().size()), new_(std::move(property)),
      notify_(std::move(notify)) {
    if (!oldName.empty()) {
        const int idx = node_->propertyIndex(oldName);
        if (idx >= 0) {
            old_ = node_->getProperties()[static_cast<size_t>(idx)];
            index_ = static_cast<size_t>(idx);
        }
    }
    if (!old_) {
        setText(QString("Add property '%1'").arg(q(new_.getName())));
    } else if (old_->getName() != new_.getName()) {
        setText(QString("Rename property '%1' to '%2'").arg(q(old_->getName()), q(new_.getName())));
    } else {
        setText(QString("Change property '%1'").arg(q(new_.getName())));
    }
}

void SetPropertyCommand::redo() {
    if (old_) {
        node_->removeProperty(old_->getName());
    }
    node_->insertProperty(index_, new_);
    callNotify(notify_, node_, new_.getName());
}

void SetPropertyCommand::undo() {
    node_->removeProperty(new_.getName());
    if (old_) {
        node_->insertProperty(index_, *old_);
        callNotify(notify_, node_, old_->getName());
    } else {
        callNotify(notify_, node_);
    }
}

// RemovePropertyCommand

RemovePropertyCommand::RemovePropertyCommand(std::shared_ptr<DeviceTreeNode> node, const std::string& name,
                                             EditNotify notify)
    : node_(std::move(node)), notify_(std::move(notify)) {
    const int idx = node_->propertyIndex(name);
    index_ = static_cast<size_t>(std::max(0, idx));
    if (idx >= 0) property_ = node_->getProperties()[static_cast<size_t>(idx)];
    setText(QString("Remove property '%1'").arg(q(name)));
}

void RemovePropertyCommand::redo() {
    node_->removeProperty(property_.getName());
    callNotify(notify_, node_);
}

void RemovePropertyCommand::undo() {
    node_->insertProperty(index_, property_);
    callNotify(notify_, node_, property_.getName());
}

// ReplaceRootCommand

ReplaceRootCommand::ReplaceRootCommand(DeviceTree& tree, std::shared_ptr<DeviceTreeNode> newRoot,
                                       const QString& text, EditNotify notify)
    : tree_(tree), oldRoot_(tree.getRoot()), newRoot_(std::move(newRoot)), notify_(std::move(notify)) {
    setText(text);
}

void ReplaceRootCommand::redo() {
    tree_.setRoot(newRoot_);
    callNotify(notify_, newRoot_);
}

void ReplaceRootCommand::undo() {
    tree_.setRoot(oldRoot_);
    callNotify(notify_, oldRoot_);
}

} // namespace DTE
