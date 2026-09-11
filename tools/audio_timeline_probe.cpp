// Read-only diagnostic against the application's actual provider selection.
// A zero-based range does not establish agreement with another DJ application's
// encoder-delay convention. Do not use this report alone to authorize cue import.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstring>
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
        QFile pcm(QString::fromLocal8Bit(argv[1]) + QStringLiteral(".%1.f32").arg(i - 2));
        if (!pcm.open(QIODevice::WriteOnly)) {
            audio->close();
            return 7;
        }
        std::vector<CSAMPLE> block(1024 * channels);
        for (SINT pos = first; pos < end;) {
            const auto frames = std::min<SINT>(1024, end - pos);
            const auto decoded = audio->readSampleFrames(mixxx::WritableSampleFrames(
                    mixxx::IndexRange::forward(pos, frames),
                    mixxx::SampleBuffer::WritableSlice(block.data(), block.size())));
            if (decoded.frameLength() != frames ||
                    pcm.write(reinterpret_cast<const char*>(decoded.readableData()),
                            frames * channels * sizeof(CSAMPLE)) !=
                            frames * channels * sizeof(CSAMPLE)) {
                audio->close();
                return 8;
            }
            pos += frames;
        }
        pcm.close();
        const auto tailBytes = count * channels * sizeof(CSAMPLE);
        if (!pcm.open(QIODevice::ReadOnly) || !pcm.seek(pcm.size() - tailBytes)) {
            audio->close();
            return 9;
        }
        const auto tail = pcm.read(tailBytes);
        pcm.close();
        // Force a nonsequential seek away from the end before checking it.
        audio->readSampleFrames(mixxx::WritableSampleFrames(
                mixxx::IndexRange::forward(first, count),
                mixxx::SampleBuffer::WritableSlice(buffer.data(), buffer.size())));
        const auto endRead = audio->readSampleFrames(mixxx::WritableSampleFrames(
                mixxx::IndexRange::forward(end - count, count),
                mixxx::SampleBuffer::WritableSlice(buffer.data(), buffer.size())));
        const bool tailMatches = endRead.frameLength() == count &&
                tail.size() == qint64(tailBytes) &&
                std::memcmp(tail.constData(), endRead.readableData(), tailBytes) == 0;
        // Byte inequality alone cannot distinguish a timing error from tiny
        // floating-point decoder differences. Keep the exact check and report
        // numerical magnitude separately, without inventing a quality gate.
        const bool tailSameLength = endRead.frameLength() == count &&
                tail.size() == qint64(tailBytes);
        bool tailFinite = tailSameLength;
        double tailMaxError = 0, tailSquaredError = 0;
        if (tailSameLength) {
            for (SINT sample = 0; sample < count * channels; ++sample) {
                CSAMPLE expected;
                std::memcpy(&expected, tail.constData() + sample * sizeof(CSAMPLE), sizeof(CSAMPLE));
                const auto actual = endRead.readableData()[sample];
                if (!std::isfinite(expected) || !std::isfinite(actual)) {
                    tailFinite = false;
                    break;
                }
                const double error = double(actual) - double(expected);
                tailMaxError = std::max(tailMaxError, std::abs(error));
                tailSquaredError += error * error;
            }
        }
        tracks.append(QJsonObject{
                {"file", path},
                {"provider", proxy.getProvider()->getDisplayName()},
                {"sampleRate", int(rate)},
                {"channels", int(channels)},
                {"firstFrame", QString::number(first)},
                {"endFrame", QString::number(end)},
                {"frameLength", QString::number(audio->frameLength())},
                {"firstReadFrames", int(readCount)},
                {"tailSeekMatchesSequential", tailMatches},
                {"tailSeekFrames", int(endRead.frameLength())},
                {"tailSeekReferenceFrames", int(count)},
                {"tailSeekFiniteAndSameLength", tailFinite},
                {"tailSeekMaxAbsDifference", tailFinite ? QJsonValue(tailMaxError) : QJsonValue(QJsonValue::Null)},
                {"tailSeekRmsDifference", tailFinite ? QJsonValue(std::sqrt(tailSquaredError / (count * channels))) : QJsonValue(QJsonValue::Null)}});
        audio->close();
        if (readCount != count) {
            return 5;
        }
    }
    QFile report(QString::fromLocal8Bit(argv[1]));
    const auto bytes = QJsonDocument(tracks).toJson();
    return report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size() ? 0 : 6;
}
