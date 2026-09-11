#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QStringList>
#include <iostream>
#include <stdexcept>
#include "track/track.h"

namespace mixxx::rekordbox {
QStringList readAnalyzeFiles(TrackPointer, audio::SampleRate, int, const QString&, bool);
}
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void u32(std::string& bytes, unsigned value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes += char(value >> shift);
}
std::string entry(unsigned declared, unsigned comment = 0) {
    std::string bytes = "PCP2";
    u32(bytes, 16); u32(bytes, declared); u32(bytes, 1);
    bytes += char(1); bytes.append(3, '\0');
    u32(bytes, 1000); u32(bytes, 0); bytes.append(12, '\0');
    if (declared >= 44) u32(bytes, comment);
    return bytes;
}
void writeAnalysis(const QString& path, const std::string& record) {
    std::string section = "PCO2";
    u32(section, 20); u32(section, 20 + record.size()); u32(section, 1);
    section.append("\0\1\0\0", 4); section += record;
    std::string bytes = "PMAI";
    u32(bytes, 28); u32(bytes, 28 + section.size()); bytes.append(16, '\0');
    bytes += section;
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "open fixture");
    require(file.write(bytes.data(), bytes.size()) == qint64(bytes.size()), "write fixture");
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory");
    const auto path = directory.filePath("ANLZ.DAT");
    auto track = Track::newTemporary();
    auto cue = track->createAndAddCue(mixxx::CueType::HotCue, 0,
            mixxx::audio::FramePos(441), mixxx::audio::kInvalidFramePos,
            mixxx::RgbColor(0x123456));
    cue->setLabel("Preserve me");
    for (const auto& bad : {entry(12), entry(44, 4)}) {
        writeAnalysis(path, bad + std::string(128, 'X'));
        for (bool importBeats : {false, true}) {
            const auto failed = mixxx::rekordbox::readAnalyzeFiles(
                    track, mixxx::audio::SampleRate(44100), 0, path, importBeats);
            require(failed == QStringList{path}, "real importer did not report malformed file");
            require(track->getCuePoints() == QList<CuePointer>{cue}, "cue object/list changed");
            require(cue->getLabel() == "Preserve me", "cue label changed");
            require(cue->getPosition() == mixxx::audio::FramePos(441), "cue moved");
            require(cue->getColor() == mixxx::RgbColor(0x123456), "cue color changed");
        }
    }
    // A subsequent valid short cue must import successfully without optional RGB.
    writeAnalysis(path, entry(40));
    require(mixxx::rekordbox::readAnalyzeFiles(track,
                    mixxx::audio::SampleRate(44100), 0, path, false).isEmpty(),
            "valid import failed after malformed input");
    require(track->getCuePoints() == QList<CuePointer>{cue}, "existing cue replaced");
    require(cue->getPosition() == mixxx::audio::FramePos(44100), "valid cue not imported");
    require(cue->getColor() == mixxx::RgbColor(0x123456), "missing RGB replaced color");
    std::cout << "PASS: real ANLZ import catches both validation errors, preserves cues, and recovers with optional color fallback\n";
}
