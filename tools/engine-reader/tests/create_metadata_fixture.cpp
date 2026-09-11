#include <djinterop/djinterop.hpp>
#include <iostream>

// Synthetic schema 3.0.2 fixture: no firmware, music or user library required.
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    try {
        auto schema = djinterop::engine::engine_schema::schema_3_0_2;
        if (argc == 3) {
            if (std::string(argv[2]) != "3.0.0") return 2;
            schema = djinterop::engine::engine_schema::schema_3_0_0;
        }
        auto db = djinterop::engine::create_database(argv[1], schema);
        for (int i = 0; i < 26; ++i) {
            djinterop::track_snapshot s;
            s.title = "metadata-" + std::to_string(i);
            s.relative_path = "../missing-" + std::to_string(i) + ".mp3";
            s.sample_count = 44100;
            s.sample_rate = 44100;
            s.hot_cues.resize(8);
            s.loops.resize(8);
            if (i < 24) {
                s.key = static_cast<djinterop::musical_key>(i);
                s.bitrate = 320;
                s.rating = i == 0 ? 0 : 100;
                s.year = 2026;
                s.track_number = i + 1;
                s.file_bytes = 9007199254740993ULL;
                s.comment = "VIP — keep this version";
                s.composer = "Composer";
                s.publisher = "Publisher";
            } else if (i == 25) {
                s.comment = "";
                s.composer = "";
                s.publisher = "";
                s.bitrate = 0;
                s.rating = 0;
                s.year = 0;
                s.track_number = 0;
                s.file_bytes = 0;
            }
            db.create_track(s);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
