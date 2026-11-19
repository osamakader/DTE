#include "MainWindow.h"
#include "DeviceTreeParser.h"
#include "DtsValue.h"
#include "PropertyDialog.h"

#include <functional>
#include <QApplication>
#include <QTimer>
#include <QSet>
#include <QMenu>
#include <QInputDialog>
#include <QFileInfo>
#include <QToolButton>
#include <QShortcut>
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
#include <QSettings>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStandardPaths>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace DTE {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mainSplitter_(nullptr)
    , treeWidget_(nullptr)
    , detailTabs_(nullptr)
    , propertyTable_(nullptr)
    , sourceView_(nullptr)
    , diffViewer_(nullptr)
    , problemsTable_(nullptr)
    , statusBar_(nullptr)
    , progressBar_(nullptr)
    , statusLabel_(nullptr)
    , deviceTree_(nullptr)
    , diffTree_(nullptr)
    , currentDiff_(nullptr)
    , isModified_(false)
    , findEdit_(nullptr)
    , findInPropertiesCheck_(nullptr)
    , findStatusLabel_(nullptr)
    , currentSearchIndex_(-1)
    , undoStack_(new QUndoStack(this)) {
    
    setWindowTitle("Device Tree Explorer");
    setMinimumSize(800, 600);
    resize(1200, 800);
    
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    createCentralWidget();
    createDockWidgets();
    
    setupDragAndDrop();
    loadSettings();
    
    // Set up connections
    connect(treeWidget_, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::onTreeItemSelectionChanged);
    connect(treeWidget_, &QTreeWidget::itemDoubleClicked,
            this, &MainWindow::onTreeItemDoubleClicked);
    connect(propertyTable_, &PropertyTableWidget::propertyChanged,
            this, &MainWindow::onPropertyChanged);
    connect(diffViewer_, &DiffViewerWidget::entryActivated,
            this, &MainWindow::onDiffEntryActivated);

    // Editing
    connect(undoStack_, &QUndoStack::cleanChanged, this, [this](bool clean) { setModified(!clean); });
    connect(treeWidget_, &QWidget::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);
    propertyTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(propertyTable_, &QWidget::customContextMenuRequested, this, &MainWindow::showPropertyContextMenu);
    connect(propertyTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::onPropertyCellDoubleClicked);
    connect(propertyTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateEditActions);
    connect(problemsTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::onProblemDoubleClicked);
    connect(referencedByList_, &QListWidget::itemActivated, this, &MainWindow::onReferencedByActivated);
    updateEditActions();
}

MainWindow::~MainWindow() {
    saveSettings();
    // Child objects are destroyed after this destructor has run. Their
    // signals (e.g. QUndoStack::cleanChanged, emitted by its clear() on
    // destruction) must not reach the partially destroyed window.
    for (QObject* child : findChildren<QObject*>()) {
        QObject::disconnect(child, nullptr, this, nullptr);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (isModified_ && !saveChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        for (const QUrl& url : urlList) {
            QString filePath = url.toLocalFile();
            if (canOpenFile(filePath)) {
                loadDeviceTree(filePath);
                break; // Load only the first valid file
            }
        }
    }
}

void MainWindow::createActions() {
    // File actions
    openAction_ = new QAction(QIcon(":/icons/open.png"), "&Open...", this);
    openAction_->setShortcut(QKeySequence::Open);
    openAction_->setStatusTip("Open a device tree file");
    connect(openAction_, &QAction::triggered, this, static_cast<void (MainWindow::*)()>(&MainWindow::openFile));
    
    saveAction_ = new QAction(QIcon(":/icons/save.png"), "&Save", this);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setStatusTip("Save the current device tree");
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveFile);
    
    saveAsAction_ = new QAction(QIcon(":/icons/save_as.png"), "Save &As...", this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    saveAsAction_->setStatusTip("Save the current device tree as...");
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveFileAs);
    
    exportAction_ = new QAction(QIcon(":/icons/export.png"), "&Export...", this);
    exportAction_->setStatusTip("Export device tree to different format");
    connect(exportAction_, &QAction::triggered, this, &MainWindow::exportAsJSON);
    
    exitAction_ = new QAction("E&xit", this);
    exitAction_->setShortcut(QKeySequence::Quit);
    exitAction_->setStatusTip("Exit the application");
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);
    
    // Edit actions
    undoAction_ = undoStack_->createUndoAction(this, "&Undo");
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = undoStack_->createRedoAction(this, "&Redo");
    redoAction_->setShortcut(QKeySequence::Redo);

    addNodeAction_ = new QAction(QIcon(":/icons/add_node.png"), "Add &Node...", this);
    addNodeAction_->setStatusTip("Add a child node to the selected node");
    connect(addNodeAction_, &QAction::triggered, this, &MainWindow::addNode);
    
    removeNodeAction_ = new QAction(QIcon(":/icons/remove_node.png"), "Remove &Node", this);
    removeNodeAction_->setStatusTip("Remove the selected node");
    connect(removeNodeAction_, &QAction::triggered, this, &MainWindow::removeNode);

    renameNodeAction_ = new QAction("Re&name Node...", this);
    renameNodeAction_->setStatusTip("Rename the selected node");
    connect(renameNodeAction_, &QAction::triggered, this, &MainWindow::renameNode);
    
    addPropertyAction_ = new QAction(QIcon(":/icons/add_property.png"), "Add &Property...", this);
    addPropertyAction_->setStatusTip("Add a new property");
    connect(addPropertyAction_, &QAction::triggered, this, &MainWindow::addProperty);
    
    removePropertyAction_ = new QAction(QIcon(":/icons/remove_property.png"), "Remove &Property", this);
    removePropertyAction_->setStatusTip("Remove the selected property");
    connect(removePropertyAction_, &QAction::triggered, this, &MainWindow::removeProperty);
    
    editPropertyAction_ = new QAction(QIcon(":/icons/edit_property.png"), "&Edit Property...", this);
    editPropertyAction_->setStatusTip("Edit the selected property");
    connect(editPropertyAction_, &QAction::triggered, this, &MainWindow::editProperty);
    
    // View actions
    expandAllAction_ = new QAction(QIcon(":/icons/expand.png"), "&Expand All", this);
    expandAllAction_->setStatusTip("Expand all nodes");
    connect(expandAllAction_, &QAction::triggered, this, &MainWindow::expandAll);
    
    collapseAllAction_ = new QAction(QIcon(":/icons/collapse.png"), "&Collapse All", this);
    collapseAllAction_->setStatusTip("Collapse all nodes");
    connect(collapseAllAction_, &QAction::triggered, this, &MainWindow::collapseAll);
    
    findAction_ = new QAction(QIcon(":/icons/find.png"), "&Find...", this);
    findAction_->setShortcut(QKeySequence::Find);
    findAction_->setStatusTip("Find nodes or properties");
    connect(findAction_, &QAction::triggered, this, &MainWindow::findNode);
    
    findNextAction_ = new QAction(QIcon(":/icons/find_next.png"), "Find &Next", this);
    findNextAction_->setShortcut(QKeySequence::FindNext);
    findNextAction_->setStatusTip("Find next occurrence");
    connect(findNextAction_, &QAction::triggered, this, &MainWindow::findNext);
    
    findPreviousAction_ = new QAction(QIcon(":/icons/find_prev.png"), "Find &Previous", this);
    findPreviousAction_->setShortcut(QKeySequence::FindPrevious);
    findPreviousAction_->setStatusTip("Find previous occurrence");
    connect(findPreviousAction_, &QAction::triggered, this, &MainWindow::findPrevious);
    
    // Diff actions
    diffAction_ = new QAction(QIcon(":/icons/diff.png"), "&Diff...", this);
    diffAction_->setStatusTip("Compare with another device tree");
    connect(diffAction_, &QAction::triggered, this, static_cast<void (MainWindow::*)()>(&MainWindow::openDiffFile));
    
    validateAction_ = new QAction(QIcon(":/icons/validate.png"), "&Validate", this);
    validateAction_->setStatusTip("Validate the current device tree");
    connect(validateAction_, &QAction::triggered, this, &MainWindow::validate);
    
    // Help actions
    aboutAction_ = new QAction("&About", this);
    aboutAction_->setStatusTip("About Device Tree Explorer");
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::about);
    
    aboutQtAction_ = new QAction("About &Qt", this);
    aboutQtAction_->setStatusTip("About Qt");
    connect(aboutQtAction_, &QAction::triggered, this, &MainWindow::aboutQt);
    
    helpAction_ = new QAction(QIcon(":/icons/help.png"), "&Help", this);
    helpAction_->setShortcut(QKeySequence::HelpContents);
    helpAction_->setStatusTip("Show help");
    connect(helpAction_, &QAction::triggered, this, &MainWindow::showHelp);
}

