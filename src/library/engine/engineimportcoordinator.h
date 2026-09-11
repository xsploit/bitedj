#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <memory>

#include "track/trackid.h"

class TrackCollectionManager;
namespace mixxx {
class EngineMediaResolver;
// Uses the ordinary live metadata-edit/save path. Tracks commit individually;
// each playlist and its provenance commit together. Cancellation is between
// items and retains completed work. Timing data is recorded, never applied.
class EngineImportCoordinator : public QObject {
    Q_OBJECT
  public:
    explicit EngineImportCoordinator(TrackCollectionManager* manager, QObject* parent = nullptr);
    ~EngineImportCoordinator() override;
    bool start(const QJsonObject& package, const QString& libraryDirectory,
            const QString& mediaRoot, QString* error);
    bool isRunning() const { return m_running; }
    void cancel();
  signals:
    void progress(int completed, int total);
    void finished(int tracks, int playlists, int attention, bool cancelled,
            const QStringList& details);
  private:
    void step();
    void importTrack(const QJsonObject& source);
    void importPlaylist(const QJsonObject& source);
    void issue(const QString& entity, const QString& message);
    QString playlistName(const QJsonObject& source) const;
    TrackCollectionManager* m_manager;
    std::unique_ptr<EngineMediaResolver> m_media;
    QString m_uuid;
    QJsonArray m_tracks;
    QJsonArray m_playlists;
    QHash<QString, QJsonObject> m_playlistById;
    QHash<QString, TrackId> m_resolvedTracks;
    QStringList m_details;
    int m_index = 0;
    int m_importedTracks = 0;
    int m_importedPlaylists = 0;
    int m_attention = 0;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_portableRatingsDeferred = false;
};
} // namespace mixxx
