#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTranslator>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "CLIApp.h"
#include "MainWindow.h"

namespace {

void setApplicationInfo() {
    QCoreApplication::setApplicationName("Device Tree Explorer");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCoreApplication::setOrganizationName("DTE Project");
    QCoreApplication::setOrganizationDomain("dte-project.org");
}

struct Options {
    QCommandLineOption file{QStringList{"f", "file"}, "Open device tree file", "filename"};
    QCommandLineOption diff{QStringList{"d", "diff"}, "Compare with another device tree file", "filename"};
    QCommandLineOption exportFormat{QStringList{"e", "export"},
                                    "Export the file given with -f and exit (json, yaml, dts, dtb)", "format"};
    QCommandLineOption output{QStringList{"o", "output"}, "Output file for --export", "filename"};
    QCommandLineOption validate{QStringList{"validate"}, "Validate the file given with -f and exit"};
    QCommandLineOption include{QStringList{"I", "include"}, "Include directory for DTS sources (repeatable)",
                               "dir"};

    void addTo(QCommandLineParser& parser) {
        parser.setApplicationDescription(
            "Device Tree Explorer - visualize, edit and compare Device Tree files.\n\n"
            "Headless use (no display needed):\n"
            "  DeviceTreeExplorer --cli <dte-cli arguments>   e.g. --cli diff a.dtb b.dts\n"
            "  DeviceTreeExplorer -f board.dts -e json -o board.json\n"
            "  DeviceTreeExplorer -f board.dts --validate");
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addOptions({file, diff, exportFormat, output, validate, include});
        parser.addPositionalArgument("file", "Device tree file to open (same as -f)", "[file]");
    }
};

// True if the arguments ask for a headless operation, so no QApplication
// (and no display) is created.
bool wantsHeadless(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--validate" || a == "-e" || a == "--export" || a.rfind("--export=", 0) == 0 ||
            (a.size() > 2 && a.rfind("-e", 0) == 0 && a[1] == 'e')) {
            return true;
        }
    }
    return false;
}

// Runs the dte-cli command line (argv[0] plus `args`).
int runCli(const char* program, const std::vector<std::string>& args) {
    std::vector<std::string> storage{program};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : storage) argv.push_back(s.data());
    argv.push_back(nullptr);
    try {
        DTE::CLIApp cli(static_cast<int>(storage.size()), argv.data());
        return cli.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

QString fileArgument(const QCommandLineParser& parser, const Options& o) {
    if (parser.isSet(o.file)) return parser.value(o.file);
    const auto positional = parser.positionalArguments();
    return positional.isEmpty() ? QString() : positional.first();
}

std::vector<std::string> includeArgs(const QCommandLineParser& parser, const Options& o) {
    std::vector<std::string> out;
    for (const auto& dir : parser.values(o.include)) {
        out.push_back("-I");
        out.push_back(dir.toStdString());
    }
    return out;
}

// -e/--export and --validate, mapped onto the dte-cli commands so output
// and exit codes are identical.
int runHeadless(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    setApplicationInfo();
    QCommandLineParser parser;
    Options o;
    o.addTo(parser);
    parser.process(app);

    const QString file = fileArgument(parser, o);
    if (file.isEmpty()) {
        std::cerr << "Error: --export and --validate need a file (-f FILE or a positional argument)\n";
        return EXIT_FAILURE;
    }

    auto args = includeArgs(parser, o);
    if (parser.isSet(o.validate)) {
        if (parser.isSet(o.exportFormat)) {
            std::cerr << "Error: use either --export or --validate, not both\n";
            return EXIT_FAILURE;
        }
        args.insert(args.end(), {"validate", file.toStdString()});
    } else {
        if (!parser.isSet(o.output)) {
            std::cerr << "Error: --export needs an output file (-o FILE)\n";
            return EXIT_FAILURE;
        }
        args.insert(args.end(), {"export", file.toStdString(), parser.value(o.exportFormat).toStdString(),
                                 parser.value(o.output).toStdString()});
    }
    return runCli(argv[0], args);
}

int runGui(int argc, char* argv[]) {
    QApplication app(argc, argv);
    setApplicationInfo();

    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }
    QTranslator appTranslator;
    if (appTranslator.load("dte_" + QLocale::system().name(),
                           QStandardPaths::locate(QStandardPaths::AppDataLocation, "translations",
                                                  QStandardPaths::LocateDirectory))) {
        app.installTranslator(&appTranslator);
    }

    QCommandLineParser parser;
    Options o;
    o.addTo(parser);
    parser.process(app);

    QSettings settings;
    const QString theme = settings.value("appearance/theme", "default").toString();
    if (theme != "default") {
        app.setStyle(QStyleFactory::create(theme));
    }

    DTE::MainWindow window;
    const QString file = fileArgument(parser, o);
    if (!file.isEmpty()) {
        window.loadDeviceTree(file);
    }
    if (parser.isSet(o.diff)) {
        window.openDiffFile(parser.value(o.diff));
    }

    window.show();
    const QRect geometry = settings.value("window/geometry").toRect();
    if (geometry.isValid()) {
        window.setGeometry(geometry);
    }
    window.setWindowState(static_cast<Qt::WindowStates>(settings.value("window/state", Qt::WindowNoState).toInt()));

    return app.exec();
}

} // namespace

int main(int argc, char* argv[]) {
    // --cli forwards everything after it to the dte-cli command set.
    if (argc > 1 && (std::strcmp(argv[1], "--cli") == 0 || std::strcmp(argv[1], "-c") == 0)) {
        return runCli(argv[0], std::vector<std::string>(argv + 2, argv + argc));
    }
    if (wantsHeadless(argc, argv)) {
        return runHeadless(argc, argv);
    }
    return runGui(argc, argv);
}
