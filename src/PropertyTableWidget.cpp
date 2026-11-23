#include "PropertyTableWidget.h"
#include "Addresses.h"
#include "Interrupts.h"
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QColor>

namespace DTE {

PropertyTableWidget::PropertyTableWidget(QWidget* parent)
    : QTableWidget(parent), currentNode_(nullptr) {
    
    // Set up table
    setColumnCount(4);
    // Details: phandle targets of reference properties, decoded reg/ranges
    setHorizontalHeaderLabels({"Property", "Type", "Value", "Details"});
    
    // Set table properties
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    // Only the value column is editable (see populateTable); edits are
    // reported through propertyChanged and applied by the owner.
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    
    // Resize columns
    horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(TypeColumn, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(ValueColumn, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(DetailsColumn, QHeaderView::Stretch);
    
    // Connect signals
    connect(this, &QTableWidget::itemChanged, this, &PropertyTableWidget::onItemChanged);
}

void PropertyTableWidget::setNode(std::shared_ptr<DeviceTreeNode> node, std::vector<PhandleReference> references) {
    currentNode_ = node;
    references_ = std::move(references);
    populateTable();
}

QString PropertyTableWidget::referenceTarget(int row) const {
    auto* it = item(row, DetailsColumn);
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void PropertyTableWidget::clearNode() {
    currentNode_ = nullptr;
    references_.clear();
    setRowCount(0);
}

QString PropertyTableWidget::selectedPropertyName() const {
    const auto rows = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty() || !item(rows.first().row(), 0)) {
        return {};
    }
    return item(rows.first().row(), 0)->text();
}

bool PropertyTableWidget::selectProperty(const QString& name) {
    for (int row = 0; row < rowCount(); ++row) {
        if (item(row, 0) && item(row, 0)->text() == name) {
            selectRow(row);
            scrollToItem(item(row, 0));
            return true;
        }
    }
    return false;
}

void PropertyTableWidget::populateTable() {
    // Filling cells emits itemChanged; those are not user edits.
    const QSignalBlocker blocker(this);
    setRowCount(0);
    
    if (!currentNode_) {
        return;
    }
    
    const auto& properties = currentNode_->getProperties();
    setRowCount(static_cast<int>(properties.size()));
    
    for (int i = 0; i < static_cast<int>(properties.size()); ++i) {
        const auto& prop = properties[i];
        
        // Property name
        auto nameItem = new QTableWidgetItem(QString::fromStdString(prop.getName()));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        setItem(i, 0, nameItem);
        
        // Property type
        QString typeStr;
        if (prop.isEmpty()) {
            typeStr = "Empty";
        } else if (prop.isString()) {
            typeStr = "String";
        } else if (prop.isStringList()) {
            typeStr = "String list";
        } else if (prop.isBinary()) {
            typeStr = "Binary";
        } else if (prop.isCells()) {
            typeStr = "Cells";
        } else if (prop.isCells64()) {
            typeStr = "Cells64";
        } else {
            typeStr = "Unknown";
        }
        
        auto typeItem = new QTableWidgetItem(typeStr);
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        setItem(i, 1, typeItem);
        
        // Property value
        auto valueItem = new QTableWidgetItem(QString::fromStdString(prop.getValueAsString()));
        valueItem->setToolTip("Double-click to edit (DTS syntax)");
        setItem(i, ValueColumn, valueItem);

        // Phandle references held by this property, or decoded addresses
        QStringList targets;
        QString firstTarget;
        bool broken = false;
        if (prop.getName() == "reg" && currentNode_->getParent()) {
            std::vector<RegEntry> regs;
            std::string error;
            if (decodeReg(currentNode_, regs, &error)) {
                for (const auto& r : regs) {
                    targets << QString::fromStdString(describeReg(r));
                    if (!r.translated && r.note.find("outside") != std::string::npos) broken = true;
                }
            } else {
                targets << QString("⚠ %1").arg(QString::fromStdString(error));
                broken = true;
            }
        } else if ((prop.getName() == "interrupts" || prop.getName() == "interrupts-extended") && tree_ &&
                   !(prop.getName() == "interrupts" && currentNode_->findProperty("interrupts-extended"))) {
            std::vector<InterruptSpec> specs;
            std::string error;
            if (decodeInterrupts(*tree_, currentNode_, specs, &error)) {
                for (const auto& spec : specs) {
                    targets << QString::fromStdString(describeInterrupt(spec));
                    if (!spec.problem.empty()) broken = true;
                    if (firstTarget.isEmpty() && spec.controller) {
                        firstTarget = QString::fromStdString(spec.controller->getFullPath());
                    }
                }
            } else {
                targets << QString("⚠ %1").arg(QString::fromStdString(error));
                broken = true;
            }
        } else if (prop.getName() == "ranges" && currentNode_->getParent()) {
            std::vector<RangeEntry> ranges;
            bool identity = false;
            std::string error;
            if (!decodeRanges(currentNode_, ranges, identity, &error)) {
                targets << QString("⚠ %1").arg(QString::fromStdString(error));
                broken = true;
            } else if (identity) {
                targets << "1:1 (child addresses = parent addresses)";
            } else {
                for (const auto& r : ranges) targets << QString::fromStdString(describeRange(r));
            }
        }
        const bool decodedAsInterrupts = !targets.isEmpty() && prop.getName() == "interrupts-extended";
        for (const auto& ref : references_) {
            if (ref.property != prop.getName() || decodedAsInterrupts) continue;
            if (!ref.problem.empty()) {
                targets << QString("⚠ %1").arg(QString::fromStdString(ref.problem));
                broken = true;
                continue;
            }
            QString text = QString::fromStdString(ref.target->getFullPath());
            if (!ref.args.empty()) {
                QStringList args;
                for (auto a : ref.args) args << QString("0x%1").arg(a, 0, 16);
                text += " <" + args.join(' ') + ">";
            }
            targets << text;
            if (firstTarget.isEmpty()) firstTarget = QString::fromStdString(ref.target->getFullPath());
        }
        auto refItem = new QTableWidgetItem(targets.join("; "));
        refItem->setFlags(refItem->flags() & ~Qt::ItemIsEditable);
        refItem->setData(Qt::UserRole, firstTarget);
        if (!targets.isEmpty()) {
            refItem->setToolTip(targets.join("\n") + (firstTarget.isEmpty() ? "" : "\n\nDouble-click to go to the target"));
        }
        if (broken) refItem->setForeground(QColor(0xd3, 0x2f, 0x2f));
        setItem(i, DetailsColumn, refItem);
    }
}

void PropertyTableWidget::onItemChanged(QTableWidgetItem* item) {
    if (!currentNode_ || !item) {
        return;
    }
    
    int row = item->row();
    int column = item->column();
    
    if (column == ValueColumn) {
        QString propertyName = this->item(row, 0)->text();
        QString newValue = item->text();
        
        emit propertyChanged(propertyName, newValue);
    }
}

} // namespace DTE 