void MainWindow::createMenus() {
    // File menu
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(openAction_);
    fileMenu->addAction(saveAction_);
    fileMenu->addAction(saveAsAction_);
    fileMenu->addSeparator();
    
    QMenu* exportMenu = fileMenu->addMenu("&Export");
    exportMenu->addAction("As &JSON...", this, &MainWindow::exportAsJSON);
    exportMenu->addAction("As &YAML...", this, &MainWindow::exportAsYAML);
    exportMenu->addAction("As &DTS...", this, &MainWindow::exportAsDTS);
    exportMenu->addAction("As &DTB...", this, &MainWindow::exportAsDTB);
    
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction_);
    
    // Edit menu
    QMenu* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    editMenu->addSeparator();
    editMenu->addAction(addNodeAction_);
    editMenu->addAction(renameNodeAction_);
    editMenu->addAction(removeNodeAction_);
    editMenu->addSeparator();
    editMenu->addAction(addPropertyAction_);
    editMenu->addAction(removePropertyAction_);
    editMenu->addAction(editPropertyAction_);
    
    // View menu
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(expandAllAction_);
    viewMenu->addAction(collapseAllAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(findAction_);
    viewMenu->addAction(findNextAction_);
    viewMenu->addAction(findPreviousAction_);
    
    // Tools menu
    QMenu* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction(diffAction_);
    toolsMenu->addAction("&Export Diff...", this, &MainWindow::exportDiff);
    toolsMenu->addAction(validateAction_);
    
    // Help menu
    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction(helpAction_);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction_);
    helpMenu->addAction(aboutQtAction_);
}

void MainWindow::createToolBars() {
    // File toolbar
    QToolBar* fileToolBar = addToolBar("File");
    fileToolBar->addAction(openAction_);
    fileToolBar->addAction(saveAction_);
    fileToolBar->addSeparator();
    fileToolBar->addAction(exportAction_);
    
    // Edit toolbar
    QToolBar* editToolBar = addToolBar("Edit");
    editToolBar->addAction(undoAction_);
    editToolBar->addAction(redoAction_);
    editToolBar->addSeparator();
    editToolBar->addAction(addNodeAction_);
    editToolBar->addAction(removeNodeAction_);
    editToolBar->addSeparator();
    editToolBar->addAction(addPropertyAction_);
    editToolBar->addAction(removePropertyAction_);
    editToolBar->addAction(editPropertyAction_);
    
    // View toolbar
    QToolBar* viewToolBar = addToolBar("View");
    viewToolBar->addAction(expandAllAction_);
    viewToolBar->addAction(collapseAllAction_);
    viewToolBar->addSeparator();
    viewToolBar->addAction(findAction_);
    
    // Tools toolbar
    QToolBar* toolsToolBar = addToolBar("Tools");
    toolsToolBar->addAction(diffAction_);
    toolsToolBar->addAction(validateAction_);
}

