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
    void setNode(std::shared_ptr<DeviceTreeNode> node);
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
    void populateTable();
};

} // namespace DTE 