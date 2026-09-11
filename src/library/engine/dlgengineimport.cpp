#include "library/engine/dlgengineimport.h"

#include <QAbstractTableModel>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScroller>
#include <QTableView>
#include <QVBoxLayout>

#include "library/engine/enginereaderservice.h"
#include "library/engine/engineimportcoordinator.h"
#include "moc_dlgengineimport.cpp"

namespace mixxx {
class EnginePreviewModel : public QAbstractTableModel {
  public:
    explicit EnginePreviewModel(QObject* parent) : QAbstractTableModel(parent) {
    }
    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : m_tracks.size();
    }
    int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : 6;
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return {};
        const QStringList headers{tr("Title"), tr("Artist"), tr("Length"), tr("Hot cues"), tr("Saved loops"), tr("Audio")};
        return section >= 0 && section < headers.size() ? headers[section] : QVariant{};
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size() || role != Qt::DisplayRole)
            return {};
        const auto track = m_tracks[index.row()].toObject();
        switch (index.column()) {
        case 0:
            return track["title"].toString().isEmpty() ? tr("Untitled") : track["title"].toString();
        case 1:
            return track["artist"].toString();
        case 2: {
            if (!track["durationMs"].isDouble())
                return {};
            const double duration = track["durationMs"].toDouble();
            if (duration < 0 || duration > 1e15)
                return tr("Unknown");
            const qint64 seconds = static_cast<qint64>(duration / 1000);
            return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
        }
        case 3:
            return track["hotCues"].toArray().size();
        case 4:
            return track["loops"].toArray().size();
        case 5: {
            const auto status = track["media"].toObject()["status"].toString();
            if (status == "resolved")
                return tr("Found");
            if (status == "missing")
                return tr("Missing");
            if (status == "unreadable")
                return tr("Unreadable");
            if (status == "outside-media-root")
                return tr("Outside folder");
            if (status == "unsupported-reference" || status == "invalid-reference")
                return tr("Unsupported path");
            return tr("Not checked");
        }
        default:
            return {};
        }
    }
    void setTracks(const QJsonArray& tracks) {
        beginResetModel();
        m_tracks = tracks;
        endResetModel();
    }

  private:
    QJsonArray m_tracks;
};

namespace {
QString readerLauncher() {
    const auto overridePath = qEnvironmentVariable("BITEDJ_ENGINE_IMPORT_HELPER");
    const auto appDir = QCoreApplication::applicationDirPath();
    const QStringList paths = overridePath.isEmpty()
            ? QStringList{appDir + "/../libexec/bitedj-engine/engine-import.py",
                      appDir + "/libexec/bitedj-engine/engine-import.py"}
            : QStringList{overridePath};
    for (const auto& path : paths) {
        const QFileInfo file(path);
        if (file.isAbsolute() && file.isFile() && file.isExecutable())
            return file.canonicalFilePath();
    }
    return {};
}
} // namespace

