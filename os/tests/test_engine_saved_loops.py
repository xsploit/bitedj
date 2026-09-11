"""Compile the exporter's exact saved-loop block; round-trip real Engine DBs.
Cue/Track getters are fixtures, not a full BiteDJ application build.
"""
from pathlib import Path
import subprocess,shlex,tempfile,argparse
from native_test_support import fpclassify_object
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--djinterop-source',type=Path,required=True)
parser.add_argument('--djinterop-build',type=Path,required=True)
parser.add_argument('--gsl-include',type=Path,required=True)
args=parser.parse_args()
code=(root/'src/library/export/engineprimeexportjob.cpp').read_text()
block=code[code.index('    // Export numbered saved loops'):code.index('    // Write waveform.')]
src=r'''
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <iostream>
#include <QColor>
#include <QDebug>
#include <QString>
#include <QTemporaryDir>
#include <djinterop/djinterop.hpp>
namespace mixxx { struct RgbColor { static QColor toQColor(QColor v) { return v; } }; }
enum class CueType { HotCue, Loop };
#include "audio/frame.h"
using Pos=mixxx::audio::FramePos;
struct Cue {
 static constexpr int kNoHotCue=-1;
 CueType type;int slot;double start,end;QString label;QColor color;
 CueType getType()const{return type;} int getHotCue()const{return slot;}
 struct Positions {Pos startPosition,endPosition;};
 Positions getStartAndEndPosition()const{return {Pos(start),Pos(end)};}
 QString getLabel()const{return label;} QColor getColor()const{return color;}
};
using CuePointer=std::shared_ptr<Cue>;
struct Track {int getId()const{return 123;}};
static constexpr int kMaxSavedLoops=8;
void apply(djinterop::track_snapshot& snapshot,const std::vector<CuePointer>& cues,int64_t frameCount) {
 Track track;auto* pTrack=&track;
'''+block+r'''
}
CuePointer cue(int slot,double start,double end,QString label="",CueType type=CueType::Loop){return std::make_shared<Cue>(Cue{type,slot,start,end,label,QColor(12,34,56)});}
int main(){
 int cases=0;
 for(auto schema:{djinterop::engine::engine_schema::schema_2_18_0,djinterop::engine::engine_schema::schema_3_0_0}){
  for(int rate:{44100,48000}){
   djinterop::track_snapshot initial;initial.relative_path="../fixture.wav";initial.title="Loop fixture";
   initial.sample_rate=rate;initial.sample_count=rate*10;initial.duration=std::chrono::milliseconds(10000);
   initial.loops.resize(8);initial.loops[3]=djinterop::loop{"Keep",100,200,{1,2,3,255}};
   initial.hot_cues.push_back(djinterop::hot_cue{"Keep cue",0,{1,2,3,255}});initial.hot_cues.resize(8);
   auto updated=initial;
   apply(updated,{cue(0,0,rate*2,"Intro"),cue(7,rate*8,rate*10),cue(0,0,rate,"Duplicate")},rate*10);
   assert(updated.loops[0]->label=="Intro" && updated.loops[0]->end_sample_offset==rate*2);
   assert(updated.loops[7]->label=="Loop 8" && updated.loops[7]->start_sample_offset==rate*8);
   assert((updated.loops[0]->color==djinterop::pad_color{12,34,56,255}));
   assert(updated.loops[3]==initial.loops[3] && updated.hot_cues==initial.hot_cues);
   const auto before=updated;
   for(auto c:{cue(-1,0,10),cue(-2,0,10),cue(8,0,10),cue(1,-1,10),cue(1,10,10),cue(1,11,10),cue(1,0,rate*10+1),cue(1,NAN,10),cue(1,0,INFINITY),cue(1,0,10,"hot",CueType::HotCue)}){
    apply(updated,{c},rate*10);assert(updated==before);++cases;
   }
   QTemporaryDir dir;assert(dir.isValid());int64_t id;
   {auto db=djinterop::engine::create_database(dir.path().toStdString(),schema);auto track=db.create_track(initial);id=track.id();track.update(updated);}
   {auto db=djinterop::engine::load_database(dir.path().toStdString());auto track=db.track_by_id(id);assert(track);auto actual=track->snapshot();assert(actual.loops==updated.loops);assert(actual.hot_cues==updated.hot_cues);}
   ++cases;
  }
 }
 std::cout<<cases<<" mapping checks including four real database close/reopen round-trips passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'probe.cpp').write_text(src)
 flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','Qt6Core','Qt6Gui'],text=True))
 lib=args.djinterop_build.resolve()
 fp=fpclassify_object(p)
 cmd=['c++','-std=c++20','-O3','-ffast-math','-Wall','-Wextra','-Werror',str(p/'probe.cpp'),fp,'-I'+str(root/'src'),'-I'+str(args.gsl_include.resolve()),'-I'+str(args.djinterop_source.resolve()/'include'),'-I'+str(lib/'include'),'-L'+str(lib),'-Wl,-rpath,'+str(lib),'-ldjinterop',*flags,'-o',str(p/'probe')]
 subprocess.run(cmd,check=True);subprocess.run([str(p/'probe')],check=True)