void MainWindow::createStatusBar() {
    statusBar_ = statusBar();
    
    // Permanent (right-aligned) so temporary showMessage() text on the left
    // never overlaps it.
    statusLabel_ = new QLabel("Ready");
    statusBar_->addPermanentWidget(statusLabel_);
    
    progressBar_ = new QProgressBar();
    progressBar_->setVisible(false);
    statusBar_->addPermanentWidget(progressBar_);
}

void MainWindow::createCentralWidget() {
    mainSplitter_ = new QSplitter(Qt::Horizontal);
    setCentralWidget(mainSplitter_);
    
    // Create tree widget
    treeWidget_ = new QTreeWidget();
    treeWidget_->setHeaderLabel("Device Tree");
    treeWidget_->setAlternatingRowColors(true);
    treeWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Create detail tabs
    detailTabs_ = new QTabWidget();
    
    // Properties tab: the property table, and below it the nodes that
    // reference the selected node through phandles
    propertyTable_ = new PropertyTableWidget();
    referencedByLabel_ = new QLabel("Referenced by:");
    referencedByList_ = new QListWidget();
    referencedByList_->setObjectName("referencedByList");
    referencedByList_->setToolTip("Nodes that refer to the selected node; double-click to go there");
    auto* referencedByBox = new QWidget();
    auto* referencedByLayout = new QVBoxLayout(referencedByBox);
    referencedByLayout->setContentsMargins(0, 4, 0, 0);
    referencedByLayout->addWidget(referencedByLabel_);
    referencedByLayout->addWidget(referencedByList_);
    auto* propertiesSplitter = new QSplitter(Qt::Vertical);
    propertiesSplitter->addWidget(propertyTable_);
    propertiesSplitter->addWidget(referencedByBox);
    propertiesSplitter->setStretchFactor(0, 4);
    propertiesSplitter->setStretchFactor(1, 1);
    propertiesPage_ = propertiesSplitter;
    detailTabs_->addTab(propertiesPage_, "Properties");
    
    // Source tab
    sourceView_ = new QTextEdit();
    sourceView_->setReadOnly(true);
    sourceView_->setFont(QFont("Monospace", 10));
    detailTabs_->addTab(sourceView_, "Source");
    
    // Diff tab
    diffViewer_ = new DiffViewerWidget();
    detailTabs_->addTab(diffViewer_, "Diff");

    // Problems tab (Tools > Validate)
    problemsTable_ = new QTableWidget(0, 4);
    problemsTable_->setObjectName("problemsTable");
    problemsTable_->setHorizontalHeaderLabels({"Severity", "Path", "Property", "Message"});
    problemsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    problemsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    problemsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsTable_->setAlternatingRowColors(true);
    problemsTable_->verticalHeader()->setVisible(false);
    problemsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    problemsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    problemsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    problemsTable_->horizontalHeader()->setStretchLastSection(true);
    detailTabs_->addTab(problemsTable_, "Problems");
    
    // Left pane: find bar above the tree
    auto* leftPane = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);
    leftLayout->addWidget(createFindBar());
    leftLayout->addWidget(treeWidget_);

    // Add widgets to splitter
    mainSplitter_->addWidget(leftPane);
    mainSplitter_->addWidget(detailTabs_);
    mainSplitter_->setStretchFactor(0, 1);
    mainSplitter_->setStretchFactor(1, 2);
}

void MainWindow::createDockWidgets() {
    // TODO: Add dock widgets for additional functionality
}

void MainWindow::openFile() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Device Tree File", "",
        "Device Tree Files (*.dts *.dtsi *.dtso *.dtb *.dtbo);;DTS Files (*.dts *.dtsi *.dtso);;"
        "DTB Files (*.dtb *.dtbo);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        loadDeviceTree(fileName);
    }
}

bool MainWindow::saveFile() {
    if (currentFile_.isEmpty()) {
        saveFileAs();
        return !currentFile_.isEmpty() && !isModified_;
    }
    return saveDeviceTree(currentFile_);
}

void MainWindow::saveFileAs() {
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Device Tree File", "",
        "DTS Files (*.dts);;DTB Files (*.dtb);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        saveDeviceTree(fileName);
    }
}

void MainWindow::exportAsJSON() {
    if (!deviceTree_) {
        showError("Export Error", "No device tree loaded");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export as JSON", "",
        "JSON Files (*.json);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        std::string jsonContent = deviceTree_->exportAsJSON();
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << QString::fromStdString(jsonContent);
            showSuccess("Export", "Successfully exported to " + fileName);
        } else {
            showError("Export Error", "Failed to write file: " + fileName);
        }
    }
}

void MainWindow::exportAsYAML() {
    if (!deviceTree_) {
        showError("Export Error", "No device tree loaded");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export as YAML", "",
        "YAML Files (*.yaml *.yml);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        std::string yamlContent = deviceTree_->exportAsYAML();
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << QString::fromStdString(yamlContent);
            showSuccess("Export", "Successfully exported to " + fileName);
        } else {
            showError("Export Error", "Failed to write file: " + fileName);
        }
    }
}

void MainWindow::exportAsDTS() {
    if (!deviceTree_) {
        showError("Export Error", "No device tree loaded");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export as DTS", "",
        "DTS Files (*.dts);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        std::string error;
        if (deviceTree_->saveToFile(fileName.toStdString(), true, &error)) {
            showSuccess("Export", "Successfully exported to " + fileName);
        } else {
            showError("Export Error", QString::fromStdString(error));
        }
    }
}

void MainWindow::exportAsDTB() {
    if (!deviceTree_) {
        showError("Export Error", "No device tree loaded");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export as DTB", "",
        "DTB Files (*.dtb);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        std::string error;
        if (deviceTree_->saveToFile(fileName.toStdString(), false, &error)) {
            showSuccess("Export", "Successfully exported to " + fileName);
        } else {
            showError("Export Error", QString::fromStdString(error));
        }
    }
}

