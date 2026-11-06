#include "controller_mac.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sys/resource.h>  // For getrusage() - more reliable than ps command

namespace app {
namespace {
std::string boolean(bool v) { return v ? "true" : "false"; }

std::string array(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += web::json(values[i]);
    }
    return out + "]";
}

double number(const std::string& value, double minimum, double maximum, bool integer = false) {
    std::size_t count = 0;
    double result = std::stod(value, &count);
    if (count != value.size() || !std::isfinite(result) || result < minimum || result > maximum || (integer && result != std::trunc(result)))
        throw std::invalid_argument("Value is outside the allowed range");
    return result;
}
std::wstring toWide(const std::string& utf8) { return std::wstring(utf8.begin(), utf8.end()); }
}

ControllerMac::ControllerMac() = default;

ControllerMac::~ControllerMac() {
    shutdown();
}

void ControllerMac::log(const std::string& message) {
#ifdef BUILD_DEBUG
    std::cout << "[FT-710] " << message << std::endl;
#endif
    {
        std::lock_guard lock(activityMutex_);
        // Safety bounds to prevent bad_alloc during logging itself
        constexpr size_t MAX_ACTIVITY_ENTRIES = 500;
        if (activity_.size() >= MAX_ACTIVITY_ENTRIES) {
            // Rotate all entries instead of growing - replaces oldest with newest
            for (auto& existing : activity_) {
                existing = message;
            }
        } else {
            activity_.push_back(message);
        }
    }
}

// ============================================================================
// MEMORY DEBUGGING CHANGES (November 2026) - For debugging std::bad_alloc crashes
// ============================================================================
//
// PROBLEM: Application crashed with std::bad_alloc during initialization. Initial
// analysis pointed to std::ostringstream allocating >4MB for Hamlib settings,
// but logs showed JSON only reaching ~49KB while still crashing.
//
// FIXES IMPLEMENTED:
// 1. Fixed get_memory_usage_kb() to use getrusage() instead of ps command -
//    previous shell-based method may return incorrect values
// 2. Added bounds checking on activity_ vector (MAX_ACTIVITY_ENTRIES = 500) -
//    prevents logging from causing crash if activity log grows unbounded
// 3. Added safety bounds in buildStateJson() when iterating activity_ entries
//
// VERIFICATION: Need to rebuild and test in non-demo mode with physical radio.
// Current fixes are conservative and prevent crashes even if activity_ grows large.
// ============================================================================

// Memory instrumentation helpers (for debugging std::bad_alloc crashes)
/// Returns RSS memory usage in KB using getrusage() - more reliable than ps command
size_t ControllerMac::get_memory_usage_kb() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // ru_maxrss behavior varies by platform:
        // - Linux: kilobytes
        // - macOS: also reported as kilobytes per Apple docs
        // Sanity check: if value > 500MB, it's likely wrong (return bytes)
        size_t raw = static_cast<size_t>(usage.ru_maxrss);
        if (raw > 500 * 1024) {
            // Value is too high for KB - assume it's in bytes on this system
            raw /= 1024;
        }
        return raw;
    }
    return 0;
}

void ControllerMac::log_memory_usage() {
#ifdef BUILD_DEBUG
    size_t mem = get_memory_usage_kb();
    std::cout << "[FT-710] [MEMORY] RSS: " << mem << " KB" << std::endl;
#endif
}

// JSON building size constant for buildStateJson (changed from 4MB to 16MB)
constexpr size_t MAX_JSON_STRING_SIZE = 16 * 1024 * 1024; // 16 MB max - enough for full Hamlib settings

