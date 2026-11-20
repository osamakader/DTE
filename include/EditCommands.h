#pragma once

// Undoable edits of a device tree, for QUndoStack. Commands only change
// the model; after every redo/undo they call the notify callback with the
// node (and optionally property) the view should focus, so the view can
// refresh. Removed nodes and properties are kept by the command and put
// back at their original position on undo.

#include "DeviceTree.h"

#include <QString>
#include <QUndoCommand>

#include <functional>
#include <memory>
#include <optional>

namespace DTE {

using EditNotify = std::function<void(const std::shared_ptr<DeviceTreeNode>& focus, const QString& property)>;

class AddNodeCommand : public QUndoCommand {
public:
    AddNodeCommand(std::shared_ptr<DeviceTreeNode> parent, std::shared_ptr<DeviceTreeNode> child,
                   EditNotify notify);
    void redo() override;
    void undo() override;

private:
    std::shared_ptr<DeviceTreeNode> parent_;
    std::shared_ptr<DeviceTreeNode> child_;
    size_t index_;
    EditNotify notify_;
};

class RemoveNodeCommand : public QUndoCommand {
public:
    RemoveNodeCommand(std::shared_ptr<DeviceTreeNode> node, EditNotify notify);
    void redo() override;
    void undo() override;

private:
    std::shared_ptr<DeviceTreeNode> parent_;
    std::shared_ptr<DeviceTreeNode> node_;
    size_t index_;
    EditNotify notify_;
};

class RenameNodeCommand : public QUndoCommand {
public:
    RenameNodeCommand(std::shared_ptr<DeviceTreeNode> node, std::string newName, EditNotify notify);
    void redo() override;
    void undo() override;

private:
    std::shared_ptr<DeviceTreeNode> node_;
    std::string oldName_;
    std::string newName_;
    EditNotify notify_;
};

// Adds a property, or replaces (and optionally renames) an existing one
// in place.
class SetPropertyCommand : public QUndoCommand {
public:
    // oldName empty: add `property` at the end.
    SetPropertyCommand(std::shared_ptr<DeviceTreeNode> node, const std::string& oldName,
                       DeviceTreeProperty property, EditNotify notify);
    void redo() override;
    void undo() override;

private:
    std::shared_ptr<DeviceTreeNode> node_;
    std::optional<DeviceTreeProperty> old_;
    size_t index_;
    DeviceTreeProperty new_;
    EditNotify notify_;
};

class RemovePropertyCommand : public QUndoCommand {
public:
    RemovePropertyCommand(std::shared_ptr<DeviceTreeNode> node, const std::string& name, EditNotify notify);
    void redo() override;
    void undo() override;

private:
    std::shared_ptr<DeviceTreeNode> node_;
    DeviceTreeProperty property_;
    size_t index_;
    EditNotify notify_;
};

// Replaces the whole tree (e.g. with the result of applying an overlay);
// undo puts the previous root object back, so commands recorded earlier
// still refer to the nodes they edited.
class ReplaceRootCommand : public QUndoCommand {
public:
    ReplaceRootCommand(DeviceTree& tree, std::shared_ptr<DeviceTreeNode> newRoot, const QString& text,
                       EditNotify notify);
    void redo() override;
    void undo() override;

private:
    DeviceTree& tree_;
    std::shared_ptr<DeviceTreeNode> oldRoot_;
    std::shared_ptr<DeviceTreeNode> newRoot_;
    EditNotify notify_;
};

} // namespace DTE