void MainWindow::addNode() {
    if (!deviceTree_) {
        showError("Add Node", "No device tree loaded");
        return;
    }
    auto parent = selectedNode() ? selectedNode() : deviceTree_->getRoot();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add Node", QString("Name of the new child of %1\n(name or name@unit-address):")
                              .arg(QString::fromStdString(parent->getFullPath())),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    const QString error = applyAddNode(parent, name);
    if (!error.isEmpty()) showError("Add Node", error);
}

void MainWindow::removeNode() {
    const QString error = applyRemoveNode(selectedNode());
    if (!error.isEmpty()) showError("Remove Node", error);
}

void MainWindow::renameNode() {
    auto node = selectedNode();
    if (!node) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename Node", "New name:", QLineEdit::Normal,
                                               QString::fromStdString(node->getName()), &ok);
    if (!ok) return;
    const QString error = applyRenameNode(node, name);
    if (!error.isEmpty()) showError("Rename Node", error);
}

void MainWindow::addProperty() {
    auto node = selectedNode();
    if (!node) {
        showError("Add Property", "Select a node first");
        return;
    }
    PropertyDialog dialog("Add Property", QString::fromStdString(node->getFullPath()), QString(), QString(),
                          [this, node](const QString& name, const QString& value) {
                              return validatePropertyInput(node, QString(), name, value);
                          },
                          this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString error = applySetProperty(node, QString(), dialog.name(), dialog.value());
    if (!error.isEmpty()) showError("Add Property", error);
}

void MainWindow::removeProperty() {
    const QString name = propertyTable_->selectedPropertyName();
    if (name.isEmpty()) {
        showError("Remove Property", "Select a property first");
        return;
    }
    const QString error = applyRemoveProperty(propertyTable_->node(), name);
    if (!error.isEmpty()) showError("Remove Property", error);
}

void MainWindow::editProperty() {
    auto node = propertyTable_->node();
    const QString oldName = propertyTable_->selectedPropertyName();
    const DeviceTreeProperty* prop = node ? node->findProperty(oldName.toStdString()) : nullptr;
    if (!prop) {
        showError("Edit Property", "Select a property first");
        return;
    }
    PropertyDialog dialog("Edit Property", QString::fromStdString(node->getFullPath()), oldName,
                          QString::fromStdString(prop->getValueAsString()),
                          [this, node, oldName](const QString& name, const QString& value) {
                              return validatePropertyInput(node, oldName, name, value);
                          },
                          this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString error = applySetProperty(node, oldName, dialog.name(), dialog.value());
    if (!error.isEmpty()) showError("Edit Property", error);
}

void MainWindow::expandAll() {
    treeWidget_->expandAll();
}

void MainWindow::collapseAll() {
    treeWidget_->collapseAll();
}

QWidget* MainWindow::createFindBar() {
    auto* bar = new QWidget();
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(2, 2, 2, 0);

    findEdit_ = new QLineEdit();
    findEdit_->setObjectName("findEdit");
    findEdit_->setPlaceholderText("Find node (use / for paths)...");
    findEdit_->setClearButtonEnabled(true);

    findInPropertiesCheck_ = new QCheckBox("Properties");
    findInPropertiesCheck_->setObjectName("findInProperties");
    findInPropertiesCheck_->setToolTip("Also match property names and values");

    auto* prevButton = new QToolButton();
    prevButton->setArrowType(Qt::UpArrow);
    prevButton->setToolTip("Previous match (Shift+Enter, Shift+F3)");
    auto* nextButton = new QToolButton();
    nextButton->setArrowType(Qt::DownArrow);
    nextButton->setToolTip("Next match (Enter, F3)");

    findStatusLabel_ = new QLabel();
    findStatusLabel_->setObjectName("findStatus");

    layout->addWidget(findEdit_, 1);
    layout->addWidget(findInPropertiesCheck_);
    layout->addWidget(prevButton);
    layout->addWidget(nextButton);
    layout->addWidget(findStatusLabel_);

    connect(findEdit_, &QLineEdit::textChanged, this, &MainWindow::performSearch);
    connect(findInPropertiesCheck_, &QCheckBox::toggled, this, &MainWindow::performSearch);
    connect(findEdit_, &QLineEdit::returnPressed, this, &MainWindow::findNext);
    auto* shiftEnter = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), findEdit_);
    shiftEnter->setContext(Qt::WidgetShortcut);
    connect(shiftEnter, &QShortcut::activated, this, &MainWindow::findPrevious);
    connect(prevButton, &QToolButton::clicked, this, &MainWindow::findPrevious);
    connect(nextButton, &QToolButton::clicked, this, &MainWindow::findNext);

    return bar;
}

