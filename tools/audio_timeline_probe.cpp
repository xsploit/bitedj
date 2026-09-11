// Read-only diagnostic against the application's actual provider selection.
// A zero-based range does not establish agreement with another DJ application's
// encoder-delay convention. Do not use this report alone to authorize cue import.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <vector>

#include "sources/soundsourceproxy.h"
#include "track/track.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3 || !SoundSourceProxy::registerProviders()) {
        return 2;
    }
    QJsonArray tracks;
    for (int i = 2; i < argc; ++i) {
        const auto path = QString::fromLocal8Bit(argv[i]);
        SoundSourceProxy proxy(Track::newTemporary(path));
        auto audio = proxy.openAudioSource();
        if (!audio) {
            return 3;
        }
        const auto channels = audio->getSignalInfo().getChannelCount().value();
        const auto rate = audio->getSignalInfo().getSampleRate().value();
        const auto first = audio->frameIndexMin();
        const auto end = audio->frameIndexMax();
        const auto count = std::min<SINT>(64, audio->frameLength());
        if (channels <= 0 || count <= 0) {
            audio->close();
            return 4;
        }
        std::vector<CSAMPLE> buffer(count * channels);
        const auto read = audio->readSampleFrames(mixxx::WritableSampleFrames(
                mixxx::IndexRange::forward(first, count),
                mixxx::SampleBuffer::WritableSlice(buffer.data(), buffer.size())));
        const auto readCount = read.frameLength();
        tracks.append(QJsonObject{
                {"file", path},
                {"provider", proxy.getProvider()->getDisplayName()},
                {"sampleRate", int(rate)},
                {"channels", int(channels)},
                {"firstFrame", QString::number(first)},
                {"endFrame", QString::number(end)},
                {"frameLength", QString::number(audio->frameLength())},
                {"firstReadFrames", int(readCount)}});
        audio->close();
        if (readCount != count) {
            return 5;
        }
    }
    QFile report(QString::fromLocal8Bit(argv[1]));
    const auto bytes = QJsonDocument(tracks).toJson();
    return report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() ? 0 : 6;
}