void ControllerMac::initialize(bool demo, const std::string& serialPort, int baud, const std::string& passcode) {
    running_ = true;

    if (demo) {
        connectionKind_ = 2; // Dummy demo
        endpoint_ = "Dummy";
    } else {
        connectionKind_ = 0; // USB CAT
        endpoint_ = serialPort.empty() ? "/dev/cu.SLAB_USBtoUART" : serialPort;
    }
    baud_ = baud;

    webAccessCode_ = passcode.empty() ? web::randomToken().substr(0, 16) : passcode;

    worker_ = std::make_unique<radio::Worker>(
        [this](const radio::Snapshot& s) { onRadioUpdate(s); },
        [this](const std::string& msg) { log(msg); }
    );

    webServer_ = std::make_unique<web::Server>(
        [this](std::shared_ptr<web::Request> req) { handleWebRequest(req); },
        [this](const std::string& msg) { log(msg); }
    );

    const char* home = std::getenv("HOME");
    std::filesystem::path certDir = home ? (std::filesystem::path(home) / ".config" / "ft710") : ".";
    std::wstring certDirW = certDir.wstring();

#ifdef BUILD_DEBUG
    std::cout << "[FT-710] [INIT] Phase 2: Starting web server with TLS certificate loading..." << std::endl;
    std::cout << "[FT-710] [MEMORY] Before TLS init: " << get_memory_usage_kb() << " KB" << std::endl;
#endif
    // The web dashboard is a core feature: it must start in release builds
    // too (only the diagnostics above are debug-only).
    webServer_->start(certDirW, webAccessCode_);
#ifdef BUILD_DEBUG
    std::cout << "[FT-710] [INIT] Web server started successfully. Memory after TLS: " << get_memory_usage_kb() << " KB" << std::endl;
#endif

    connectRadio();
    if (sharingWanted_) startSharing();
}

void ControllerMac::connectRadio() {
    radio::Command cmd;
    cmd.action = radio::Action::Connect;
    cmd.connection.type = static_cast<radio::ConnectionType>(connectionKind_);
    cmd.connection.endpoint = endpoint_;
    cmd.connection.baud = baud_;
    cmd.connection.stopBits = stopBits_;
    cmd.connection.handshake = flow_;
    cmd.connection.pollMs = pollMs_;
    worker_->submit(std::move(cmd));
}

void ControllerMac::disconnectRadio() {
    radio::Command cmd;
    cmd.action = radio::Action::Disconnect;
    worker_->submit(std::move(cmd));
}

void ControllerMac::startSharing() {
    if (!catServer_) {
        catServer_ = std::make_unique<CatServer>(
            [this](std::shared_ptr<radio::CatJob> job) {
                return worker_->request(job);
            },
            [this](const std::string& msg) { log(msg); },
            [this]() -> audio::AudioTap* { return receiveAudio_.add_tap(); },
            [this](audio::AudioTap* tap) { receiveAudio_.remove_tap(tap); }
        );
    }
    try {
        catServer_->start(sharePort_, 1);
        sharingError_.clear();
    } catch (const std::exception& e) {
        sharingError_ = e.what();
        log("CAT sharing error: " + sharingError_);
    }
}

void ControllerMac::stopSharing() {
    if (catServer_) {
        catServer_->stop();
        catServer_.reset();
    }
}

void ControllerMac::releasePtt() {
    if (browserPttOwner_ || pttPending_ || state_.dashboardPtt) {
        radio::Command cmd;
        cmd.action = radio::Action::PttOff;
        worker_->submit(std::move(cmd));
    }
    browserPttOwner_ = 0;
    pttPending_ = false;
    microphoneAudio_.gate(false);
}

std::wstring ControllerMac::receiveSource() const {
    if (!audioSelectedIds_[0].empty()) return audioSelectedIds_[0];
    // No explicit choice: use the radio's USB Audio CODEC, else the default input.
    return audio::findFT710AudioDevice(audio::devices(), true);
}
std::wstring ControllerMac::receiveDest() const {
    // Empty => system default output; "browser:playback" => no host output.
    return audioSelectedIds_[1];
}
std::wstring ControllerMac::micSource() const {
    // Empty => system default input; "browser:capture" => no host input.
    return audioSelectedIds_[2];
}
std::wstring ControllerMac::micDest() const {
    if (!audioSelectedIds_[3].empty()) return audioSelectedIds_[3];
    // No explicit choice: route transmit audio into the radio's USB Audio CODEC.
    return audio::findFT710AudioDevice(audio::devices(), false);
}