void MainWindow::findNode() {
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void MainWindow::findNext() {
    if (searchMatches_.empty()) return;
    currentSearchIndex_ = (currentSearchIndex_ + 1) % static_cast<int>(searchMatches_.size());
    highlightSearchResult();
}

void MainWindow::findPrevious() {
    if (searchMatches_.empty()) return;
    const int n = static_cast<int>(searchMatches_.size());
    currentSearchIndex_ = (currentSearchIndex_ - 1 + n) % n;
    highlightSearchResult();
}

void MainWindow::openDiffFile() {
    if (!deviceTree_) {
        showError("Diff", "Open a device tree first; it is compared with the file you choose next.");
        return;
    }
    QString fileName = QFileDialog::getOpenFileName(this,
        "Compare With", "",
        "Device Tree Files (*.dts *.dtsi *.dtso *.dtb *.dtbo);;All Files (*)");
    
    if (!fileName.isEmpty()) {
        openDiffFile(fileName);
    }
}

void MainWindow::openDiffFile(const QString& filename) {
    if (!deviceTree_) {
        showError("Diff", "Open a device tree before comparing.");
        return;
    }

    auto other = std::make_shared<DeviceTree>();
    if (!other->loadFromFile(filename.toStdString())) {
        showError("Diff", "Could not load " + filename + "\n\n" +
                              QString::fromStdString(other->getLoadError()));
        return;
    }

    diffTree_ = other;
    currentDiffFile_ = filename;
    refreshDiff();
    showDiffView();

    const size_t changes = currentDiff_->getTotalChanges();
    statusBar()->showMessage(changes == 0 ? QString("No differences with %1").arg(filename)
                                          : QString("%1 changes compared with %2").arg(changes).arg(filename),
                             5000);
}

void MainWindow::refreshDiff() {
    // Recomputed after every edit, so the Diff tab stays live.
    if (!diffTree_ || !deviceTree_) return;
    currentDiff_ = std::make_unique<DeviceTreeDiff>(deviceTree_, diffTree_);
    const QString baseName = currentFile_.isEmpty() ? QString("Untitled") : QFileInfo(currentFile_).fileName();
    diffViewer_->setDiff(*currentDiff_, baseName, QFileInfo(currentDiffFile_).fileName());
}

void MainWindow::clearDiff() {
    currentDiff_.reset();
    diffTree_.reset();
    currentDiffFile_.clear();
    diffViewer_->clearDiff();
}

void MainWindow::onDiffEntryActivated(const QString& path, const QString& propertyName) {
    auto node = deviceTree_ ? deviceTree_->findNodeByPath(path.toStdString()) : nullptr;
    if (node && selectNode(node, propertyName)) {
        return;
    }
    // Added nodes only exist in the other tree
    statusBar()->showMessage(QString("%1 only exists in %2").arg(path, QFileInfo(currentDiffFile_).fileName()),
                             5000);
}

void MainWindow::saveDiff() {
    exportDiff();
}

void MainWindow::exportDiff() {
    if (!diffViewer_->hasDiff()) {
        showError("Export Diff", "There is no diff to export. Use Tools > Diff first.");
        return;
    }
    diffViewer_->exportDiff();
}

void MainWindow::onTreeItemSelectionChanged() {
    updatePropertyTable();
    updateEditActions();
}

void MainWindow::onTreeItemDoubleClicked(QTreeWidgetItem* item, int column) {
    // Qt already expands/collapses on double-click; nothing extra yet.
    Q_UNUSED(item);
    Q_UNUSED(column);
}

void MainWindow::onPropertyChanged(const QString& propertyName, const QString& newValue) {
    // Called while the table is committing the edit; apply it (which
    // rebuilds the table) once control returns to the event loop.
    auto node = propertyTable_->node();
    QTimer::singleShot(0, this, [this, node, propertyName, newValue]() {
        const QString error = applySetProperty(node, propertyName, propertyName, newValue);
        if (!error.isEmpty()) {
            statusBar()->showMessage(
                QString("Invalid value for '%1': %2. Change reverted.").arg(propertyName, error), 10000);
            updatePropertyTable();
        }
    });
}

void MainWindow::onPropertyCellDoubleClicked(int row, int column) {
    if (row < 0) return;
    if (column == PropertyTableWidget::ReferencesColumn) {
        // Follow the reference
        const QString target = propertyTable_->referenceTarget(row);
        if (!target.isEmpty() && deviceTree_) {
            selectNode(deviceTree_->findNodeByPath(target.toStdString()));
        }
        return;
    }
    // The value column edits in place; elsewhere open the dialog.
    if (column != PropertyTableWidget::ValueColumn) {
        editProperty();
    }
}

void MainWindow::showTreeContextMenu(const QPoint& pos) {
    if (auto* item = treeWidget_->itemAt(pos)) {
        treeWidget_->setCurrentItem(item);
    }
    if (!deviceTree_) return;
    QMenu menu(this);
    menu.addAction(addNodeAction_);
    menu.addAction(addPropertyAction_);
    menu.addSeparator();
    menu.addAction(renameNodeAction_);
    menu.addAction(removeNodeAction_);
    menu.exec(treeWidget_->viewport()->mapToGlobal(pos));
}

void MainWindow::showPropertyContextMenu(const QPoint& pos) {
    if (auto* item = propertyTable_->itemAt(pos)) {
        propertyTable_->selectRow(item->row());
    }
    if (!propertyTable_->node()) return;
    QMenu menu(this);
    menu.addAction(addPropertyAction_);
    menu.addAction(editPropertyAction_);
    menu.addAction(removePropertyAction_);
    menu.exec(propertyTable_->viewport()->mapToGlobal(pos));
}

void MainWindow::updateEditActions() {
    const auto node = selectedNode();
    const bool isRoot = node && !node->getParent();
    const bool haveProperty = !propertyTable_->selectedPropertyName().isEmpty();
    addNodeAction_->setEnabled(deviceTree_ != nullptr);
    renameNodeAction_->setEnabled(node && !isRoot);
    removeNodeAction_->setEnabled(node && !isRoot);
    addPropertyAction_->setEnabled(node != nullptr);
    editPropertyAction_->setEnabled(haveProperty);
    removePropertyAction_->setEnabled(haveProperty);
}

std::shared_ptr<DeviceTreeNode> MainWindow::selectedNode() const {
    auto* item = dynamic_cast<DeviceTreeWidgetItem*>(treeWidget_->currentItem());
    return item && item->isSelected() ? item->getNode() : nullptr;
}

EditNotify MainWindow::editNotifier() {
    return [this](const std::shared_ptr<DeviceTreeNode>& focus, const QString& property) {
        onTreeEdited(focus, property);
    };
}

QString MainWindow::validatePropertyInput(const std::shared_ptr<DeviceTreeNode>& node, const QString& oldName,
                                          const QString& name, const QString& valueText,
                                          DeviceTreeProperty* parsed) const {
    if (!node) return "No node selected";
    std::string error;
    const std::string newName = name.trimmed().toStdString();
    if (!isValidPropertyName(newName, &error)) return QString::fromStdString(error);
    if (newName != oldName.toStdString() && node->findProperty(newName)) {
        return QString("Property '%1' already exists on %2")
            .arg(QString::fromStdString(newName), QString::fromStdString(node->getFullPath()));
    }
    PropertyValue value;
    if (!parseDtsValue(valueText.toStdString(), value, &error)) return QString::fromStdString(error);
    if (parsed) *parsed = DeviceTreeProperty(newName, value);
    return {};
}

QString MainWindow::applyAddNode(const std::shared_ptr<DeviceTreeNode>& parent, const QString& name) {
    if (!deviceTree_ || !parent) return "No node selected";
    const std::string n = name.trimmed().toStdString();
    std::string error;
    if (!isValidNodeName(n, &error)) return QString::fromStdString(error);
    if (parent->findChild(n)) {
        return QString("A node named '%1' already exists under %2")
            .arg(QString::fromStdString(n), QString::fromStdString(parent->getFullPath()));
    }
    undoStack_->push(new AddNodeCommand(parent, std::make_shared<DeviceTreeNode>(n), editNotifier()));
    return {};
}

QString MainWindow::applyRemoveNode(const std::shared_ptr<DeviceTreeNode>& node) {
    if (!deviceTree_ || !node) return "No node selected";
    if (!node->getParent()) return "The root node cannot be removed";
    undoStack_->push(new RemoveNodeCommand(node, editNotifier()));
    return {};
}

QString MainWindow::applyRenameNode(const std::shared_ptr<DeviceTreeNode>& node, const QString& newName) {
    if (!deviceTree_ || !node) return "No node selected";
    auto parent = node->getParent();
    if (!parent) return "The root node cannot be renamed";
    const std::string n = newName.trimmed().toStdString();
    if (n == node->getName()) return {};
    std::string error;
    if (!isValidNodeName(n, &error)) return QString::fromStdString(error);
    if (parent->findChild(n)) {
        return QString("A node named '%1' already exists under %2")
            .arg(QString::fromStdString(n), QString::fromStdString(parent->getFullPath()));
    }
    undoStack_->push(new RenameNodeCommand(node, n, editNotifier()));
    return {};
}

QString MainWindow::applySetProperty(const std::shared_ptr<DeviceTreeNode>& node, const QString& oldName,
                                     const QString& newName, const QString& valueText) {
    if (!deviceTree_ || !node) return "No node selected";
    if (!oldName.isEmpty() && !node->findProperty(oldName.toStdString())) {
        return QString("Property '%1' no longer exists").arg(oldName);
    }
    DeviceTreeProperty property;
    const QString error = validatePropertyInput(node, oldName, newName, valueText, &property);
    if (!error.isEmpty()) return error;
    if (!oldName.isEmpty() && *node->findProperty(oldName.toStdString()) == property) {
        return {}; // unchanged
    }
    undoStack_->push(new SetPropertyCommand(node, oldName.toStdString(), property, editNotifier()));
    return {};
}

QString MainWindow::applyRemoveProperty(const std::shared_ptr<DeviceTreeNode>& node, const QString& name) {
    if (!deviceTree_ || !node) return "No node selected";
    if (!node->findProperty(name.toStdString())) {
        return QString("Property '%1' does not exist").arg(name);
    }
    undoStack_->push(new RemovePropertyCommand(node, name.toStdString(), editNotifier()));
    return {};
}

void MainWindow::onTreeEdited(const std::shared_ptr<DeviceTreeNode>& focus, const QString& property) {
    // Rebuild the tree view, keeping which nodes were expanded.
    QSet<const DeviceTreeNode*> expanded;
    for (auto it = nodeItems_.cbegin(); it != nodeItems_.cend(); ++it) {
        if (it.value()->isExpanded()) expanded.insert(it.key());
    }
    rebuildTreeItems();
    for (auto it = nodeItems_.cbegin(); it != nodeItems_.cend(); ++it) {
        if (expanded.contains(it.key())) it.value()->setExpanded(true);
    }
    if (!selectNode(focus, property) && deviceTree_ && deviceTree_->getRoot()) {
        selectNode(deviceTree_->getRoot());
    }

    updateSourceView();
    updateStatusBar();
    updateSearchMatches(false);
    refreshDiff();
    if (problemsShown_) updateProblems();
    updateEditActions();
}

void MainWindow::about() {
    QMessageBox::about(this, "About Device Tree Explorer",
        "<h3>Device Tree Explorer v1.0.0</h3>"
        "<p>A modern C++ and Qt application for visualizing and editing Device Tree files.</p>"
        "<p>Features:</p>"
        "<ul>"
        "<li>Visualize and edit Device Tree (DTB/DTS) files</li>"
        "<li>Diff two DTBs/DTS to see changes</li>"
        "<li>Export parsed tree as JSON/YAML</li>"
        "<li>Cross-platform support</li>"
        "</ul>"
        "<p>Copyright (c) 2024 DTE Project</p>");
}

void MainWindow::aboutQt() {
    QMessageBox::aboutQt(this, "About Qt");
}

void MainWindow::showHelp() {
    // TODO: Implement help functionality
    showInfo("Help", "Help functionality not yet implemented");
}

void MainWindow::loadDeviceTree(const QString& filename) {
    if (!saveChanges()) {
        return;
    }

    auto tree = std::make_shared<DeviceTree>();
    if (!tree->loadFromFile(filename.toStdString())) {
        showError("Load Error", "Could not load " + filename + "\n\n" +
                                    QString::fromStdString(tree->getLoadError()));
        return;
    }

    deviceTree_ = tree;
    currentFile_ = filename;
    clearDiff();
    problemsShown_ = false;
    problemsTable_->setRowCount(0);
    detailTabs_->setTabText(detailTabs_->indexOf(problemsTable_), "Problems");
    undoStack_->clear();
    setModified(false);
    updateTreeWidget();
    updateSourceView();
    updateStatusBar();
}

bool MainWindow::saveDeviceTree(const QString& filename) {
    if (!deviceTree_) {
        showError("Save Error", "No device tree loaded");
        return false;
    }

    // Format follows the extension; anything that is not a blob is source.
    const bool asDTS = !(filename.endsWith(".dtb") || filename.endsWith(".dtbo"));
    std::string error;
    if (!deviceTree_->saveToFile(filename.toStdString(), asDTS, &error)) {
        showError("Save Error", QString::fromStdString(error));
        return false;
    }

    currentFile_ = filename;
    deviceTree_->setSourceFile(filename.toStdString());
    undoStack_->setClean();
    setModified(false);
    updateStatusBar();
    return true;
}

void MainWindow::updateWindowTitle() {
    QString title = "Device Tree Explorer";
    if (!currentFile_.isEmpty()) {
        title += " - " + QFileInfo(currentFile_).fileName();
    }
    if (isModified_) {
        title += " *";
    }
    setWindowTitle(title);
}

void MainWindow::updateStatusBar() {
    if (!deviceTree_ || !deviceTree_->getRoot()) {
        statusLabel_->setText("No device tree loaded");
        return;
    }

    size_t nodes = 0;
    size_t props = 0;
    std::function<void(const std::shared_ptr<DeviceTreeNode>&)> count =
        [&](const std::shared_ptr<DeviceTreeNode>& n) {
            ++nodes;
            props += n->getPropertyCount();
            for (const auto& c : n->getChildren()) count(c);
        };
    count(deviceTree_->getRoot());

    const QString name = currentFile_.isEmpty() ? QString("Untitled") : currentFile_;
    statusLabel_->setText(QString("%1 — %2 nodes, %3 properties").arg(name).arg(nodes).arg(props));
}

QTreeWidgetItem* MainWindow::rebuildTreeItems() {
    treeWidget_->clear();
    nodeItems_.clear();
    if (!deviceTree_ || !deviceTree_->getRoot()) {
        return nullptr;
    }
    auto* rootItem = new DeviceTreeWidgetItem(deviceTree_->getRoot(), treeWidget_);
    std::function<void(QTreeWidgetItem*)> index = [&](QTreeWidgetItem* item) {
        if (auto* dtItem = dynamic_cast<DeviceTreeWidgetItem*>(item)) {
            nodeItems_.insert(dtItem->getNode().get(), item);
        }
        for (int i = 0; i < item->childCount(); ++i) index(item->child(i));
    };
    index(rootItem);
    rootItem->setExpanded(true);
    return rootItem;
}

void MainWindow::updateTreeWidget() {
    propertyTable_->clearNode();
    if (auto* rootItem = rebuildTreeItems()) {
        treeWidget_->setCurrentItem(rootItem);
    }
    performSearch(); // refresh matches for the new tree
    updateEditActions();
}

void MainWindow::updatePropertyTable() {
    referencedByList_->clear();
    auto* item = dynamic_cast<DeviceTreeWidgetItem*>(treeWidget_->currentItem());
    if (!item || !item->isSelected() || !deviceTree_) {
        propertyTable_->clearNode();
        referencedByLabel_->setText("Referenced by:");
        return;
    }
    const auto node = item->getNode();

    std::vector<PhandleReference> from;
    int incoming = 0;
    for (auto& ref : deviceTree_->references()) {
        if (ref.source == node) from.push_back(ref);
        if (ref.target == node) {
            auto* entry = new QListWidgetItem(QString("%1 : %2").arg(QString::fromStdString(ref.source->getFullPath()),
                                                                     QString::fromStdString(ref.property)));
            entry->setData(Qt::UserRole, QString::fromStdString(ref.source->getFullPath()));
            entry->setData(Qt::UserRole + 1, QString::fromStdString(ref.property));
            referencedByList_->addItem(entry);
            ++incoming;
        }
    }
    referencedByLabel_->setText(incoming == 0 ? QString("Referenced by: no other node")
                                              : QString("Referenced by %1:").arg(incoming));
    propertyTable_->setNode(node, std::move(from));
}

void MainWindow::onReferencedByActivated(QListWidgetItem* item) {
    if (!item || !deviceTree_) return;
    auto node = deviceTree_->findNodeByPath(item->data(Qt::UserRole).toString().toStdString());
    selectNode(node, item->data(Qt::UserRole + 1).toString());
}

void MainWindow::updateSourceView() {
    if (!deviceTree_) {
        sourceView_->clear();
        return;
    }
    sourceView_->setPlainText(QString::fromStdString(deviceTree_->exportAsDTS()));
}

void MainWindow::setModified(bool modified) {
    isModified_ = modified;
    updateWindowTitle();
}

bool MainWindow::saveChanges() {
    if (!isModified_) {
        return true;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Save Changes", "The device tree has been modified. Do you want to save your changes?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    
    switch (reply) {
        case QMessageBox::Save:
            return saveFile();
        case QMessageBox::Discard:
            return true;
        case QMessageBox::Cancel:
        default:
            return false;
    }
}

void MainWindow::performSearch() {
    updateSearchMatches(true);
}

void MainWindow::updateSearchMatches(bool jumpToFirst) {
    searchMatches_.clear();
    currentSearchIndex_ = -1;

    const QString query = findEdit_->text();
    if (query.isEmpty() || !deviceTree_) {
        findStatusLabel_->clear();
        return;
    }

    SearchOptions options;
    options.properties = findInPropertiesCheck_->isChecked();
    searchMatches_ = deviceTree_->search(query.toStdString(), options);

    if (searchMatches_.empty()) {
        findStatusLabel_->setText("No matches");
        return;
    }
    if (!jumpToFirst) {
        // After an edit: keep the user's selection, just update the count
        findStatusLabel_->setText(QString("%1 matches").arg(searchMatches_.size()));
        return;
    }
    // Jump to the first match as the user types
    currentSearchIndex_ = 0;
    highlightSearchResult();
}

void MainWindow::highlightSearchResult() {
    if (currentSearchIndex_ < 0 || currentSearchIndex_ >= static_cast<int>(searchMatches_.size())) {
        return;
    }
    const auto& match = searchMatches_[currentSearchIndex_];
    selectNode(match.node, QString::fromStdString(match.propertyName));
    findStatusLabel_->setText(QString("%1 of %2").arg(currentSearchIndex_ + 1).arg(searchMatches_.size()));
}

bool MainWindow::selectNode(const std::shared_ptr<DeviceTreeNode>& node, const QString& propertyName) {
    auto* item = node ? nodeItems_.value(node.get(), nullptr) : nullptr;
    if (!item) {
        return false;
    }
    for (auto* p = item->parent(); p; p = p->parent()) {
        p->setExpanded(true);
    }
    treeWidget_->setCurrentItem(item);
    treeWidget_->scrollToItem(item);
    if (!propertyName.isEmpty()) {
        detailTabs_->setCurrentWidget(propertiesPage_);
        propertyTable_->selectProperty(propertyName);
    }
    return true;
}

void MainWindow::loadSettings() {
    QSettings settings;
    
    // Restore window geometry
    QRect geometry = settings.value("window/geometry").toRect();
    if (geometry.isValid()) {
        setGeometry(geometry);
    }
    
    // Restore window state
    Qt::WindowStates state = static_cast<Qt::WindowStates>(
        settings.value("window/state", Qt::WindowNoState).toInt());
    setWindowState(state);
    
    // Restore splitter state
    QByteArray splitterState = settings.value("window/splitterState").toByteArray();
    if (!splitterState.isEmpty()) {
        mainSplitter_->restoreState(splitterState);
    }
}

void MainWindow::saveSettings() {
    QSettings settings;
    
    // Save window geometry
    settings.setValue("window/geometry", geometry());
    
    // Save window state
    settings.setValue("window/state", static_cast<int>(windowState()));
    
    // Save splitter state
    settings.setValue("window/splitterState", mainSplitter_->saveState());
}

void MainWindow::showSuccess(const QString& title, const QString& message) {
    QMessageBox::information(this, title, message);
}

void MainWindow::showError(const QString& title, const QString& message) {
    QMessageBox::critical(this, title, message);
}

void MainWindow::validate() {
    if (!deviceTree_) {
        showError("Validation", "No device tree loaded");
        return;
    }
    problemsShown_ = true;
    const int errors = updateProblems();
    const int total = problemsTable_->rowCount();
    detailTabs_->setCurrentWidget(problemsTable_);
    statusBar()->showMessage(total == 0 ? QString("Validation: no problems found")
                                        : QString("Validation: %1 error(s), %2 warning(s)")
                                              .arg(errors)
                                              .arg(total - errors),
                             8000);
}

int MainWindow::updateProblems() {
    problemsTable_->setRowCount(0);
    if (!deviceTree_) {
        detailTabs_->setTabText(detailTabs_->indexOf(problemsTable_), "Problems");
        return 0;
    }
    const auto issues = deviceTree_->check();
    problemsTable_->setRowCount(static_cast<int>(issues.size()));
    int errors = 0;
    QFont bold = problemsTable_->font();
    bold.setBold(true);
    for (int i = 0; i < static_cast<int>(issues.size()); ++i) {
        const auto& issue = issues[i];
        if (issue.isError()) ++errors;
        auto* sev = new QTableWidgetItem(issue.isError() ? "Error" : "Warning");
        sev->setForeground(issue.isError() ? QColor(0xd3, 0x2f, 0x2f) : QColor(0xe0, 0x7b, 0x00));
        sev->setFont(bold);
        problemsTable_->setItem(i, 0, sev);
        problemsTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(issue.path)));
        problemsTable_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(issue.property)));
        auto* msg = new QTableWidgetItem(QString::fromStdString(issue.message));
        msg->setToolTip(msg->text());
        problemsTable_->setItem(i, 3, msg);
    }
    const int index = detailTabs_->indexOf(problemsTable_);
    detailTabs_->setTabText(index, issues.empty() ? QString("Problems") : QString("Problems (%1)").arg(issues.size()));
    return errors;
}

