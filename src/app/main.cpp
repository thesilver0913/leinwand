// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
  // Skia draws with Qt Quick's own Vulkan device (M0 check 1).
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

  QGuiApplication app(argc, argv);
  // Fusion follows the palette fully, so the Spectrum colors set in Main.qml
  // apply to every control. Spectrum components replace it in M5.
  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  // --no-vsync: measure throughput beyond the display refresh rate.
  if (QCoreApplication::arguments().contains(QStringLiteral("--no-vsync"))) {
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);
  }

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("Leinwand", "Main");
  return app.exec();
}
