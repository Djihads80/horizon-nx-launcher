#include "util.hpp"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <mutex>
#include <thread>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <curl/curl.h>

#ifndef LAUNCHER_HOST_TEST
#include <borealis.hpp>
#include <switch.h>
#endif

namespace launcher
{

// ----- Paths ---------------------------------------------------------------------------------

const std::string& root()
{
    static const std::string value = [] {
        const char* override = ::getenv("HORIZON_LAUNCHER_ROOT");
        return std::string(override && *override ? override : "sdmc:/switch/horizon");
    }();
    return value;
}

std::string path(const std::string& rel) { return root() + "/" + rel; }
std::string configPath() { return path("launcher.json"); }
std::string accountsDir() { return path("accounts"); }
std::string instancesDir() { return path("instances"); }
std::string gamePath() { return path("horizon.nro"); }

std::string gameDirOf(const std::string& instanceId)
{
    // The first instance is the game's own folder: worlds a player already has stay there, and
    // the launcher changes nothing about how an existing install runs.
    if (instanceId == "default" || instanceId.empty())
        return root();

    return instancesDir() + "/" + instanceId + "/home";
}

std::string asset(const std::string& rel) { return "romfs:/" + rel; }

// ----- Files and folders ---------------------------------------------------------------------

bool exists(const std::string& p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

bool isDir(const std::string& p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDirs(const std::string& p)
{
    if (p.empty() || isDir(p))
        return true;

    // Walk the path one part at a time: `sdmc:` is the device, not a folder.
    std::string acc;
    size_t start = 0;
    while (start <= p.size())
    {
        size_t slash = p.find('/', start);
        std::string part =
            p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty())
        {
            acc += part;
            if (acc.back() != ':' && !isDir(acc))
                if (::mkdir(acc.c_str(), 0777) != 0 && errno != EEXIST)
                    return false;
        }
        if (slash == std::string::npos)
            break;
        acc += '/';
        start = slash + 1;
    }

    return isDir(p);
}

bool readFile(const std::string& p, std::string& out)
{
    FILE* f = ::fopen(p.c_str(), "rb");
    if (!f)
        return false;

    out.clear();
    char buf[8192];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    ::fclose(f);
    return true;
}

bool writeFile(const std::string& p, const std::string& data)
{
    FILE* f = ::fopen(p.c_str(), "wb");
    if (!f)
        return false;

    bool ok = data.empty() || ::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (::fclose(f) == 0) && ok;
    return ok;
}

bool writeFileAtomic(const std::string& p, const std::string& data)
{
    std::string tmp = p + ".tmp";
    if (!writeFile(tmp, data))
        return false;
    if (::rename(tmp.c_str(), p.c_str()) != 0)
    {
        ::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool removeTree(const std::string& p)
{
    struct stat st;
    if (::stat(p.c_str(), &st) != 0)
        return true; // already gone

    if (!S_ISDIR(st.st_mode))
        return ::remove(p.c_str()) == 0;

    for (const std::string& name : listDir(p))
        removeTree(p + "/" + name);

    return ::rmdir(p.c_str()) == 0;
}

std::vector<std::string> listDir(const std::string& p)
{
    std::vector<std::string> names;
    DIR* dir = ::opendir(p.c_str());
    if (!dir)
        return names;

    while (struct dirent* entry = ::readdir(dir))
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        names.push_back(name);
    }
    ::closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

bool fileSize(const std::string& p, uint64_t& size)
{
    struct stat st;
    if (::stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return false;
    size = (uint64_t)st.st_size;
    return true;
}

uint64_t freeBytes(const std::string& p)
{
    struct statvfs st;
    if (::statvfs(p.c_str(), &st) != 0)
        return 0;
    return (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
}

bool copyFile(const std::string& from, const std::string& to)
{
    std::string data;
    if (!readFile(from, data))
        return false;
    return writeFileAtomic(to, data);
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string sanitizeFileName(const std::string& s)
{
    std::string out;
    for (char c : trim(s))
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|' || (unsigned char)c < 0x20)
            out.push_back('-');
        else
            out.push_back(c);
    }

    while (!out.empty() && (out.front() == '.' || out.front() == ' '))
        out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();

    if (out.size() > 64)
        out.resize(64);
    return out;
}

std::string humanSize(uint64_t bytes)
{
    char buf[64];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        ::snprintf(buf, sizeof(buf), "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ULL * 1024ULL)
        ::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024ULL)
        ::snprintf(buf, sizeof(buf), "%.0f kB", (double)bytes / 1024.0);
    else
        ::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// ----- HTTP ----------------------------------------------------------------------------------

namespace
{

size_t onWrite(char* data, size_t size, size_t count, void* userdata)
{
    ((std::string*)userdata)->append(data, size * count);
    return size * count;
}

const char* caBundle()
{
    // The console has no certificate store: the bundle travels in the app's romfs.
    static std::string bundle = asset("cacert.pem");
    return bundle.c_str();
}

void setup(CURL* curl, const std::string& url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HorizonLauncher/1.0.0 (Nintendo Switch)");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

struct Progress
{
    size_t received = 0;
    size_t total = 0;
};

size_t onDownload(char* data, size_t size, size_t count, void* userdata)
{
    FILE* f = (FILE*)userdata;
    return ::fwrite(data, size, count, f) * size;
}

} // namespace

HttpResult httpRequest(const std::string& method, const std::string& url, const Fields& form,
    const std::string& json, const std::vector<std::string>& headers)
{
    HttpResult result;

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result.error = "Could not start the network.";
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    setup(curl, url);

    struct curl_slist* list = nullptr;
    for (const std::string& header : headers)
        list = curl_slist_append(list, header.c_str());
    if (list)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    std::string body;
    if (!form.empty())
    {
        for (const auto& [key, value] : form)
        {
            if (!body.empty())
                body += "&";
            body += urlEncode(key) + "=" + urlEncode(value);
        }
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        list = curl_slist_append(list, "Content-Type: application/x-www-form-urlencoded");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    }
    else if (!json.empty())
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
        list = curl_slist_append(list, "Content-Type: application/json");
        list = curl_slist_append(list, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    }

    if (method == "PUT")
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    else if (method == "DELETE")
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    else if (method == "PATCH")
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK)
        result.error = curl_easy_strerror(code);
    else
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);

    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return result;
}

bool httpDownload(const std::string& url, const std::string& dest,
    const std::vector<std::string>& headers, std::string& error)
{
    std::string part = dest + ".part";
    FILE* f = ::fopen(part.c_str(), "wb");
    if (!f)
    {
        error = "Could not write to the SD card.";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        ::fclose(f);
        ::remove(part.c_str());
        error = "Could not start the network.";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onDownload);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    setup(curl, url);

    struct curl_slist* list = nullptr;
    for (const std::string& header : headers)
        list = curl_slist_append(list, header.c_str());
    if (list)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    ::fclose(f);

    if (code != CURLE_OK || status < 200 || status >= 300)
    {
        error = code != CURLE_OK ? curl_easy_strerror(code) : "The file was not there.";
        ::remove(part.c_str());
        return false;
    }

    if (::rename(part.c_str(), dest.c_str()) != 0)
    {
        ::remove(part.c_str());
        error = "Could not save the file.";
        return false;
    }

    return true;
}

std::string urlEncode(const std::string& s)
{
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~')
            out.push_back((char)c);
        else
        {
            out.push_back('%');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 0xF]);
        }
    }
    return out;
}

// ----- Log -----------------------------------------------------------------------------------

std::string base64Decode(const std::string& text)
{
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static int lookup[256];
    static bool ready = false;
    if (!ready)
    {
        for (int i = 0; i < 256; i++)
            lookup[i] = -1;
        for (int i = 0; i < 64; i++)
            lookup[(unsigned char)alphabet[i]] = i;
        ready = true;
    }

    std::string out;
    int buffer = 0;
    int bits   = 0;
    for (unsigned char c : text)
    {
        if (c == '=')
            break;
        const int value = lookup[c];
        if (value < 0)
            continue; // whitespace and newlines in the middle

        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back((char)((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

void log(const char* format, ...)
{
    char line[2048];
    va_list args;
    va_start(args, format);
    ::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    ::printf("%s\n", line);
    ::fflush(stdout);

    FILE* f = ::fopen(path("launcher-log.txt").c_str(), "a");
    if (f)
    {
        ::fprintf(f, "%s\n", line);
        ::fclose(f);
    }
}

// ----- Worker --------------------------------------------------------------------------------

#ifndef LAUNCHER_HOST_TEST

namespace
{

struct Job
{
    std::function<void()> work;
    std::function<void()> done;
};

// libnx's own mutex, not std::mutex: the pump takes it every frame, and newlib's pthread wrappers
// are the same thing this console just showed trouble with.
RMutex queueMutex;
RMutex readyMutex;
bool mutexesReady = false;

/// Locks a libnx mutex for the length of a scope.
struct Lock
{
    explicit Lock(RMutex& mutex)
        : mutex(mutex)
    {
        rmutexLock(&this->mutex);
    }
    ~Lock() { rmutexUnlock(&this->mutex); }
    RMutex& mutex;
};

std::deque<Job> queue;
std::vector<std::function<void()>> readyCallbacks;
Thread workerThread;
bool createFailed = false;

/// Pumps finished callbacks on the UI thread, every frame.
class Pump : public brls::RepeatingTask
{
  public:
    // The constructor registers it with the task manager; the task manager calls run().
    Pump()
        : brls::RepeatingTask(16)
    {
    }

    void run(retro_time_t currentTime) override
    {
        std::vector<std::function<void()>> done;
        {
            Lock lock(readyMutex);
            done.swap(readyCallbacks);
        }
        for (auto& callback : done)
            if (callback)
                callback();

        brls::RepeatingTask::run(currentTime);
    }
};

} // namespace

Worker& Worker::get()
{
    static Worker worker;
    return worker;
}

void Worker::start()
{
    if (started)
        return;
    started = true;

    if (!mutexesReady)
    {
        rmutexInit(&queueMutex);
        rmutexInit(&readyMutex);
        mutexesReady = true;
    }
    log("worker: mutexes ready");

    // A libnx thread rather than std::thread: the stack size is named here (std::thread's default
    // comes from newlib's pthread attributes, which this console didn't like), and a failure comes
    // back as a result code to log instead of an exception to unwind.
    Result rc = threadCreate(&workerThread, &Worker::threadMain, this, nullptr, 512 * 1024, 0x2C, -2);
    log("worker: threadCreate 0x%08X", (unsigned)rc);
    if (R_SUCCEEDED(rc))
    {
        rc = threadStart(&workerThread);
        log("worker: threadStart 0x%08X", (unsigned)rc);
        if (R_FAILED(rc))
            threadClose(&workerThread);
    }

    // Without a thread, jobs run where they are posted (the UI waits for them) rather than not at
    // all: a slow sign-in beats an app that can't sign in.
    createFailed = R_FAILED(rc);

    Pump* pump = new Pump();
    pump->start();
    log("worker: pump started, jobs run %s", createFailed ? "on the UI thread" : "on the worker");
}

void Worker::threadMain(void* arg)
{
    ((Worker*)arg)->loop();
}

void Worker::loop()
{
    for (;;)
    {
        Job job;
        bool have = false;
        {
            Lock lock(queueMutex);
            if (!queue.empty())
            {
                job = std::move(queue.front());
                queue.pop_front();
                have = true;
            }
        }

        if (!have)
        {
            // No condition variable: polling once every 20 ms costs nothing and keeps this thread
            // free of newlib's pthread once mechanisms.
            svcSleepThread(20'000'000);
            continue;
        }

        if (job.work)
        {
            try
            {
                job.work();
            }
            catch (...)
            {
                log("worker: a task threw");
            }
        }

        if (job.done)
        {
            Lock lock(readyMutex);
            readyCallbacks.push_back(std::move(job.done));
        }
    }
}

void Worker::post(std::function<void()> work, std::function<void()> done)
{
    if (createFailed)
    {
        if (work)
            work();
        if (done)
            done();
        return;
    }
    {
        Lock lock(queueMutex);
        queue.push_back(Job { std::move(work), std::move(done) });
    }
}

bool Worker::hasThread() const
{
    return !createFailed;
}

#else // LAUNCHER_HOST_TEST: no threads and no Borealis, so the logic can be tested on a computer

Worker& Worker::get()
{
    static Worker worker;
    return worker;
}

void Worker::start() {}

void Worker::post(std::function<void()> work, std::function<void()> done)
{
    if (work)
        work();
    if (done)
        done();
}

void Worker::threadMain(void*) {}
void Worker::loop() {}

bool Worker::hasThread() const { return true; }

#endif

} // namespace launcher
