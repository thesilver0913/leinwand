// SPDX-License-Identifier: GPL-3.0-or-later
#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/ViewFactory.h>

#include <QDir>
#include <QEventLoop>
#include <QFileOpenEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#include <QSurfaceFormat>
#include <QTimer>
#include <QTranslator>
#include <filesystem>
#include <memory>

#include "icon_provider.h"
#include "preferences.h"
#include "render/skia_font_source.h"
#include "session.h"
#include "shortcuts.h"
#include "spectrum_theme.h"
#include "text/font.h"

namespace {

// KDDockWidgets' chrome in Spectrum style (M0 check 3).
class SpectrumViewFactory : public KDDockWidgets::QtQuick::ViewFactory {
 public:
  QUrl titleBarFilename() const override { return Url("SpectrumTitleBar.qml"); }
  QUrl tabbarFilename() const override { return Url("SpectrumTabBar.qml"); }
  QUrl separatorFilename() const override { return Url("SpectrumSeparator.qml"); }
  QUrl groupFilename() const override { return Url("SpectrumGroup.qml"); }
  QUrl floatingWindowFilename() const override { return Url("SpectrumFloatingWindow.qml"); }
  // These two hooks come from cmake/patches/kddw-indicators.patch.
  QUrl classicIndicatorsOverlayFilename() const override {
    return Url("SpectrumIndicatorsOverlay.qml");
  }
  QUrl rubberBandFilename() const override { return Url("SpectrumRubberBand.qml"); }

 private:
  static QUrl Url(const char* file) {
    return QUrl(QStringLiteral("qrc:/dock/qml/") + QLatin1String(file));
  }
};

void ConfigureDocking() {
  KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
  auto& config = KDDockWidgets::Config::self();
  // Illustrator-like panels: always show tabs, and let the tab strip act as
  // the title bar instead of stacking a title bar above it.
  config.setFlags(config.flags() | KDDockWidgets::Config::Flag_AlwaysShowTabs |
                  KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible |
                  KDDockWidgets::Config::Flag_TitleBarIsFocusable);
  // With the patch, the drop indicators are drawn inside the hovered window;
  // a translucent indicator window shows up black under Vulkan on Windows.
  config.setInternalFlags(config.internalFlags() |
                          KDDockWidgets::Config::InternalFlag_DisableTranslucency);
  config.setSeparatorThickness(2);
  config.setViewFactory(new SpectrumViewFactory());
}

// Files shipped with the program: next to the executable, or in the app
// bundle's Resources folder on macOS.
QString ResourceDir() {
#ifdef Q_OS_MACOS
  return QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources");
#else
  return QCoreApplication::applicationDirPath();
#endif
}

// The UI language: the preference, or the system's (Japanese or English).
QString Language() {
  const QString chosen = Preferences::instance()->Text(QStringLiteral("language"));
  if (chosen == QLatin1String("ja") || chosen == QLatin1String("en")) return chosen;
  return QLocale::system().language() == QLocale::Japanese ? QStringLiteral("ja")
                                                           : QStringLiteral("en");
}

// Translations of Leinwand and of Qt's own dialogs (spec 7.3).
class Translations {
 public:
  void Apply(const QString& language) {
    QCoreApplication::removeTranslator(&app_);
    QCoreApplication::removeTranslator(&qt_);
    if (app_.load(QStringLiteral(":/i18n/leinwand_%1.qm").arg(language))) {
      QCoreApplication::installTranslator(&app_);
    }
    if (qt_.load(QStringLiteral("qt_%1").arg(language),
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)) ||
        qt_.load(QStringLiteral("qt_%1").arg(language),
                 ResourceDir() + QStringLiteral("/translations"))) {
      QCoreApplication::installTranslator(&qt_);
    }
    // Source Han Sans has the Latin of Source Sans; Japanese needs it.
    const QString family = language == QLatin1String("ja") ? QStringLiteral("Source Han Sans JP")
                                                           : QStringLiteral("Source Sans 3");
    QFont font = QGuiApplication::font();
    font.setFamilies({family, QStringLiteral("Source Han Sans JP")});
    QGuiApplication::setFont(font);
    family_ = family;
  }
  QString family() const { return family_; }

 private:
  QTranslator app_;
  QTranslator qt_;
  QString family_;
};

// The bundled UI fonts (spec 7: Source Sans 3, Source Han Sans), in fonts/
// beside the program. The text engine uses them too, then the OS fonts
// (spec 5.2).
void LoadFonts() {
  const QDir dir(ResourceDir() + QStringLiteral("/fonts"));
  leinwand::text::SetFontSources({std::make_shared<leinwand::text::FolderFontSource>(
                                      std::filesystem::path(dir.absolutePath().toStdWString())),
                                  leinwand::render::MakeSystemFontSource()});
  for (const QString& file : dir.entryList({QStringLiteral("*.otf")}, QDir::Files)) {
    QFontDatabase::addApplicationFont(dir.absoluteFilePath(file));
  }
}

