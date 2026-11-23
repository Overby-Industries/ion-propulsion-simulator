// A stand-in for the orbit view in the tests of abep_run (tests/abep_run_ask.cmake). The real one is a Godot program, which the tests
// must not need. Started as `fake_viewer -- FOLDER`, the way abep_run starts the orbit view, it writes viewer_started.txt into FOLDER,
// with the arguments it was given and whether FOLDER holds samples.csv, and exits. The file appears whole or not at all (it is
// written under another name and renamed), so that a test that waits for it never reads half of it.

#include <filesystem>
#include <fstream>
#include <system_error>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::filesystem::path folder = argv[argc - 1];
    const std::filesystem::path temporary = folder / "viewer_started.tmp";
    {
        std::ofstream out(temporary);
        if (!out) return 3;
        for (int i = 1; i < argc; ++i) out << "argument " << i << ": " << argv[i] << '\n';
        std::error_code ignored;
        out << "samples.csv found: " << (std::filesystem::exists(folder / "samples.csv", ignored) ? "yes" : "no") << '\n';
    }
    std::error_code error;
    std::filesystem::rename(temporary, folder / "viewer_started.txt", error);
    return error ? 4 : 0;
}
