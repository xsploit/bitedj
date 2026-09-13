#pragma once

#include <QChar>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

class QThread;
namespace mixxx {
// Own on the GUI thread. Reading, strict JSON parsing and package validation run
// on a private worker. No Track/DAO mutation occurs in this service.
class EngineReaderService : public QObject {
    Q_OBJECT
  public:
    explicit EngineReaderService(QObject* parent = nullptr);
    ~EngineReaderService() override;
    bool start(const QString& launcher, const QString& library, const QString& mediaRoot);
    bool isRunning() const;

  public slots:
    void cancel();

  signals:
    void packageReady(const QJsonObject& package);
    void failed(const QString& message);
    void cancelled();

  private:
    bool m_busy = false;
    std::unique_ptr<QThread> m_worker;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
} // namespace mixxx
