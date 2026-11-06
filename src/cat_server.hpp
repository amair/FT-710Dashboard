#pragma once
#include "net_compat.hpp"
#include "cat.hpp"
#include "audio_tap.hpp"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// A loopback rigctld protocol server. It never opens a radio or calls Hamlib.
class CatServer {
public:
    using Handler = std::function<std::future<radio::CatReply>(std::shared_ptr<radio::CatJob>)>;
    using Log = std::function<void(const std::string&)>;
    using TapFactory = std::function<audio::AudioTap*()>;
    using TapRemover = std::function<void(audio::AudioTap*)>;
    CatServer(Handler handler, Log log, TapFactory tapFactory, TapRemover tapRemover);
    ~CatServer();
    void start(unsigned port, std::uint64_t generation);
    void stop();
    bool running() const { return running_.load(); }
    unsigned clients() const { return clients_.load(); }
    unsigned port() const { return port_; }
private:
    struct Client {
        socket_t socket = INVALID_SOCKET;
        std::uint64_t id = 0;
        std::thread thread;
        std::atomic<bool> done{false};
        bool is_streaming = false;
    };
    Handler handler_;
    Log log_;
    TapFactory tapFactory_;
    TapRemover tapRemover_;
    socket_t listener_ = INVALID_SOCKET;
    [[maybe_unused]] bool winsock_ = false;  // Used for Windows-specific cleanup in stop()
    std::atomic<bool> running_{false};
    std::atomic<unsigned> clients_{0};
    unsigned port_ = 0;
    std::uint64_t generation_ = 0, nextClient_ = 1;
    std::thread acceptThread_;
    std::mutex mutex_;
    std::vector<std::unique_ptr<Client>> sessions_;
    std::map<std::uint64_t, audio::AudioTap*> streaming_clients_;
    void acceptLoop();
    void serve(Client& client);
};