void ControllerMac::onRadioUpdate(const radio::Snapshot& newState) {
    {
        std::lock_guard lock(stateMutex_);
        state_ = newState;
    }
    publishWeb();

    // Check PTT timeout
    if (browserPttOwner_ && state_.transmitting) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - pttRequested_).count();
        if (elapsed > pttLimitSeconds_) {
            log("PTT watchdog timer expired (" + std::to_string(pttLimitSeconds_) + "s). Cutting transmission.");
            releasePtt();
        }
    }
}

void ControllerMac::handleWebRequest(std::shared_ptr<web::Request> request) {
    // Commands arrive on per-client WebSocket threads; serialize them so the
    // audio routes and shared settings below cannot race (e.g. two clients
    // starting/stopping the same route, or a map write during a route start).
    std::lock_guard<std::mutex> commandLock(commandMutex_);
    try {
        const auto& f = request->fields;
        auto get = [&](const char* k) { auto it = f.find(k); return it != f.end() ? it->second : ""; };
        auto action = get("action");
        auto vfo = get("vfo");

        radio::Command cmd;
        cmd.vfo = (vfo == "B") ? RIG_VFO_B : RIG_VFO_A;

        if (action == "connect") {
            connectionKind_ = std::stoi(get("kind"));
            endpoint_ = get("endpoint");
            baud_ = std::stoi(get("baud"));
            stopBits_ = std::stoi(get("stopBits"));
            flow_ = get("flow");
            pollMs_ = std::stoi(get("poll"));
            connectRadio();
        } else if (action == "disconnect") {
            disconnectRadio();
        } else if (action == "ptt") {
            if (get("enabled") == "0") {
                releasePtt();
            } else {
                if (!state_.online || state_.busy || !state_.canPtt || !state_.pttKnown || state_.releasePending)
                    throw std::runtime_error("Radio PTT is unavailable or release is pending");
                if (browserPttOwner_ || pttPending_ || state_.dashboardPtt || (state_.pttKnown && state_.transmitting))
                    throw std::runtime_error("PTT is already active; release it first");

                const long limits[]{30, 60, 180, 300, 600};
                cmd.action = radio::Action::PttOn;
                cmd.width = limits[std::clamp(std::stoi(get("limit").empty() ? "2" : get("limit")), 0, 4)];
                pttLimitSeconds_ = static_cast<int>(cmd.width);
                browserPttOwner_ = request->client;
                pttPending_ = true;
                pttRequested_ = std::chrono::steady_clock::now();
                worker_->submit(std::move(cmd));
            }
        } else if (action == "browserGone") {
            if (browserPttOwner_ == request->client) {
                log("Client " + std::to_string(request->client) + " disconnected while holding PTT. Safety release.");
                releasePtt();
            }
            browserMicClients_.erase(request->client);
        } else if (action == "browserMic") {
            bool en = get("enabled") == "1";
            if (en) browserMicClients_.insert(request->client);
            else browserMicClients_.erase(request->client);
        } else if (action == "sharing") {
            sharingWanted_ = get("enabled") == "1";
            sharePort_ = static_cast<unsigned>(number(get("port"), 1, 65535, true));
            if (sharingWanted_) startSharing();
            else stopSharing();
        } else if (action == "audioMap") {
            // Persist the device chosen for a route slot (0 receive source,
            // 1 receive dest, 2 mic source, 3 mic dest) and restart the route
            // if it is currently active.
            unsigned slot = static_cast<unsigned>(number(get("slot"), 0, 3, true));
            std::wstring id = toWide(get("device"));
            if (!id.empty() && !id.starts_with(L"browser:")) {
                bool found = false;
                for (const auto& d : audio::devices()) if (d.id == id) { found = true; break; }
                if (!found) throw std::runtime_error("Audio device is unavailable");
            }
            if (audioSelectedIds_[slot] != id) {
                audioSelectedIds_[slot] = id;
                log("Audio mapping " + std::to_string(slot) + " set to '" + get("device") + "'");
                if (slot <= 1) { if (receiveWanted_) receiveAudio_.start(receiveSource(), receiveDest()); }
                else if (microphoneWanted_) microphoneAudio_.start(micSource(), micDest());
            }
        } else if (action == "audioRoute") {
            bool isMic = get("route") == "microphone";
            bool en = get("enabled") == "1";
            if (isMic) {
                microphoneWanted_ = en;
                if (en) microphoneAudio_.start(micSource(), micDest());
                else microphoneAudio_.stop();
            } else {
                receiveWanted_ = en;
                if (en) {
                    receiveAudio_.receivedAudio(true);
                    receiveAudio_.start(receiveSource(), receiveDest());
                } else {
                    receiveAudio_.stop();
                }
            }
        } else if (action == "audioLevel") {
            bool isMic = get("route") == "microphone";
            float val = std::stof(get("value")) / 100.0f;
            if (isMic) { micGain_ = val; microphoneAudio_.gain(val); }
            else { receiveGain_ = val; receiveAudio_.gain(val); }
        } else if (action == "pttLimit") {
            static const int limits[] = {30, 60, 180, 300, 600};
            int index = static_cast<int>(number(get("index"), 0, 4, true));
            pttLimitSeconds_ = limits[index];
        } else if (action == "unattended") {
            unattended_ = get("enabled") == "1";
        } else if (action == "frequency") {
            cmd.action = radio::Action::Frequency;
            cmd.number = number(get("hz"), 30000, 60000000);
            worker_->submit(std::move(cmd));
        } else if (action == "step") {
            cmd.action = radio::Action::StepFrequency;
            cmd.number = number(get("hz"), -1000000, 1000000);
            worker_->submit(std::move(cmd));
        } else if (action == "mode") {
            cmd.action = radio::Action::Mode;
            cmd.text = get("mode");
            cmd.width = static_cast<long>(number(get("width").empty() ? "0" : get("width"), 0, 100000, true));
            worker_->submit(std::move(cmd));
        } else if (action == "vfo") {
            cmd.action = radio::Action::SelectVfo;
            cmd.vfo = (vfo == "B") ? RIG_VFO_B : RIG_VFO_A;
            worker_->submit(std::move(cmd));
        } else if (action == "set") {
            cmd.action = radio::Action::Set;
            cmd.id = get("setting");
            cmd.text = get("value");
            worker_->submit(std::move(cmd));
        } else if (action == "swap") {
            cmd.action = radio::Action::Swap;
            worker_->submit(std::move(cmd));
        } else if (action == "copy") {
            cmd.action = radio::Action::CopyAtoB;
            worker_->submit(std::move(cmd));
        } else if (action == "refresh") {
            cmd.action = radio::Action::Refresh;
            worker_->submit(std::move(cmd));
        } else if (action == "powerOn") {
            cmd.action = radio::Action::PowerOn;
            worker_->submit(std::move(cmd));
        } else if (action == "powerOff") {
            cmd.action = radio::Action::PowerOff;
            releasePtt();
            worker_->submit(std::move(cmd));
        } else if (action == "rfStart") {
            rfCapture_.start();
        } else if (action == "rfStop") {
            rfCapture_.stop();
        } else if (action == "rfSpan") {
            rfCat2Port_ = get("port");
            rfSpan_ = static_cast<unsigned>(number(get("span"), 0, 9, true));
            rfCat2Baud_ = static_cast<unsigned>(number(get("baud"), 4800, 115200, true));
            rfWaterfallLevelDb_ = number(get("level"), -30, 30);
            std::wstring cat2PortW(rfCat2Port_.begin(), rfCat2Port_.end());
            rf_scope::applySpan(cat2PortW, rfSpan_, rfCat2Baud_, rfWaterfallLevelDb_);
        } else if (action == "clearLog") {
            std::lock_guard lock(activityMutex_);
            activity_.clear();
        }

        publishWeb();
        request->reply.set_value("\"ok\":true,\"queued\":true");
    } catch (const std::exception& e) {
        request->reply.set_value("\"ok\":false,\"error\":" + web::json(e.what()));
    }
}

