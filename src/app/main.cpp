// SPDX-License-Identifier: GPL-3.0-or-later
#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/ViewFactory.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include "icon_provider.h"
#include "session.h"

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

}  // namespace

int main(int argc, char* argv[]) {
  // Skia draws with Qt Quick's own Vulkan device (M0 check 1).
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

  QGuiApplication app(argc, argv);
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
  ConfigureDocking();

  Session session;  // The QML singleton `Session`.
  QQmlApplicationEngine engine;
  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
  engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("Leinwand", "Main");
  return app.exec();
}