DlgEngineImport::DlgEngineImport(QWidget* parent, TrackCollectionManager* manager)
        : QDialog(parent),
          m_reader(new EngineReaderService(this)),
          m_importer(new EngineImportCoordinator(manager, this)),
          m_apply(new QPushButton(tr("Import metadata + playlists"), this)),
          m_details(new QPlainTextEdit(this)),
          m_model(new EnginePreviewModel(this)),
          m_status(new QLabel(tr("Choose your drive or Engine Library folder."), this)),
          m_choose(new QPushButton(tr("Choose library…"), this)),
          m_cancel(new QPushButton(tr("Cancel reading"), this)),
          m_tracks(new QTableView(this)) {
    setObjectName("EngineLibraryPreview");
    setWindowTitle(tr("Engine library preview"));
    resize(960, 600);
    auto* layout = new QVBoxLayout(this);
    auto* buttons = new QHBoxLayout;
    for (auto* button : {m_choose, m_apply, m_cancel}) {
        button->setMinimumHeight(44);
        buttons->addWidget(button);
    }
    auto* close = new QPushButton(tr("Close"), this);
    close->setMinimumHeight(44);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setObjectName("EnginePreviewStatus");
    layout->addWidget(m_status);
    m_tracks->setObjectName("EnginePreviewTracks");
    m_tracks->setModel(m_model);
    m_tracks->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tracks->setSelectionMode(QAbstractItemView::SingleSelection);
    QScroller::grabGesture(m_tracks->viewport(), QScroller::TouchGesture);
    m_tracks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tracks->verticalHeader()->setDefaultSectionSize(44);
    m_tracks->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_tracks);
    m_apply->setObjectName("EngineImportApply");
    m_details->setObjectName("EngineImportDetails");
    m_details->setReadOnly(true);
    m_details->setMaximumHeight(140);
    m_details->hide();
    QScroller::grabGesture(m_details->viewport(), QScroller::TouchGesture);
    layout->addWidget(m_details);
    auto* note = new QLabel(tr("Engine cues, loops and beat grids are not applied yet. Your tag-writing preferences still apply."), this);
    layout->addWidget(note);
    setBusy(false);
    connect(m_choose, &QPushButton::clicked, this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("Choose Engine drive or library"));
        if (!folder.isEmpty())
            openLibrary(folder);
    });
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(this, &QDialog::finished, m_reader, &EngineReaderService::cancel);
    connect(this, &QDialog::finished, m_importer, &EngineImportCoordinator::cancel);
    connect(m_apply, &QPushButton::clicked, this, [this] {
        QString error;
        m_details->clear(); m_details->hide();
        if (!m_importer->start(m_package, m_libraryDirectory, m_mediaRoot, &error)) {
            m_status->setText(error); return;
        }
        setBusy(true);
        m_cancel->setText(tr("Cancel import"));
        m_status->setText(tr("Importing metadata and playlists…"));
    });
    connect(m_importer, &EngineImportCoordinator::progress, this, [this](int completed, int total) {
        m_status->setText(tr("Importing %1 of %2…").arg(completed).arg(total));
    });
    connect(m_importer, &EngineImportCoordinator::finished, this,
            [this](int tracks, int playlists, int attention, bool cancelled, const QStringList& details) {
        setBusy(false);
        m_status->setText(tr("%1 tracks processed · %2 playlists processed · %3 notices%4. Completed changes are kept.")
                .arg(tracks).arg(playlists).arg(attention).arg(cancelled ? tr(" · Cancelled") : QString()));
        m_details->setPlainText(details.join("\n"));
        m_details->setVisible(!details.isEmpty());
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_status->setText(tr("Cancelling…"));
        m_cancel->setEnabled(false);
        m_reader->cancel();
        m_importer->cancel();
    });
    connect(m_reader, &EngineReaderService::packageReady, this, &DlgEngineImport::displayPackage);
    connect(m_reader, &EngineReaderService::failed, this, [this](const QString& error) {
        setBusy(false);
        m_status->setText(error);
    });
    connect(m_reader, &EngineReaderService::cancelled, this, [this] {
        setBusy(false);
        m_status->setText(tr("Reading cancelled. Choose a library to try again."));
    });
}
void DlgEngineImport::setBusy(bool busy) {
    m_choose->setEnabled(!busy);
    m_apply->setEnabled(!busy && !m_package.isEmpty());
    if (!busy) m_cancel->setText(tr("Cancel reading"));
    m_cancel->setEnabled(busy);
}
void DlgEngineImport::openLibrary(const QString& folder) {
    if (m_reader->isRunning() || m_importer->isRunning())
        return;
    m_model->setTracks({});
    m_package = {};
    m_apply->setEnabled(false);
    m_details->clear(); m_details->hide();
    QDir library(folder);
    QString mediaRoot = library.absolutePath();
    if (QFileInfo::exists(library.filePath("Engine Library/Database2/m.db"))) {
        library.cd("Engine Library");
    } else if (QFileInfo::exists(library.filePath("Database2/m.db"))) {
        QDir parent = library;
        parent.cdUp();
        mediaRoot = parent.absolutePath();
    } else {
        m_status->setText(tr("No Engine library was found in this folder. Choose the drive or Engine Library folder."));
        return;
    }
    const auto launcher = readerLauncher();
    if (launcher.isEmpty()) {
        m_status->setText(tr("The Engine library reader component is not installed."));
        return;
    }
    m_libraryDirectory = library.absolutePath();
    m_mediaRoot = mediaRoot;
    setBusy(true);
    m_status->setText(tr("Reading Engine library…"));
    if (!m_reader->start(launcher, library.absolutePath(), mediaRoot)) {
        setBusy(false);
        m_status->setText(tr("Could not start reading. Please try again."));
    }
}
void DlgEngineImport::displayPackage(const QJsonObject& package) {
    m_package = package;
    setBusy(false);
    const auto tracks = package["tracks"].toArray();
    m_model->setTracks(tracks);
    m_status->setText(tr("%1 tracks · %2 playlists").arg(tracks.size()).arg(package["playlists"].toArray().size()));
}
void DlgEngineImport::closeEvent(QCloseEvent* event) {
    m_reader->cancel();
    m_importer->cancel();
    QDialog::closeEvent(event);
}
} // namespace mixxx
