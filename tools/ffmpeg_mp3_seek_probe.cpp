#include <QCoreApplication>
#include <QUrl>
#include <cstdio>
#include <vector>
#include <cstring>
#include "sources/soundsourceffmpeg.h"
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 if(argc!=2)return 2;
 mixxx::SoundSourceFFmpeg source(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
 auto opened=source.open(mixxx::AudioSource::OpenMode::Strict,{});
 if(opened!=mixxx::AudioSource::OpenResult::Succeeded)return 3;
 const SINT channels=source.getSignalInfo().getChannelCount().value();
 std::vector<CSAMPLE> samples(1024*channels);
 std::vector<CSAMPLE> reference;
 for(SINT start=0;start<131072;start+=1024) {
  auto result=source.readSampleFrames(mixxx::WritableSampleFrames(mixxx::IndexRange::forward(start,1024),mixxx::SampleBuffer::WritableSlice(samples.data(),samples.size())));
  if(result.frameLength()!=1024)return 4;
  reference.insert(reference.end(),result.readableData(),result.readableData()+1024*channels);
 }
 for(SINT start : {SINT(0),SINT(44100),SINT(0),SINT(88200),SINT(1)}) {
  auto range=mixxx::IndexRange::forward(start,1024);
  auto result=source.readSampleFrames(mixxx::WritableSampleFrames(range,mixxx::SampleBuffer::WritableSlice(samples.data(),samples.size())));
  std::printf("start=%lld frames=%lld\n",(long long)start,(long long)result.frameLength());
  if(result.frameLength()!=1024)return 4;
  if(std::memcmp(result.readableData(),reference.data()+start*channels,1024*channels*sizeof(CSAMPLE))!=0)return 5;
 }
 std::puts("All seek blocks exactly match sequential decoder samples");
 source.close();return 0;
}
