#pragma once

#include <QTableWidget>
#include <QTableWidgetItem>
#include <memory>
#include "DeviceTree.h"

namespace DTE {

class PropertyTableWidget : public QTableWidget {
    Q_OBJECT
    
public:
    PropertyTableWidget(QWidget* parent = nullptr);
    // `references` are the decoded phandle references held by the node's
    // properties; they fill the "Refers to" column.
    void setNode(std::shared_ptr<DeviceTreeNode> node, std::vector<PhandleReference> references = {});

    enum Column { NameColumn, TypeColumn, ValueColumn, ReferencesColumn };
    // Path of the first resolved target of the property in `row`, or empty.
    QString referenceTarget(int row) const;
    void clearNode();
    // Selects and scrolls to the row of the named property, if present.
    bool selectProperty(const QString& name);
    // Name of the selected property, or empty.
    QString selectedPropertyName() const;
    std::shared_ptr<DeviceTreeNode> node() const { return currentNode_; }
    
signals:
    void propertyChanged(const QString& propertyName, const QString& newValue);
    
private slots:
    void onItemChanged(QTableWidgetItem* item);
    
private:
    std::shared_ptr<DeviceTreeNode> currentNode_;
    std::vector<PhandleReference> references_;
    void populateTable();
};

} // namespace DTE 