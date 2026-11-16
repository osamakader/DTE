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

#include "DeviceTree.h"
#include "DeviceTreeDiff.h"
#include "PropertyTableWidget.h"
#include "DiffViewerWidget.h"

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
    void showHelp();
    
private:
    // UI components
    QSplitter* mainSplitter_;
    QTreeWidget* treeWidget_;
    QTabWidget* detailTabs_;
    PropertyTableWidget* propertyTable_;
    QTextEdit* sourceView_;
    DiffViewerWidget* diffViewer_;
    
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
    
    void saveDeviceTree(const QString& filename);
    void updateWindowTitle();
    void updateStatusBar();
    void updateTreeWidget();
    void updatePropertyTable();
    void updateSourceView();
    
    void setModified(bool modified);
    bool saveChanges();
    
    void performSearch();
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