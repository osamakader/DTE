#pragma once

#include <QMainWindow>
#include <QTreeWidget>
#include <QTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QApplication>
#include <QSettings>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QHash>
#include <QListWidget>

#include "DeviceTree.h"
#include "DeviceTreeDiff.h"
#include "PropertyTableWidget.h"
#include "DiffViewerWidget.h"
#include "EditCommands.h"
#include <QUndoStack>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QToolBar;
QT_END_NAMESPACE

namespace DTE {

// Tree widget item for device tree nodes
class DeviceTreeWidgetItem : public QTreeWidgetItem {
public:
    DeviceTreeWidgetItem(std::shared_ptr<DeviceTreeNode> node, QTreeWidget* parent);
    DeviceTreeWidgetItem(std::shared_ptr<DeviceTreeNode> node, QTreeWidgetItem* parent);
    
    std::shared_ptr<DeviceTreeNode> getNode() const { return node_; }
    void updateDisplay();
    
private:
    std::shared_ptr<DeviceTreeNode> node_;
};



// Main application window
class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    
    // Device tree operations
    void loadDeviceTree(const QString& filename);
    void openDiffFile(const QString& filename);

    // Undoable edits. Each validates its input and returns an error message,
    // or an empty string after pushing the change onto the undo stack. The
    // menu/dialog actions are thin wrappers around these.
    QString applyAddNode(const std::shared_ptr<DeviceTreeNode>& parent, const QString& name);
    QString applyRemoveNode(const std::shared_ptr<DeviceTreeNode>& node);
    QString applyRenameNode(const std::shared_ptr<DeviceTreeNode>& node, const QString& newName);
    // oldName empty adds a property; otherwise replaces (and may rename) it.
    QString applySetProperty(const std::shared_ptr<DeviceTreeNode>& node, const QString& oldName,
                             const QString& newName, const QString& valueText);
    QString applyRemoveProperty(const std::shared_ptr<DeviceTreeNode>& node, const QString& name);

    QUndoStack* undoStack() const { return undoStack_; }
    std::shared_ptr<DeviceTree> deviceTree() const { return deviceTree_; }
    // Node of the selected tree item, or nullptr.
    std::shared_ptr<DeviceTreeNode> selectedNode() const;
    bool isModified() const { return isModified_; }
    // "source : property" for every reference into `node` (and, with
    // subtree, its descendants) from outside that subtree. Used to warn
    // before edits that would leave them dangling.
    QStringList incomingReferences(const std::shared_ptr<DeviceTreeNode>& node, bool subtree) const;
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    
private slots:
    // File operations
    void newFile();
    void openFile();
    void openFile(const QString& filename);
    bool saveFile();
    void saveFileAs();
    
    // Device tree operations
    void validate();
    
    // Export operations
    void exportAsJSON();
    void exportAsYAML();
    void exportAsDTS();
    void exportAsDTB();
    
    // Edit operations
    void addNode();
    void removeNode();
    void addProperty();
    void removeProperty();
    
    // View operations
    void showTreeView();
    void showPropertyView();
    void showSourceView();
    void showDiffView();
    
    // Search operations
    void findNode();
    void findProperty();
    
    // Help operations
    void about();
    void aboutQt();
    
    // Utility methods
    void showSuccess(const QString& title, const QString& message);
    
    // Additional methods
    void editProperty();
    void expandAll();
    void collapseAll();
    void findNext();
    void findPrevious();
    void openDiffFile();
    void saveDiff();
    void exportDiff();
    void onTreeItemSelectionChanged();
    void onTreeItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onPropertyChanged(const QString& propertyName, const QString& newValue);
    void onDiffEntryActivated(const QString& path, const QString& propertyName);
    void renameNode();
    void showTreeContextMenu(const QPoint& pos);
    void showPropertyContextMenu(const QPoint& pos);
    void onPropertyCellDoubleClicked(int row, int column);
    void updateEditActions();
    void onProblemDoubleClicked(int row, int column);
    void onReferencedByActivated(QListWidgetItem* item);
    void showHelp();
    
private:
    // UI components
    QSplitter* mainSplitter_;
    QTreeWidget* treeWidget_;
    QTabWidget* detailTabs_;
    PropertyTableWidget* propertyTable_;
    QWidget* propertiesPage_ = nullptr;   // property table + "Referenced by"
    QLabel* referencedByLabel_ = nullptr;
    QListWidget* referencedByList_ = nullptr;
    QTextEdit* sourceView_;
    DiffViewerWidget* diffViewer_;
    QTableWidget* problemsTable_;
    bool problemsShown_ = false;
    
