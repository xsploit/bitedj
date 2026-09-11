#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QPushButton;
class QTableView;
class QCloseEvent;
class QPlainTextEdit;
class TrackCollectionManager;

namespace mixxx {
class EngineReaderService;
class EngineImportCoordinator;
class EnginePreviewModel;

// Preview and explicit metadata/playlist Apply workflow. Reading alone never
// changes the library. Timing imports remain deferred.
class DlgEngineImport : public QDialog {
    Q_OBJECT
  public:
    explicit DlgEngineImport(QWidget* parent = nullptr, TrackCollectionManager* manager = nullptr);

  public slots:
    void openLibrary(const QString& folder);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void setBusy(bool busy);
    void displayPackage(const QJsonObject& package);
    EngineReaderService* m_reader;
    EngineImportCoordinator* m_importer;
    QPushButton* m_apply;
    QPlainTextEdit* m_details;
    QJsonObject m_package;
    QString m_libraryDirectory;
    QString m_mediaRoot;
    EnginePreviewModel* m_model;
    QLabel* m_status;
    QPushButton* m_choose;
    QPushButton* m_cancel;
    QTableView* m_tracks;
};
} // namespace mixxx
