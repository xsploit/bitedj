#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QPushButton;
class QTableView;
class QCloseEvent;

namespace mixxx {
class EngineReaderService;
class EnginePreviewModel;

// Read-only first step of the Engine import workflow. Packages are not an
// authorization to open media or mutate library/Track state.
class DlgEngineImport : public QDialog {
    Q_OBJECT
  public:
    explicit DlgEngineImport(QWidget* parent = nullptr);

  public slots:
    void openLibrary(const QString& folder);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void setBusy(bool busy);
    void displayPackage(const QJsonObject& package);
    EngineReaderService* m_reader;
    EnginePreviewModel* m_model;
    QLabel* m_status;
    QPushButton* m_choose;
    QPushButton* m_cancel;
    QTableView* m_tracks;
};
} // namespace mixxx
