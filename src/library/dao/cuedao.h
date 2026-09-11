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

    bool deleteCuesForTrack(TrackId trackId) const;
    bool deleteCuesForTracks(const QList<TrackId>& trackIds) const;

  private:
    bool saveCue(TrackId trackId, Cue* pCue) const;
};