    QStatusBar* statusBar_;
    QProgressBar* progressBar_;
    QLabel* statusLabel_;
    
    // Actions
    QAction* openAction_;
    QAction* saveAction_;
    QAction* saveAsAction_;
    QAction* exportAction_;
    QAction* exitAction_;
    
    QAction* addNodeAction_;
    QAction* removeNodeAction_;
    QAction* addPropertyAction_;
    QAction* removePropertyAction_;
    QAction* editPropertyAction_;
    QAction* renameNodeAction_;
    QAction* undoAction_;
    QAction* redoAction_;
    
    QAction* expandAllAction_;
    QAction* collapseAllAction_;
    QAction* findAction_;
    QAction* findNextAction_;
    QAction* findPreviousAction_;
    
    QAction* diffAction_;
    QAction* validateAction_;
    
    QAction* aboutAction_;
    QAction* aboutQtAction_;
    QAction* helpAction_;
    
    // Data
    std::shared_ptr<DeviceTree> deviceTree_;
    std::shared_ptr<DeviceTree> diffTree_;
    std::unique_ptr<DeviceTreeDiff> currentDiff_;
    
    QString currentFile_;
    QString currentDiffFile_;
    bool isModified_;
    
    // Search (find bar above the tree)
    QLineEdit* findEdit_;
    QCheckBox* findInPropertiesCheck_;
    QLabel* findStatusLabel_;
    std::vector<SearchMatch> searchMatches_;
    int currentSearchIndex_;

    // Tree items by node, rebuilt with the tree widget
    QHash<const DeviceTreeNode*, QTreeWidgetItem*> nodeItems_;
    
    // Undo history of edits to deviceTree_
    QUndoStack* undoStack_;

    // Settings
    QSettings settings_;
    
    // Methods
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void createCentralWidget();
    void createDockWidgets();
    void clearDiff();
    
    bool saveDeviceTree(const QString& filename);
    void updateWindowTitle();
    void updateStatusBar();
    void updateTreeWidget();
    void updatePropertyTable();
    void updateSourceView();
    
    void setModified(bool modified);
    bool saveChanges();
    
    void performSearch();
    void updateSearchMatches(bool jumpToFirst);
    QTreeWidgetItem* rebuildTreeItems();
    void refreshDiff();
    // Fills the Problems tab from DeviceTree::check(); returns the number
    // of errors.
    int updateProblems();
    // Called by edit commands after every redo/undo
    void onTreeEdited(const std::shared_ptr<DeviceTreeNode>& focus, const QString& property);
    EditNotify editNotifier();
    // Error for a property name/value typed by the user, or empty
    QString validatePropertyInput(const std::shared_ptr<DeviceTreeNode>& node, const QString& oldName,
                                  const QString& name, const QString& valueText,
                                  DeviceTreeProperty* parsed = nullptr) const;
    void highlightSearchResult();
    QWidget* createFindBar();
    // Selects the node in the tree (expanding its parents) and, if given,
    // the property row. Returns false if the node is not in the tree.
    bool selectNode(const std::shared_ptr<DeviceTreeNode>& node, const QString& propertyName = {});
    
    void loadSettings();
    void saveSettings();
    
    void showError(const QString& title, const QString& message);
    void showInfo(const QString& title, const QString& message);
    
    // Drag and drop support
    void setupDragAndDrop();
    bool canOpenFile(const QString& filename);
};

} // namespace DTE 