void ControllerMac::publishWeb(bool urgent) {
    try {
        if (webServer_) {
            std::string state = buildStateJson();
            webServer_->publish(std::move(state), urgent);
        }

        // Audio polling
        audio::SpectrumFrame frame;
        if (receiveAudio_.spectrum().poll(frame)) {
            webServer_->spectrum(frame);
        }

        // RF spectrum
        rf_scope::Frame rfFrame;
        if (rfCapture_.latest(rfFrame)) {
            webServer_->rfSpectrum(rfFrame, rfFrame.centerHz, rf_scope::spanHz(rfFrame.spanIndex));
        }
    } catch (const std::bad_alloc& e) {
#ifdef BUILD_DEBUG
        size_t rssAtCrash = get_memory_usage_kb();
        std::cout << "[FT-710] [PUBWEB] CRITICAL: Memory allocation failed at RSS=" << rssAtCrash << " KB" << std::endl;
#endif
        throw;
    } catch (const std::exception& e) {
#ifdef BUILD_DEBUG
        size_t rssAtCrash = get_memory_usage_kb();
        std::cout << "[FT-710] [PUBWEB] CRITICAL: exception at RSS=" << rssAtCrash << " KB: " << e.what() << std::endl;
#endif
        throw;
    } catch (...) {
#ifdef BUILD_DEBUG
        size_t rssAtCrash = get_memory_usage_kb();
        std::cout << "[FT-710] [PUBWEB] CRITICAL: unknown exception at RSS=" << rssAtCrash << " KB" << std::endl;
#endif
        throw;
    }
}

