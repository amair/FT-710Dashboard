#pragma once
#include "net_compat.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <condition_variable>
#endif
#include "audio_scope.hpp"
#include "audio_tap.hpp"
#include "browser_audio.hpp"

namespace audio {
struct Device { std::wstring id, name; bool input = false, direct = false, remote = false; };
std::vector<Device> devices();
std::vector<Device> directDevices();
std::wstring directDeviceIssue();
bool remoteSession();
std::wstring savedDeviceName(const std::wstring& identity, bool input);
// Search for FT-710 USB Audio CODEC device (macOS only) - implementation detail
#if !defined(_WIN32)
std::wstring findFT710AudioDevice(const std::vector<Device>& devices, bool isInput);
#endif
struct Status {
    bool active = false, starting = false;
    float inputPeak = 0, outputPeak = 0;
    std::wstring text = L"Off";
};
// One explicit capture -> render route. No implicit default endpoint changes.
class Route {
public:
    Route();
    ~Route();
    void start(const std::wstring& source, const std::wstring& destination);
    void stop();
    void requestStop();
    void gate(bool enabled);
    void gain(float value);
    Status status() const;
    Spectrum& spectrum() { return spectrum_; }
    void receivedAudio(bool enabled) { receiveTap_ = enabled; spectrum_.enable(enabled); }
private:
#if defined(_WIN32)
    HANDLE stopEvent_ = nullptr;
#else
    std::atomic<bool> stopWanted_{false};
    std::condition_variable stopCv_;
    std::mutex stopMutex_;
#endif
    std::thread thread_;
    // Serializes start()/stop() across threads (web command threads,
    // shutdown) so the route thread is never double-joined or reassigned
    // while another caller is joining it.
    std::mutex startStopMutex_;
    void stopLocked();
    std::atomic<bool> gate_{false};
    std::atomic<unsigned> gateEpoch_{0};
    std::atomic<float> gain_{1};
    mutable std::mutex mutex_;
    Status status_;
    Spectrum spectrum_;
    bool receiveTap_ = false;
    std::vector<std::unique_ptr<AudioTap>> taps_;
    std::mutex taps_mutex_;
    void run(std::wstring source, std::wstring destination);
    void runDirect(std::wstring source, std::wstring destination);
public:
    AudioTap* add_tap() {
        std::lock_guard lock(taps_mutex_);
        taps_.push_back(std::make_unique<AudioTap>());
        return taps_.back().get();
    }
    void remove_tap(AudioTap* tap) {
        std::lock_guard lock(taps_mutex_);
        for (auto it = taps_.begin(); it != taps_.end(); ++it) {
            if (it->get() == tap) { taps_.erase(it); return; }
        }
    }
};
}
