#pragma once

#include "library/dao/dao.h"
#include "track/cue.h"
#include "track/trackid.h"

#define CUE_TABLE "cues"

class Cue;
class SqlTransaction;

class CueDAO : public DAO {
  public:
    ~CueDAO() override = default;

    QList<CuePointer> getCuesForTrack(TrackId trackId) const;

    void saveTrackCues(TrackId trackId, const QList<CuePointer>& cueList) const;
    // Write snapshots under an active caller transaction. Originals retain IDs,
    // dirty state and QObject identity. Publish returned copies only AFTER the
    // outer commit; discard them on rollback. No model/file side effects occur.
    bool stageTrackCues(const SqlTransaction& transaction,
            TrackId trackId,
            const QList<CuePointer>& cues,
            QList<CuePointer>* staged,
            QString* error) const;

    // Ordinary existing-track saves stage detached objects. Original IDs and
    // dirty flags are accepted only after the enclosing transaction commits.
    class PendingCueSave {
      private:
        QList<CuePointer> originals;
        QList<CuePointer> staged;
        QList<DbId> originalIds;
        friend class CueDAO;
    };
    bool prepareTrackCueSave(const SqlTransaction& transaction,
            TrackId trackId, const QList<CuePointer>& cues,
            PendingCueSave* pending, QString* error) const;
    // Call only after successful outer commit. Keeps edited cues dirty and
    // returns false if a captured cue changed. Never replaces Cue QObjects.
    bool finishTrackCueSave(PendingCueSave* pending) const;

    bool deleteCuesForTrack(TrackId trackId) const;
    bool deleteCuesForTracks(const QList<TrackId>& trackIds) const;

  private:
    bool saveCue(TrackId trackId, Cue* pCue) const;
    bool stageTrackCuesInternal(const SqlTransaction& transaction,
            TrackId trackId, const QList<CuePointer>& cues,
            QList<CuePointer>* staged, QString* error,
            QList<DbId>* originalIds) const;
};