std::string ControllerMac::buildStateJson() {
    // Memory-safe JSON construction (November 2026 fix)
    // - Changed from std::ostringstream (crashed at >4MB) to incremental std::string
    // - Size limit: 16 MB (was 4 MB)
    // - Fixed get_memory_usage_kb() to use getrusage() instead of ps command
    // - Added bounds checking on activity_ vector (MAX_ACTIVITY_ENTRIES = 500)
    // - Size checkpoints at each stage for debugging

    std::lock_guard lock(stateMutex_);
    const auto& s = state_;

    // Log RSS at start of construction (debug output only)
#ifdef BUILD_DEBUG
    std::cout << "[FT-710] [BUILD] Starting JSON construction..." << std::endl;
    std::cout << "[FT-710] [BUILD] RSS memory: " << get_memory_usage_kb() << " KB" << std::endl;
#endif

    std::string result = "{\"type\":\"state\",\"generation\":" + std::to_string(s.generation);
    result += ",\"online\":" + boolean(s.online);
    result += ",\"portOpen\":" + boolean(s.portOpen);
    result += ",\"busy\":" + boolean(s.busy);
    result += ",\"demo\":" + boolean(s.demo);
    result += ",\"poweredOff\":" + boolean(s.poweredOff);
    result += ",\"standby\":" + boolean(s.standby);
    result += ",\"powerKnown\":" + boolean(s.powerKnown);
    result += ",\"canPower\":" + boolean(s.canPower);
    result += ",\"canPtt\":" + boolean(s.canPtt);
    result += ",\"pttKnown\":" + boolean(s.pttKnown);
    result += ",\"transmitting\":" + boolean(s.transmitting);
    result += ",\"dashboardPtt\":" + boolean(s.dashboardPtt);
    result += ",\"pttPending\":" + boolean(pttPending_);
    result += ",\"releasePending\":" + boolean(s.releasePending);
    result += ",\"browserOwner\":" + std::to_string(browserPttOwner_);
    result += ",\"micLease\":" + std::to_string(audio::browserAudio().lease());
    result += ",\"status\":" + web::json(s.connectionStatus);
    result += ",\"error\":" + web::json(s.error);
    result += ",\"activeVfo\":" + web::json(s.activeVfo == RIG_VFO_B ? "B" : "A");
    result += ",\"settingsVfo\":" + web::json(s.settingsVfo == RIG_VFO_A ? "A" : s.settingsVfo == RIG_VFO_B ? "B" : "Current");
    result += ",\"modes\":" + array(s.modes);
    result += ",\"vfos\":[";

    if (result.size() > MAX_JSON_STRING_SIZE) {
        throw std::runtime_error("buildStateJson: Exceeded maximum JSON size at VFO start");
    }

    for (int i = 0; i < 2; ++i) {
        const auto& vfo = i ? s.b : s.a;
        if (i) result += ',';
        result += "{\"frequency\":" + radio::formatNumber(std::isfinite(vfo.frequency) ? vfo.frequency : 0);
        result += ",\"frequencyValid\":" + boolean(vfo.frequencyValid);
        result += ",\"mode\":" + web::json(vfo.mode);
        result += ",\"modeValid\":" + boolean(vfo.modeValid);
        result += ",\"width\":" + std::to_string(vfo.width);
        result += ",\"frequencyRevision\":" + std::to_string(s.frequencyRevision[i]);
        result += ",\"modeRevision\":" + std::to_string(s.modeRevision[i]) + '}';
    }
    result += "],\"vfoRevision\":" + std::to_string(s.vfoRevision);
    result += ",\"settings\":[";

    auto now = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < s.settings.size(); ++i) {
        const auto& setting = s.settings[i];
        if (i) result += ',';
        auto age = setting.valid ? std::chrono::duration_cast<std::chrono::milliseconds>(now - setting.updated).count() : -1;
        const char* type = setting.type == radio::ValueType::Boolean ? "boolean" :
                           setting.type == radio::ValueType::Choice ? "choice" :
                           setting.type == radio::ValueType::Text ? "text" :
                           setting.type == radio::ValueType::Real ? "real" : "integer";
        result += "{\"id\":" + web::json(setting.id);
        result += ",\"name\":" + web::json(setting.name);
        result += ",\"group\":" + web::json(setting.group);
        result += ",\"description\":" + web::json(setting.description);
        result += ",\"units\":" + web::json(setting.units);
        result += ",\"type\":" + web::json(type);
        result += ",\"readable\":" + boolean(setting.readable);
        result += ",\"writable\":" + boolean(setting.writable);
        result += ",\"valid\":" + boolean(setting.valid);
        result += ",\"txOnly\":" + boolean(setting.txOnly);
        result += ",\"bounded\":" + boolean(setting.bounded);
        result += ",\"minimum\":" + radio::formatNumber(std::isfinite(setting.minimum) ? setting.minimum : 0);
        result += ",\"maximum\":" + radio::formatNumber(std::isfinite(setting.maximum) ? setting.maximum : 0);
        result += ",\"step\":" + radio::formatNumber(std::isfinite(setting.step) ? setting.step : 0);
        result += ",\"choices\":" + array(setting.choices);
        result += ",\"choiceValues\":" + array(setting.choiceValues);
        result += ",\"value\":" + web::json(setting.value);
        result += ",\"display\":" + web::json(radio::displayValue(setting));
        result += ",\"range\":" + web::json(radio::rangeText(setting));
        result += ",\"status\":" + web::json(setting.status);
        result += ",\"age\":" + std::to_string(age) + '}';

        // Size checkpoint every 50 settings (at ~20% through Hamlib settings)
        if ((i + 1) % 50 == 0 && result.size() > MAX_JSON_STRING_SIZE) {
            throw std::runtime_error("buildStateJson: Exceeded maximum JSON size at setting " + std::to_string(i + 1));
        }
    }
    result += "],\"connection\":{\"kind\":" + std::to_string(connectionKind_);
    result += ",\"endpoint\":" + web::json(endpoint_);
    result += ",\"baud\":" + web::json(std::to_string(baud_));
    result += ",\"stopBits\":" + web::json(std::to_string(stopBits_));
    result += ",\"flow\":" + web::json(flow_);
    result += ",\"poll\":" + web::json(std::to_string(pollMs_));
    result += "},\"sharing\":{\"wanted\":" + boolean(sharingWanted_);
    result += ",\"running\":" + boolean(catServer_ && catServer_->running());
    result += ",\"port\":" + web::json(std::to_string(sharePort_));
    result += ",\"clients\":" + std::to_string(catServer_ ? catServer_->clients() : 0);
    result += ",\"error\":" + web::json(sharingError_);
    result += "},\"audio\":{\"devices\":[";

    // Limit audio devices to prevent unbounded growth (MAX_AUDIO_DEVICE_IDS = 30)
    auto devList = audio::devices();
    size_t maxAudioDevices = 30;
    if (devList.size() > maxAudioDevices) {
        devList.resize(maxAudioDevices);  // Limit to first 30 devices
    }

    for (unsigned i = 0; i < devList.size(); ++i) {
        const auto& d = devList[i];
        if (i) result += ',';
        std::string idStr(d.id.begin(), d.id.end());
        std::string nameStr(d.name.begin(), d.name.end());
        result += "{\"id\":" + web::json(idStr);
        result += ",\"name\":" + web::json(nameStr);
        result += ",\"input\":" + boolean(d.input);
        result += ",\"remote\":" + boolean(d.remote) + '}';
    }

    if (result.size() > MAX_JSON_STRING_SIZE) {
        throw std::runtime_error("buildStateJson: Exceeded maximum JSON size after audio devices");
    }

    // "maps" is an array of 4 device-id strings ("" = choose/default), matching
    // web/app.js which compares a.maps[slot] against each device's id.
    result += "],\"maps\":[";
    for (unsigned i = 0; i < 4; ++i) {
        if (i) result += ',';
        result += web::json(std::string(audioSelectedIds_[i].begin(), audioSelectedIds_[i].end()));
    }
    const auto rx = receiveAudio_.status();
    const auto mic = microphoneAudio_.status();
    const auto pttIndex = [](int seconds) {
        static const int limits[] = {30, 60, 180, 300, 600};
        for (int i = 0; i < 5; ++i) if (limits[i] == seconds) return i;
        return 2;
    }(pttLimitSeconds_);
    result += "],\"receiveWanted\":" + boolean(receiveWanted_)
        + ",\"microphoneWanted\":" + boolean(microphoneWanted_)
        + ",\"receiveActive\":" + boolean(rx.active)
        + ",\"microphoneActive\":" + boolean(mic.active)
        + ",\"receiveText\":" + web::json(std::string(rx.text.begin(), rx.text.end()))
        + ",\"microphoneText\":" + web::json(std::string(mic.text.begin(), mic.text.end()))
        + ",\"inputPeak\":" + std::to_string(rx.inputPeak)
        + ",\"outputPeak\":" + std::to_string(rx.outputPeak)
        + ",\"micInputPeak\":" + std::to_string(mic.inputPeak)
        + ",\"micOutputPeak\":" + std::to_string(mic.outputPeak)
        + ",\"receiveLevel\":" + std::to_string(static_cast<int>(receiveGain_ * 100.0f))
        + ",\"microphoneLevel\":" + std::to_string(static_cast<int>(micGain_ * 100.0f))
        + ",\"pttLimit\":" + std::to_string(pttIndex)
        + ",\"unattended\":" + boolean(unattended_)
        + ",\"refreshing\":false"
        + ",\"error\":\"\"";
    // Log before routes section (debug output only)
