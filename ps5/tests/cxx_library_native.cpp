#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <locale>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <wchar.h>
#include <xlocale.h>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int sceKernelUsleep(unsigned);
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_once_t retry = PTHREAD_ONCE_INIT;
static int published, calls, attempts;
static std::atomic<unsigned> tlsDestroyed{0};
struct ThreadLifetime { int value = 0; ~ThreadLifetime() { if (value == 42) ++tlsDestroyed; } };
static void Initialize() { ++calls; sceKernelUsleep(20000); published = 42; }
static void Retry() { if (++attempts == 1) throw std::runtime_error("retry"); }
extern "C" int mkw_cxx_library_test() {
    mkw_diagnostic_log("[mkw-libcxx] begin locale, standard exceptions and concurrent once\n");
    try {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << 42 << ' ' << 1.25;
        if (out.str() != "42 1.25" || std::toupper('q', std::locale::classic()) != 'Q') return 1;
        std::istringstream in(out.str());
        int integer = 0; double fraction = 0;
        in >> integer >> fraction;
        if (!in || integer != 42 || fraction != 1.25) return 2;
        bool rejected = false, bounds = false;
        try { std::locale unsupported("fr_FR.UTF-8"); }
        catch (const std::runtime_error&) { rejected = true; }
        try { std::vector<int>{1, 2}.at(2); }
        catch (const std::out_of_range&) { bounds = true; }
        if (!rejected || !bounds) return 3;
        locale_t loc = newlocale(LC_ALL_MASK, "C", nullptr);
        if (!loc) return 4;
        mbstate_t state{}; wchar_t buffer[4]{};
        const char* original = "abc", *source = original;
        if (mbsnrtowcs_l(nullptr, &source, 4, 0, &state, loc) != 3 || source != original) return 5;
        if (mbsnrtowcs_l(buffer, &source, 4, 2, &state, loc) != 2 || source != original + 2 || buffer[1] != L'b') return 6;
        if (mbsrtowcs_l(buffer, &source, 4, &state, loc) != 1 || source || buffer[0] != L'c' || buffer[1]) return 7;
        errno = 0;
        if (mbrtowc_l(buffer, "\xff", 1, &state, loc) != static_cast<size_t>(-1) || errno != EILSEQ) return 8;
        freelocale(loc);
        mkw_diagnostic_log("[mkw-libcxx] PASS streams, classic locale, rejected named locale, bounds exception and conversions\n");
        char errorText[256]{};
        if (strerror_r(EACCES,errorText,sizeof(errorText)) != 0 || !*errorText) return 22;
        if (strerror_r(EACCES,errorText,0) != ERANGE) return 23;
        if (strerror_r(EACCES,errorText,1) != ERANGE) return 24;
        const std::system_error systemError(EOPNOTSUPP,std::generic_category(),"PS5 error contract");
        if (systemError.code().value()!=EOPNOTSUPP || !std::strstr(systemError.what(),"PS5 error contract")) return 25;
        bool caughtPath=false;
        try { (void)std::filesystem::current_path(); }
        catch (const std::filesystem::filesystem_error& error) {
            caughtPath=error.code().value()==EOPNOTSUPP && *error.what();
        }
        if (!caughtPath) return 26;
        mkw_diagnostic_log("[mkw-libcxx] PASS POSIX strerror_r, small buffers, system_error and filesystem_error diagnostics\n");
        bool caught = false;
        try { pthread_once(&retry, Retry); }
        catch (const std::runtime_error&) { caught = true; }
        pthread_once(&retry, Retry); pthread_once(&retry, Retry);
        if (!caught || attempts != 2) return 9;
        std::atomic<unsigned> failures{0};
        auto worker = [&] {
            thread_local ThreadLifetime lifetime;
            lifetime.value = 42;
            pthread_once(&once, Initialize);
            if (published != 42) ++failures;
            try { throw std::runtime_error("worker"); }
            catch (const std::exception& error) { if (std::string(error.what()) != "worker") ++failures; }
        };
        // Reserve before starting threads; no vector allocation may unwind
        // across an already joinable std::thread on the error path.
        std::vector<std::thread> threads; threads.reserve(3);
        try { for (int i = 0; i < 3; ++i) threads.emplace_back(worker); }
        catch (...) { for (auto& thread : threads) thread.join(); throw; }
        for (auto& thread : threads) thread.join();
        if (calls != 1 || failures || tlsDestroyed != 3) return 10;
        mkw_diagnostic_log("[mkw-libcxx] PASS three std::threads, exceptions, once publication and retry after throw\n");
        mkw_diagnostic_log("[mkw-libcxx] PASS thread_local destructors completed before joins\n");
        // Reentrant graphics callbacks require a genuinely recursive lock.
        // Verify another thread cannot acquire it after only one unlock.
        std::recursive_mutex recursive;
        recursive.lock();recursive.lock();
        if (!recursive.try_lock()) return 12;
        recursive.unlock();
        auto other_can_lock=[&] {
            bool acquired=false;
            std::thread other([&]{acquired=recursive.try_lock();if(acquired)recursive.unlock();});
            other.join();return acquired;
        };
        if(other_can_lock())return 13;
        recursive.unlock();
        if(other_can_lock())return 14;
        recursive.unlock();
        if(!other_can_lock())return 15;
        pthread_mutexattr_t attr{};pthread_mutex_t checked{};
        if(pthread_mutexattr_init(&attr))return 16;
        if(pthread_mutexattr_settype(&attr,99)!=EINVAL)return 17;
        if(pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK)||pthread_mutex_init(&checked,&attr))return 18;
        if(pthread_mutexattr_destroy(&attr))return 19;
        if(pthread_mutex_lock(&checked)||pthread_mutex_lock(&checked)!=EDEADLK)return 20;
        if(pthread_mutex_unlock(&checked)||pthread_mutex_destroy(&checked))return 21;
        mkw_diagnostic_log("[mkw-libcxx] PASS recursive mutex nesting, cross-thread exclusion through partial unlock, final release and error-check mutex ABI\n");
        return 0;
    } catch (const std::exception& error) {
        mkw_diagnostic_log("[mkw-libcxx] unexpected exception: "); mkw_diagnostic_log(error.what());
        mkw_diagnostic_log("\n"); return 11;
    }
}