// macOS hands over files to open (double-click, "Open With", a drop on the
// Dock icon) as events rather than as arguments.
class FileOpenFilter : public QObject {
 public:
  explicit FileOpenFilter(Session* session) : session_(session) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::FileOpen) return QObject::eventFilter(watched, event);
    const QString path = static_cast<QFileOpenEvent*>(event)->file();
    // Queued, so that the main window and its panels are set up first.
    QTimer::singleShot(0, session_, [session = session_, path] { session->openPath(path); });
    return true;
  }

 private:
  Session* session_;
};

}  // namespace

int main(int argc, char* argv[]) {
  // Skia draws with Qt Quick's own GPU device (M0 check 1): Vulkan, or Metal
  // on macOS.
#ifdef Q_OS_MACOS
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#else
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#endif
  // The UI scale (preferences) must be known before Qt starts.
  const double scale = Preferences::ReadEarly(QStringLiteral("uiScale")).toDouble();
  if (scale > 0 && scale != 100 && !qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
    qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));
  }

  QGuiApplication app(argc, argv);
  // Names the data folder (%LOCALAPPDATA%\Leinwand: autosave recovery).
  QCoreApplication::setApplicationName(QStringLiteral("Leinwand"));
  QCoreApplication::setApplicationVersion(QStringLiteral(LEINWAND_VERSION));
  QIcon icon;
  for (int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
    icon.addFile(QStringLiteral(":/resources/icons/app/leinwand-%1.png").arg(size),
                 QSize(size, size));
  }
  QGuiApplication::setWindowIcon(icon);
  // The Spectrum components are built on Qt Quick Templates; the few stock
  // controls left (scroll bars, tooltips) use Fusion, which follows the
  // palette set from the Spectrum tokens.
  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  // --no-vsync: measure throughput beyond the display refresh rate.
  if (QCoreApplication::arguments().contains(QStringLiteral("--no-vsync"))) {
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);
  }

  Preferences preferences;  // The QML singleton `Preferences`.
  Shortcuts shortcuts;      // The QML singleton `Shortcuts`.
  Translations translations;
  LoadFonts();
  translations.Apply(Language());

  QQmlApplicationEngine engine;
  engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
  engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

  // Theme and font for every window, following the preferences.
  auto* theme = engine.singletonInstance<SpectrumTheme*>("Leinwand", "Spectrum");
  auto apply_theme = [&] {
    const QString choice = preferences.Text(QStringLiteral("theme"));
    const bool dark = choice == QLatin1String("system")
                          ? QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light
                          : choice != QLatin1String("light");
    theme->setDark(dark);
    theme->setFontFamily(translations.family());
  };
  apply_theme();
  QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                   apply_theme);
  QObject::connect(&preferences, &Preferences::changed, &app, [&](const QString& key) {
    if (key == QLatin1String("language")) {
      translations.Apply(Language());
      engine.retranslate();
    }
    apply_theme();
  });

  // Spec 9: the splash screen names each step of the start-up as it runs,
  // with a progress bar (the share of the start-up done before the step).
  engine.loadFromModule("Leinwand", "Splash");
  QObject* splash = engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().constLast();
  auto step = [&](const QString& name, double progress) {
    if (splash) {
      splash->setProperty("step", name);
      splash->setProperty("progress", progress);
    }
    QCoreApplication::processEvents();
  };

  // --hold-splash: keep the splash up for a few seconds (to look at it).
  if (QCoreApplication::arguments().contains(QStringLiteral("--hold-splash"))) {
    step(QCoreApplication::translate("Startup", "Setting up the panels..."), 0.35);
    QEventLoop wait;
    QTimer::singleShot(6000, &wait, &QEventLoop::quit);
    wait.exec();
  }
  step(QCoreApplication::translate("Startup", "Preparing the document..."), 0.1);
  Session session;  // The QML singleton `Session`.
  FileOpenFilter file_open(&session);
  app.installEventFilter(&file_open);
  step(QCoreApplication::translate("Startup", "Setting up the panels..."), 0.35);
  ConfigureDocking();
  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
  // Building the window takes the longest.
  step(QCoreApplication::translate("Startup", "Building the window..."), 0.5);
  engine.loadFromModule("Leinwand", "Main");
  step(QCoreApplication::translate("Startup", "Ready"), 1.0);
  if (splash) QMetaObject::invokeMethod(splash, "close");
  return app.exec();
}
