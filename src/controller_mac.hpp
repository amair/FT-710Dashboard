#pragma once

#include "radio.hpp"
#include "cat_server.hpp"
#include "audio.hpp"
#include "rf_scope.hpp"
#include "web_server.hpp"
#include "station_settings.hpp"
#include "task_queue.hpp"
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <array>

namespace app {

class ControllerMac {
public:
    ControllerMac();
    ~ControllerMac();

    void initialize(bool demo = false, const std::string& serialPort = "", int baud = 38400, const std::string& passcode = "");
    void run();
    void shutdown();

private:
    std::unique_ptr<radio::Worker> worker_;
    std::unique_ptr<CatServer> catServer_;
    std::unique_ptr<web::Server> webServer_;
    audio::Route receiveAudio_, microphoneAudio_;
    rf_scope::Capture rfCapture_;

    std::atomic<bool> running_{false};
    std::string webAccessCode_;
    radio::Snapshot state_;
    std::mutex stateMutex_;
    // Web commands arrive on per-client WebSocket threads; serialize them so
    // audio maps/routes, PTT ownership, sharing and connection settings are
    // never read or written concurrently.
    std::mutex commandMutex_;

    // Station connection settings
    int connectionKind_ = 0; // 0: USB CAT, 1: rigctld, 2: Dummy
    std::string endpoint_;
    int baud_ = 38400;
    int stopBits_ = 1;
    std::string flow_ = "None";
    int pollMs_ = 750;

    // CAT sharing
    bool sharingWanted_ = true;
    unsigned sharePort_ = 4532;
    std::string sharingError_;

    // PTT management
    std::uint64_t browserPttOwner_ = 0;
    std::set<std::uint64_t> browserMicClients_;
    bool pttPending_ = false;
    std::chrono::steady_clock::time_point pttRequested_;
    int pttLimitSeconds_ = 180;

    // Audio & RF state
    bool receiveWanted_ = false;
    bool microphoneWanted_ = false;
    float receiveGain_ = 1.0f;
    float micGain_ = 1.0f;
    std::array<std::wstring, 4> audioSelectedIds_{};  // per-slot device id ("", "pa:N", "browser:playback", "browser:capture")
    bool unattended_ = false;
    std::string rfCat2Port_ = "";
    unsigned rfSpan_ = 7;
    unsigned rfCat2Baud_ = 4800;
    double rfWaterfallLevelDb_ = -5.0;

    // Activity log
    std::vector<std::string> activity_;
    std::mutex activityMutex_;

    void log(const std::string& message);
    void connectRadio();
    void disconnectRadio();
    void startSharing();
    void stopSharing();
    void releasePtt();
    void onRadioUpdate(const radio::Snapshot& newState);
    void handleWebRequest(std::shared_ptr<web::Request> request);
    void publishWeb(bool urgent = false);
    std::string buildStateJson();

    // Resolve the PortAudio endpoint for each route slot. Empty string means
    // "system default device"; L"null" means "endpoint owned elsewhere
    // (browser), open no host device".
    std::wstring receiveSource() const;
    std::wstring receiveDest() const;
    std::wstring micSource() const;
    std::wstring micDest() const;

    // Memory instrumentation helpers (for debugging std::bad_alloc)
    /// Returns RSS memory usage in KB using getrusage() - more reliable than ps command
    size_t get_memory_usage_kb();

    /// Logs current RSS memory usage with [FT-710] prefix
    void log_memory_usage();
};

}
