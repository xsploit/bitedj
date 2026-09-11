#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

#include "library/engine/enginemediaresolver.h"

void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory");
    const auto root = temp.path() + "/usb";
    const auto library = root + "/Engine Library";
    require(QDir().mkpath(library) && QDir().mkpath(root + "/Music") &&
                    QDir().mkpath(temp.path() + "/usb-other"),
            "fixture directories");
    const auto audio = root + "/Music/Original.wav";
    const auto other = temp.path() + "/usb-other/VIP.wav";
    for (const auto& path : {audio, other}) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write("fixture", 7) == 7, "fixture files");
    }
    mixxx::EngineMediaResolver resolver(library, root);
    const auto found = resolver.resolve("../Music/Original.wav");
    require(found["status"] == "resolved" && found["path"] == QFileInfo(audio).canonicalFilePath() &&
                    found["sizeBytes"] == "7",
            "canonical path and size");
    require(resolver.resolve("../Music/Missing.wav")["status"] == "missing", "missing file");
    require(resolver.resolve("../../usb-other/VIP.wav")["status"] == "outside-media-root", "root prefix/traversal");
    for (const auto& path : {QString("/tmp/other.wav"), QString("C:/Music/a.wav"), QString("C:Music/a.wav"), QString("..\\Music\\a.wav"), QString("\\\\server\\share")})
        require(resolver.resolve(path)["status"] == "unsupported-reference", "foreign path");
    require(resolver.resolve("")["status"] == "invalid-reference", "empty path");
    require(resolver.resolve(QString("a") + QChar::Null + "b")["status"] == "invalid-reference", "NUL path");
    require(QFile::link(other, library + "/outside.wav"), "outside symlink fixture");
    require(QFile::link(audio, library + "/inside.wav"), "inside symlink fixture");
    require(resolver.resolve("outside.wav")["status"] == "outside-media-root", "symlink escape");
    require(resolver.resolve("inside.wav")["path"] == QFileInfo(audio).canonicalFilePath(), "inside symlink");
    require(QFile::remove(library + "/inside.wav") && QFile::link(other, library + "/inside.wav"), "retarget symlink");
    require(resolver.resolve("inside.wav")["status"] == "outside-media-root", "fresh resolution after retarget");
    bool rejected = false;
    try {
        mixxx::EngineMediaResolver invalid(library, temp.path() + "/usb-other");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "unrelated library/root");
    std::cout << "PASS parent media resolution: caller context, missing/foreign paths, traversal, symlinks and retargeting\n";
}
