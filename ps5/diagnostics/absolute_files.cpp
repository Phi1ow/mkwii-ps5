// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <dirent.h>
#include "../tests/nand_settings_contract.h"
extern "C" int stat(const char*, void*);
extern "C" int mkdir(const char*, unsigned short);
extern "C" int rmdir(const char*);
extern "C" void mkw_diagnostic_log(const char*);
namespace {
unsigned checks;
void check(bool value, const char* operation) {
    ++checks;
    if (value) return;
    const int error = errno;
    char text[256];
    std::snprintf(text, sizeof(text), "[mkw-files] FAIL check=%u operation=%s errno=%d\n", checks, operation, error);
    mkw_diagnostic_log(text);
    throw std::runtime_error(operation);
}
void run() {
    // All mutations are confined to a newly-created directory in this test's
    // own package. Refuse an existing directory; never overwrite prior data.
    constexpr const char* root = "/app0/mkw-file-probe";
    constexpr const char* nested = "/app0/mkw-file-probe/nested";
    constexpr const char* first = "/app0/mkw-file-probe/first.bin";
    constexpr const char* second = "/app0/mkw-file-probe/second.bin";
    check(mkdir(root, 0755) == 0, "create fresh test directory");
    check(mkdir(nested, 0755) == 0, "create nested directory");
    std::array<char, 4099> original{}, read{};
    for (size_t i = 0; i < original.size(); ++i) original[i] = char((i * 37 + 11) & 255);
    {
        std::ofstream output(first, std::ios::binary | std::ios::out);
        check(output.is_open(), "ofstream open absolute path");
        output.write(original.data(), original.size()); output.flush();
        check(bool(output), "ofstream write and flush 4099 bytes");
        output.close(); check(!output.fail(), "ofstream close");
    }
    alignas(16) unsigned char status[4096]{};
    check(stat(first, status) == 0, "stat created file");
    unsigned long size = 0; std::memcpy(&size, status + 72, sizeof(size));
    check(size == original.size(), "native stat file size");
    {
        std::ifstream input(first, std::ios::binary);
        check(input.is_open(), "ifstream open absolute path");
        input.read(read.data(), read.size());
        check(input.gcount() == long(read.size()) && read == original, "ifstream exact binary roundtrip");
        char extra = 0;
        input.read(&extra, 1); check(input.gcount() == 0 && input.eof(), "ifstream eof");
    }
    {
        struct File { std::FILE* value; ~File() { if(value) std::fclose(value); } } file{std::fopen(first, "r+b")};
        check(file.value != nullptr, "fopen update");
        check(std::fseek(file.value, 123, SEEK_SET) == 0, "fseek update offset");
        check(std::fputc(0x5a, file.value) == 0x5a && std::fflush(file.value) == 0, "stdio update and flush");
        check(std::fseek(file.value, 0, SEEK_SET) == 0, "fseek rewind");
        original[123] = 0x5a;
        check(std::fread(read.data(), 1, read.size(), file.value) == read.size() && read == original, "stdio exact updated data");
    }
    mkw_diagnostic_log("[mkw-files] streams and native stat passed\n");
    {
        struct Directory { DIR* value; ~Directory() { if(value) closedir(value); } } directory{opendir(root)};
        check(directory.value != nullptr, "opendir absolute directory");
        unsigned files = 0, directories = 0, unexpected = 0;
        errno = 0;
        while (const dirent* entry = readdir(directory.value)) {
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            if (!std::strcmp(entry->d_name, "first.bin")) ++files;
            else if (!std::strcmp(entry->d_name, "nested")) ++directories;
            else ++unexpected;
        }
        check(errno == 0 && files == 1 && directories == 1 && unexpected == 0, "readdir complete expected entries");
    }
    check(std::rename(first, second) == 0, "rename file");
    errno = 0;
    check(stat(first, status) == -1 && errno == ENOENT, "old path absent after rename");
    check(stat(second, status) == 0, "new path present after rename");
    {
        std::ifstream input(second, std::ios::binary);
        input.read(read.data(), read.size());
        check(input.gcount() == long(read.size()) && read == original, "renamed contents preserved");
    }
    check(std::remove(second) == 0, "remove own test file");
    check(rmdir(nested) == 0, "remove own empty child directory");
    check(rmdir(root) == 0, "remove own empty test directory");
    errno = 0;
    check(stat(root, status) == -1 && errno == ENOENT, "test tree removed");
}
void run_filesystem() {
    namespace fs = std::filesystem;
    const fs::path root("/app0/mkw-std-files-probe");
    std::error_code ec;
    check(!fs::exists(root, ec) && !ec, "filesystem fresh root absent");
    check(fs::create_directory(root, ec) && !ec, "filesystem create root exclusively");
    // Only this newly-created tree may be modified or removed below.
    const auto child = root / "nested" / "leaf";
    check(fs::create_directories(child, ec) && !ec, "filesystem recursive mkdir");
    check(fs::is_directory(child, ec) && !ec, "filesystem directory status");
    const auto first = child / "first.bin";
    const auto copy = root / "copy.bin";
    const auto renamed = root / "renamed.bin";
    const std::string bytes("Mario\0Kart\xffWii", 14);
    { std::ofstream out(first, std::ios::binary); out.write(bytes.data(), bytes.size());
      out.close(); check(!out.fail(), "filesystem path stream write"); }
    check(fs::is_regular_file(first, ec) && !ec, "filesystem regular file status");
    check(fs::file_size(first, ec) == bytes.size() && !ec, "filesystem exact file size");
    check(fs::symlink_status(first, ec).type() == fs::file_type::regular && !ec,
          "filesystem nofollow regular file status");
    check(fs::canonical(child / ".." / "leaf" / "first.bin", ec) == first && !ec,
          "filesystem canonical absolute path");
    check(fs::weakly_canonical(child / "missing" / ".." / "future", ec) == child / "future" && !ec,
          "filesystem weak canonical missing suffix");
    check(fs::copy_file(first, copy, ec) && !ec, "filesystem copy file");
    fs::rename(copy, renamed, ec); check(!ec, "filesystem rename");
    { std::ifstream in(renamed, std::ios::binary); std::string actual(bytes.size(), '\0');
      in.read(actual.data(), actual.size());
      check(in.gcount() == long(bytes.size()) && actual == bytes, "filesystem copy and rename preserve bytes"); }
    unsigned entries = 0;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) ++entries;
    check(!ec && entries == 4, "filesystem recursive enumeration");
    fs::resize_file(renamed, 5, ec); check(!ec && fs::file_size(renamed, ec) == 5 && !ec,
                                        "filesystem truncate file");
    check(fs::current_path(ec).empty() && ec == std::errc::operation_not_supported,
          "filesystem cwd explicitly unsupported");
    fs::space(root, ec); check(ec == std::errc::operation_not_supported,
                             "filesystem capacity explicitly unsupported");
    // LLVM 18 canonical calls realpath("") after absolute() fails to get cwd;
    // its final error is ENOENT. Require rejection, without promising an errno
    // that this unmodified upstream implementation does not preserve.
    check(fs::canonical("relative", ec).empty() && bool(ec),
          "filesystem relative canonical rejected");
    check(fs::remove_all(root, ec) == 5 && !ec, "filesystem remove own complete tree");
    check(!fs::exists(root, ec) && !ec, "filesystem own tree absent after cleanup");
}
}
extern "C" int mkw_test_filesystem() {
    checks = 0;
    mkw_diagnostic_log("[mkw-files] begin absolute paths: C++ streams, stdio, directories, rename and removal\n");
    try {
        run(); run_filesystem();
        const auto nandChecks=test_nand_settings_contract("/app0/mkw-nand-probe");
        char result[160];
        std::snprintf(result,sizeof(result),"[mkw-nand] PASS %u first-run, concurrent publication, existing/invalid files and stale-lock checks\n",nandChecks);
        mkw_diagnostic_log(result);
    }
    catch (const std::exception& error) { mkw_diagnostic_log(error.what()); mkw_diagnostic_log("\n"); return 1; }
    char text[160];
    std::snprintf(text, sizeof(text), "[mkw-files] PASS %u absolute and std::filesystem checks; save persistence still needs validation\n", checks);
    mkw_diagnostic_log(text);
    return 0;
}