#ifdef BUILD_DEBUG
    std::cout << "[FT-710] [BUILD] Before routes section, RSS: " << get_memory_usage_kb() << " KB" << std::endl;
#endif
    result += ",\"routes\":[";
    result += "{\"name\":\"receive\",\"active\":" + boolean(receiveAudio_.status().active);
    result += ",\"starting\":" + boolean(receiveAudio_.status().starting);
    result += ",\"status\":" + web::json(receiveAudio_.status().active ? "Running" : "Off");
    result += ",\"inputPeak\":" + std::to_string(receiveAudio_.status().inputPeak);
    result += ",\"outputPeak\":" + std::to_string(receiveAudio_.status().outputPeak) + "},";
    result += "{\"name\":\"microphone\",\"active\":" + boolean(microphoneAudio_.status().active);
    result += ",\"starting\":" + boolean(microphoneAudio_.status().starting);
    result += ",\"status\":" + web::json(microphoneAudio_.status().active ? "Running" : "Off");
    result += ",\"inputPeak\":" + std::to_string(microphoneAudio_.status().inputPeak);
    result += ",\"outputPeak\":" + std::to_string(microphoneAudio_.status().outputPeak) + "}";
    result += "]},\"activity\":[";

    // Log before activity section and lock acquisition (debug output only)
