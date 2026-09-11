#include <djinterop/djinterop.hpp>
#include <djinterop/engine/v3/engine_library.hpp>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    try {
        using namespace djinterop;
        const std::string action = argv[2];
        if (action == "initial") {
            auto db = engine::create_database(argv[1], engine::engine_schema::schema_3_0_2);
            auto list = db.create_root_playlist("Prepared set");
            for (int i = 0; i < 2; ++i) {
                track_snapshot s;
                s.title = i ? "Signal (VIP)" : "Signal (Original)";
                s.artist = "Synthetic fixture";
                s.album = "Import verification";
                s.genre = "Test";
                s.relative_path = i ? "../Music/Signal VIP.wav" : "../Music/Signal Original.wav";
                s.bpm = 120;
                s.duration = std::chrono::milliseconds{3000};
                s.sample_count = 132300;
                s.sample_rate = 44100;
                s.file_bytes = 529244;
                s.bitrate = 1411;
                s.key = musical_key::a_minor;
                s.rating = 60;
                s.comment = "Source comment A";
                s.composer = "Test generator";
                s.publisher = std::nullopt;
                s.year = 2026;
                s.track_number = i + 1;
                s.main_cue = 100.5;
                s.beatgrid = {{0, 0}, {4, 88200}};
                s.hot_cues.resize(8);
                s.loops.resize(8);
                s.hot_cues[0] = hot_cue{"Cue A", 22050.5, {255, 128, 64, 255}};
                s.loops[0] = loop{"Loop A", 44100.25, 88200.75, {12, 34, 56, 255}};
                s.loops[7] = loop{"End loop", 110250, 132300, {80, 90, 100, 255}};
                auto track = db.create_track(s);
                list.add_track_back(track);
            }
            list.create_sub_playlist("Versions").add_track_back(*db.track_by_id(2));
        } else if (action == "maincue-state") {
            auto library = engine::v3::engine_library::load(argv[1]);
            auto data = library.performance_data();
            auto cue = data.get_quick_cues(1);
            cue.default_main_cue = 12345.5;
            cue.adjusted_main_cue = 0;
            cue.is_main_cue_adjusted = false;
            data.set_quick_cues(1, cue);
            cue = data.get_quick_cues(2);
            cue.default_main_cue = 0;
            cue.adjusted_main_cue = 45678.25;
            cue.is_main_cue_adjusted = true;
            data.set_quick_cues(2, cue);
        } else if (action == "update") {
            auto db = engine::load_database(argv[1]);
            auto track = *db.track_by_id(1);
            track.set_comment(std::string("Source comment B"));
            track.set_title(std::string("Signal (Original) — source edit"));
            track.set_rating(100);
            track.set_key(musical_key::c_major);
            track.set_hot_cue_at(0, hot_cue{"Cue B", 33075.5, {20, 40, 60, 255}});
            track.set_loop_at(0, loop{"Loop B", 55125.25, 99225.75, {60, 40, 20, 255}});
        } else
            return 2;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
