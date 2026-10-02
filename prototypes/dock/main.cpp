// SPDX-License-Identifier: GPL-3.0-or-later
#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/ViewFactory.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

namespace {

// Replaces KDDockWidgets' default chrome with Spectrum-styled QML.
class SpectrumViewFactory : public KDDockWidgets::QtQuick::ViewFactory {
 public:
  QUrl titleBarFilename() const override { return Url("SpectrumTitleBar.qml"); }
  QUrl tabbarFilename() const override { return Url("SpectrumTabBar.qml"); }
  QUrl separatorFilename() const override { return Url("SpectrumSeparator.qml"); }
  QUrl groupFilename() const override { return Url("SpectrumGroup.qml"); }
  QUrl floatingWindowFilename() const override { return Url("SpectrumFloatingWindow.qml"); }
  // These two hooks come from patches/kddw-indicators.patch.
  QUrl classicIndicatorsOverlayFilename() const override {
    return Url("SpectrumIndicatorsOverlay.qml");
  }
  QUrl rubberBandFilename() const override { return Url("SpectrumRubberBand.qml"); }

 private:
  static QUrl Url(const char* file) {
    return QUrl(QStringLiteral("qrc:/dock/qml/") + QLatin1String(file));
  }
};

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  const QStringList args = QCoreApplication::arguments();

  // Vulkan as in the real app. --backend=opengl|d3d11 is for comparing how
  // the drop-indicator overlay (a translucent top-level window) renders; the
  // canvas only works on Vulkan.
  QSGRendererInterface::GraphicsApi api = QSGRendererInterface::Vulkan;
  if (args.contains(QStringLiteral("--backend=opengl"))) api = QSGRendererInterface::OpenGL;
  if (args.contains(QStringLiteral("--backend=d3d11"))) api = QSGRendererInterface::Direct3D11;
  QQuickWindow::setGraphicsApi(api);

  KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);

  auto& config = KDDockWidgets::Config::self();
  // Illustrator-like panels: always show tabs, and let the tab strip act as
  // the title bar instead of stacking a title bar above it.
  config.setFlags(config.flags() | KDDockWidgets::Config::Flag_AlwaysShowTabs |
                  KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible |
                  KDDockWidgets::Config::Flag_TitleBarIsFocusable);
  // With the patch, this draws the drop indicators inside the hovered window.
  // A translucent indicator window shows up black under Vulkan on Windows.
  // --translucent-indicators restores the original behaviour for comparison.
  if (!args.contains(QStringLiteral("--translucent-indicators"))) {
    config.setInternalFlags(config.internalFlags() |
                            KDDockWidgets::Config::InternalFlag_DisableTranslucency);
  }
  config.setSeparatorThickness(2);
  config.setViewFactory(new SpectrumViewFactory());

  QQmlApplicationEngine engine;
  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
  engine.load(QUrl(QStringLiteral("qrc:/dock/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) return 1;
  return app.exec();
}
