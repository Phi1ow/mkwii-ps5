#pragma once
#include "nand_settings.h"
#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>
inline unsigned test_nand_settings_contract(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    namespace settings = RuntimeNandSettings;
    unsigned checks=0;
    auto check=[&](bool pass){++checks;if(!pass)throw std::runtime_error("NAND contract check "+std::to_string(checks));};
    // Claim a new directory; never reuse or remove a caller's existing data.
    check(fs::create_directory(root));
    const auto fresh=root/"fresh", concurrent=root/"concurrent", invalid=root/"invalid", locked=root/"locked";
    std::string error;
    check(settings::Ensure(fresh,error,1700000000));
    auto identity=settings::Read(fresh);
    check(identity&&settings::HasIdentity(*identity));
    check(identity->at("SERNO")=="700000000");
    check(fs::file_size(settings::FilePath(fresh))==256);
    check(settings::Ensure(fresh,error,1800000000));
    check(settings::Read(fresh)==identity);
    std::atomic<unsigned> passed{0}, failures{0};
    std::vector<std::thread> workers;workers.reserve(4);
    try {
        for(unsigned i=0;i<4;++i)workers.emplace_back([&,i]{
            try {std::string message;if(settings::Ensure(concurrent,message,1700000010+i))++passed;else ++failures;}
            catch(...){++failures;}
        });
    }catch(...){for(auto& worker:workers)worker.join();throw;}
    for(auto& worker:workers)worker.join();
    check(passed==4&&failures==0);
    auto winner=settings::Read(concurrent);
    check(winner&&settings::HasIdentity(*winner));
    check(winner->at("SERNO")>="700000010"&&winner->at("SERNO")<="700000013");
    check(settings::Ensure(concurrent,error,1800000000));
    check(settings::Read(concurrent)==winner);
    fs::create_directories(settings::FilePath(invalid).parent_path());
    {std::ofstream f(settings::FilePath(invalid),std::ios::binary);f<<"keep";check(bool(f));}
    check(!settings::Ensure(invalid,error,1700000000));
    std::array<char,4> preserved{};
    {std::ifstream f(settings::FilePath(invalid),std::ios::binary);f.read(preserved.data(),4);check(bool(f));}
    check((preserved==std::array<char,4>{'k','e','e','p'}));
    const auto lock=settings::FilePath(locked).parent_path()/".setting-publish-lock";
    fs::create_directories(lock);
    check(!settings::Ensure(locked,error,1700000000));
    check(fs::is_directory(lock)&&!fs::exists(settings::FilePath(locked)));
    check(fs::remove(lock));
    check(settings::Ensure(locked,error,1700000000));
    for(const auto& profile:{fresh,concurrent,invalid,locked}) {
        check(!fs::exists(settings::FilePath(profile).parent_path()/".setting-publish-lock"));
        for(const auto& entry:fs::directory_iterator(settings::FilePath(profile).parent_path()))
            check(entry.path().filename().string().find(".setting-init-")!=0);
    }
    // Leave the owned fixtures as inspectable evidence (including the invalid file).
    return checks;
}
