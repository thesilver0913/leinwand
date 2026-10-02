// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

int main(int argc, char* argv[]) {
  // Skia draws with Qt Quick's own Vulkan device (M0 check 1).
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);

  QGuiApplication app(argc, argv);
  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("Leinwand", "Main");
  return app.exec();
}