void MainWindow::onProblemDoubleClicked(int row, int /*column*/) {
    if (!deviceTree_) return;
    const QString path = problemsTable_->item(row, 1)->text();
    const QString prop = problemsTable_->item(row, 2)->text();
    if (auto node = deviceTree_->findNodeByPath(path.toStdString())) {
        selectNode(node, prop);
    }
}

void MainWindow::showInfo(const QString& title, const QString& message) {
    QMessageBox::information(this, title, message);
}

void MainWindow::setupDragAndDrop() {
    setAcceptDrops(true);
}

bool MainWindow::canOpenFile(const QString& filename) {
    return DeviceTreeParserFactory::createParser(filename.toStdString()) != nullptr;
}

// Missing method implementations
void MainWindow::newFile() {
    if (!saveChanges()) {
        return;
    }
    deviceTree_ = std::make_shared<DeviceTree>();
    currentFile_.clear();
    clearDiff();
    problemsShown_ = false;
    problemsTable_->setRowCount(0);
    detailTabs_->setTabText(detailTabs_->indexOf(problemsTable_), "Problems");
    undoStack_->clear();
    setModified(false);
    updateTreeWidget();
    updateSourceView();
    updateStatusBar();
}

void MainWindow::openFile(const QString& filename) {
    loadDeviceTree(filename);
}

void MainWindow::findProperty() {
    findInPropertiesCheck_->setChecked(true);
    findNode();
}

void MainWindow::showTreeView() {
    treeWidget_->setFocus();
}

void MainWindow::showPropertyView() {
    detailTabs_->setCurrentWidget(propertiesPage_);
}

void MainWindow::showSourceView() {
    detailTabs_->setCurrentWidget(sourceView_);
}

void MainWindow::showDiffView() {
    detailTabs_->setCurrentWidget(diffViewer_);
}

} // namespace DTE 