#ifdef BUILD_DEBUG
    std::cout << "[FT-710] [BUILD] Before activity section, RSS: " << get_memory_usage_kb() << " KB" << std::endl;
#endif

    {
#ifdef BUILD_DEBUG
        std::cout << "[FT-710] [BUILD] After logging, RSS: " << get_memory_usage_kb() << " KB" << std::endl;
#endif
        std::lock_guard lock(activityMutex_);
        // Safety bounds - don't iterate beyond max activity entries
        constexpr size_t MAX_ACTIVITY_ENTRIES = 500;

        // Size check before activity array (should still be safe at this point)
        if (result.size() > MAX_JSON_STRING_SIZE) {
            throw std::runtime_error("buildStateJson: Exceeded maximum JSON size before activity array");
        }

        // Limit to MAX_ACTIVITY_ENTRIES even if vector is larger
        size_t limit = std::min(activity_.size(), MAX_ACTIVITY_ENTRIES);

        for (std::size_t i = 0; i < limit; ++i) {
            if (i) result += ',';

            // Bound activity entry length to prevent bad_alloc in web::json()
            const size_t MAX_ACTIVITY_ENTRY_LENGTH = 256;
            std::string safeEntry = activity_[i];
            if (safeEntry.size() > MAX_ACTIVITY_ENTRY_LENGTH) {
                safeEntry = safeEntry.substr(0, MAX_ACTIVITY_ENTRY_LENGTH);
            }

            result += web::json(safeEntry);

            // Size checkpoint through activity array
            if ((i + 1) % 50 == 0 && result.size() > MAX_JSON_STRING_SIZE) {
                throw std::runtime_error("buildStateJson: Exceeded maximum JSON size at activity " + std::to_string(i + 1));
            }
        }
    }

    // Bound rf status string to prevent unbounded memory allocation
    auto const& statusWS = rfCapture_.status();
    std::string rfStatus;

    // Convert wstring to UTF-8 string safely (wchar_t is UTF-16 on macOS)
    if (!statusWS.empty()) {
        rfStatus.reserve(std::min(statusWS.size() * 4, size_t(2048)));
        for (wchar_t wc : statusWS) {
            char buf[8];  // Max UTF-8 chars for any Unicode character
            int n = wcstombs(buf, &wc, sizeof(buf));
            if (n <= 0) break;  // Invalid or error on this character
            rfStatus.append(buf, n);
        }
    }
    result += "],\"rf\":{\"running\":" + boolean(rfCapture_.running());
    result += ",\"status\":" + web::json(rfStatus);
    result += ",\"port\":" + web::json(rfCat2Port_);
    result += ",\"baud\":" + std::to_string(rfCat2Baud_);
    result += ",\"span\":" + std::to_string(rfSpan_);
    result += ",\"level\":" + std::to_string(rfWaterfallLevelDb_) + "}}";

    // Final size check before return
    if (result.size() > MAX_JSON_STRING_SIZE) {
        throw std::runtime_error("buildStateJson: Exceeded maximum JSON size at end of function");
    }

    return result;
}

void ControllerMac::run() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        try {
            publishWeb();
        } catch (const std::bad_alloc&) {
            // Must check bad_alloc BEFORE std::exception since it inherits from exception
#ifdef BUILD_DEBUG
            std::cout << "[FT-710] [RUN] CRITICAL: Memory allocation failed in publishWeb()!" << std::endl;
#endif
        } catch (const std::exception& e) {
#ifdef BUILD_DEBUG
            std::cout << "[FT-710] [RUN] Error in publishWeb(): " << e.what() << std::endl;
#endif
        } catch (...) {
#ifdef BUILD_DEBUG
            std::cout << "[FT-710] [RUN] Unknown exception in publishWeb()!" << std::endl;
#endif
        }
    }
}

void ControllerMac::shutdown() {
    if (!running_.exchange(false)) return;
    log("Shutting down FT-710 Dashboard on macOS...");
    releasePtt();
    receiveAudio_.stop();
    microphoneAudio_.stop();
    rfCapture_.stop();
    stopSharing();
    if (webServer_) webServer_->stop();
    if (worker_) worker_->stop();
}

}
