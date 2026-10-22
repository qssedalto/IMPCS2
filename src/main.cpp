#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Xinput9_1_0.lib")

namespace {
using Clock = std::chrono::steady_clock;

struct Point {
    double x = 0.5;
    double y = 0.5;
};

struct Profile {
    int pollHz = 120;
    double deadzone = 0.18;
    double stickRadius = 0.10;
    Point stickCenter{0.16, 0.78};
    std::map<std::string, Point> buttons;
};

struct Touch {
    Point p;
    std::string action;
};

std::atomic<bool> gReady{false};
std::atomic<bool> gStop{false};
std::atomic<unsigned long long> gSequence{0};
std::mutex gWriteMutex;

std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

double Clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

Profile LoadProfile(const std::string& path) {
    Profile profile;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Warning: profile not found; using example defaults: " << path << "\n";
        return profile;
    }

    std::string section, line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        try {
            const double number = std::stod(value);
            if (section == "general") {
                if (key == "poll_hz") profile.pollHz = std::clamp(static_cast<int>(number), 30, 240);
                else if (key == "stick_deadzone") profile.deadzone = std::clamp(number, 0.0, 0.9);
                else if (key == "stick_radius") profile.stickRadius = std::clamp(number, 0.01, 0.45);
            } else if (section == "virtual_stick") {
                if (key == "center_x") profile.stickCenter.x = Clamp01(number);
                else if (key == "center_y") profile.stickCenter.y = Clamp01(number);
            } else if (section == "buttons") {
                const auto pos = key.find("_x");
                if (pos != std::string::npos && pos + 2 == key.size()) {
                    profile.buttons[key.substr(0, pos)].x = Clamp01(number);
                } else {
                    const auto yPos = key.find("_y");
                    if (yPos != std::string::npos && yPos + 2 == key.size())
                        profile.buttons[key.substr(0, yPos)].y = Clamp01(number);
                }
            }
        } catch (...) {
            std::cerr << "Warning: invalid profile value for " << key << "\n";
        }
    }

    for (auto& [name, point] : profile.buttons) {
        if (!std::isfinite(point.x)) point.x = 0.5;
        if (!std::isfinite(point.y)) point.y = 0.5;
    }
    return profile;
}

std::string EscapeJson(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (c == '\\' || c == '"') { out.push_back('\\'); out.push_back(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);
    }
    return out;
}

class Bridge {
public:
    ~Bridge() { Stop(); }

    bool Start(const std::string& exe, const std::string& udid) {
        SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE childStdoutRead = nullptr, childStdoutWrite = nullptr;
        HANDLE childStdinRead = nullptr, childStdinWrite = nullptr;
        HANDLE childStderrRead = nullptr, childStderrWrite = nullptr;
        if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0) ||
            !SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0) ||
            !CreatePipe(&childStdinRead, &childStdinWrite, &sa, 0) ||
            !SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0) ||
            !CreatePipe(&childStderrRead, &childStderrWrite, &sa, 0) ||
            !SetHandleInformation(childStderrRead, HANDLE_FLAG_INHERIT, 0)) {
            std::cerr << "Could not create bridge pipes.\n";
            return false;
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = childStdinRead;
        si.hStdOutput = childStdoutWrite;
        si.hStdError = childStderrWrite;
        PROCESS_INFORMATION pi{};
        std::string command = "\"" + exe + "\" --usb --udid \"" + udid + "\" --rate-hz 120";
        std::vector<char> cmd(command.begin(), command.end());
        cmd.push_back('\0');

        const BOOL created = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(childStdoutWrite);
        CloseHandle(childStdinRead);
        CloseHandle(childStderrWrite);
        if (!created) {
            std::cerr << "Could not start iUsbBridge.exe. Win32 error: " << GetLastError() << "\n";
            CloseHandle(childStdoutRead); CloseHandle(childStdinWrite); CloseHandle(childStderrRead);
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        stdout_ = childStdoutRead;
        stdin_ = childStdinWrite;
        stderr_ = childStderrRead;
        stdoutThread_ = std::thread([this] { ReadStdout(); });
        stderrThread_ = std::thread([this] { DrainStderr(); });
        std::cout << "Started iUsbBridge; waiting for ready event...\n";
        return true;
    }

    bool Ready() const { return gReady.load(); }

    bool SendTouches(const std::vector<std::pair<int, Touch>>& changes) {
        if (changes.empty() || !Ready() || stdin_ == INVALID_HANDLE_VALUE) return changes.empty();
        std::ostringstream json;
        json << "{\"schema\":\"iphoneMirror.touch.v2\",\"kind\":\"touch_batch\",\"seq\":"
             << ++gSequence << ",\"timestampNs\":0,\"points\":[";
        bool first = true;
        for (const auto& [id, touch] : changes) {
            if (!first) json << ',';
            first = false;
            json << "{\"pointerId\":" << id
                 << ",\"action\":\"" << EscapeJson(touch.action)
                 << "\",\"normalizedX\":" << touch.p.x
                 << ",\"normalizedY\":" << touch.p.y << '}';
        }
        json << "]}";
        const std::string payload = json.str();
        if (payload.size() > 65536) return false;
        const uint32_t length = static_cast<uint32_t>(payload.size());
        const uint8_t header[4] = {
            static_cast<uint8_t>(length & 0xff),
            static_cast<uint8_t>((length >> 8) & 0xff),
            static_cast<uint8_t>((length >> 16) & 0xff),
            static_cast<uint8_t>((length >> 24) & 0xff)
        };
        std::lock_guard<std::mutex> lock(gWriteMutex);
        DWORD written = 0;
        if (!WriteFile(stdin_, header, sizeof(header), &written, nullptr) || written != sizeof(header)) return false;
        if (!WriteFile(stdin_, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) ||
            written != payload.size()) return false;
        return true;
    }

    void Stop() {
        gStop.store(true);
        gReady.store(false);
        if (stdin_ != INVALID_HANDLE_VALUE) {
            CloseHandle(stdin_);
            stdin_ = INVALID_HANDLE_VALUE;
        }
        if (process_) {
            if (WaitForSingleObject(process_, 1500) == WAIT_TIMEOUT) {
                TerminateProcess(process_, 0);
                WaitForSingleObject(process_, 1000);
            }
            CloseHandle(process_);
            process_ = nullptr;
        }
        if (stdout_ != INVALID_HANDLE_VALUE) { CloseHandle(stdout_); stdout_ = INVALID_HANDLE_VALUE; }
        if (stderr_ != INVALID_HANDLE_VALUE) { CloseHandle(stderr_); stderr_ = INVALID_HANDLE_VALUE; }
        if (stdoutThread_.joinable()) stdoutThread_.join();
        if (stderrThread_.joinable()) stderrThread_.join();
    }

private:
    void ReadStdout() {
        std::string line;
        char c;
        while (!gStop.load() && stdout_ != INVALID_HANDLE_VALUE) {
            DWORD read = 0;
            if (!ReadFile(stdout_, &c, 1, &read, nullptr) || read == 0) break;
            if (c != '\n') {
                if (line.size() < 8192) line.push_back(c);
                continue;
            }
            if (line.find("\"event\":\"ready\"") != std::string::npos ||
                line.find("\"event\": \"ready\"") != std::string::npos) {
                gReady.store(true);
                std::cout << "iUsbBridge is READY.\n";
            } else if (line.find("\"event\":\"error\"") != std::string::npos ||
                       line.find("\"event\": \"error\"") != std::string::npos) {
                std::cerr << "iUsbBridge reported an error: " << line << "\n";
            } else if (!line.empty()) {
                std::cout << "[bridge] " << line << "\n";
            }
            line.clear();
        }
    }

    void DrainStderr() {
        char buffer[1024];
        DWORD read = 0;
        while (!gStop.load() && stderr_ != INVALID_HANDLE_VALUE) {
            if (!ReadFile(stderr_, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
            std::string s(buffer, buffer + read);
            std::cerr << s;
        }
    }

    HANDLE process_ = nullptr;
    HANDLE stdin_ = INVALID_HANDLE_VALUE;
    HANDLE stdout_ = INVALID_HANDLE_VALUE;
    HANDLE stderr_ = INVALID_HANDLE_VALUE;
    std::thread stdoutThread_;
    std::thread stderrThread_;
};

struct Binding {
    WORD mask;
    std::string name;
};

const std::vector<Binding> kBindings = {
    { XINPUT_GAMEPAD_A, "a" }, { XINPUT_GAMEPAD_B, "b" },
    { XINPUT_GAMEPAD_X, "x" }, { XINPUT_GAMEPAD_Y, "y" },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, "lb" }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, "rb" },
    { XINPUT_GAMEPAD_LEFT_THUMB, "ls" }, { XINPUT_GAMEPAD_RIGHT_THUMB, "rs" },
    { XINPUT_GAMEPAD_START, "start" }, { XINPUT_GAMEPAD_BACK, "back" }
};

void ReleaseAll(Bridge& bridge, std::map<int, Point>& active) {
    std::vector<std::pair<int, Touch>> changes;
    for (const auto& [id, p] : active) changes.push_back({id, Touch{p, "up"}});
    bridge.SendTouches(changes);
    active.clear();
}

BOOL WINAPI ConsoleControlHandler(DWORD eventType) {
    if (eventType == CTRL_C_EVENT || eventType == CTRL_BREAK_EVENT || eventType == CTRL_CLOSE_EVENT || eventType == CTRL_SHUTDOWN_EVENT) {
        gStop.store(true);
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::cout << "IMPCS2 - Switch 2 Pro to iPad touch adapter\n\n"
              << "Usage:\n"
              << "  IMPCS2.exe --dry-run [--controller-index 0] [--profile path]\n"
              << "  IMPCS2.exe --udid UDID [--bridge path] [--controller-index 0] [--profile path]\n\n"
              << "Options:\n"
              << "  --dry-run              Show XInput state without connecting to iPad\n"
              << "  --udid UDID            Target iPhone/iPad UDID (required outside dry-run)\n"
              << "  --bridge path          Path to iUsbBridge.exe\n"
              << "  --controller-index N   XInput controller slot, 0-3 (default 0)\n"
              << "  --profile path         Mapping profile (default config/cult-of-the-lamb.example.ini)\n";
}

std::string FindBridge(const std::string& requested) {
    if (!requested.empty()) return requested;
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path(exePath);
    const auto slash = path.find_last_of("\\/");
    if (slash != std::string::npos) path.resize(slash + 1);
    const std::vector<std::string> candidates = {
        path + "iUsbBridge.exe",
        "C:\\Program Files\\iPhoneMirror\\iUsbBridge.exe",
        "C:\\Program Files (x86)\\iPhoneMirror\\iUsbBridge.exe"
    };
    for (const auto& candidate : candidates) {
        const DWORD attr = GetFileAttributesA(candidate.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return candidate;
    }
    return {};
}
}

int main(int argc, char** argv) {
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    bool dryRun = false;
    std::string udid, bridgePath, profilePath = "config/cult-of-the-lamb.example.ini";
    int controllerIndex = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (arg == "--help" || arg == "-h") { PrintUsage(); return 0; }
        if (arg == "--dry-run") dryRun = true;
        else if (arg == "--udid") udid = next();
        else if (arg == "--bridge") bridgePath = next();
        else if (arg == "--profile") profilePath = next();
        else if (arg == "--controller-index") {
            try { controllerIndex = std::clamp(std::stoi(next()), 0, 3); }
            catch (...) { std::cerr << "Invalid controller index.\n"; return 2; }
        } else { std::cerr << "Unknown option: " << arg << "\n"; PrintUsage(); return 2; }
    }
    if (!dryRun && udid.empty()) { std::cerr << "--udid is required unless --dry-run is used.\n"; return 2; }

    const Profile profile = LoadProfile(profilePath);
    std::cout << "IMPCS2 0.1.0 | XInput slot " << controllerIndex
              << " | poll " << profile.pollHz << " Hz\n";
    std::cout << "Profile: " << profilePath << "\n";
    std::cout << "NOTE: example touch coordinates are not calibrated. Test cautiously.\n";

    Bridge bridge;
    if (!dryRun) {
        bridgePath = FindBridge(bridgePath);
        if (bridgePath.empty()) {
            std::cerr << "iUsbBridge.exe not found. Pass --bridge with its full path.\n";
            return 3;
        }
        if (!bridge.Start(bridgePath, udid)) return 4;
        for (int i = 0; i < 180 && !bridge.Ready() && !gStop.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!bridge.Ready()) {
            std::cerr << "Bridge did not become ready within 18 seconds. Check its status/error above.\n";
            bridge.Stop();
            return 5;
        }
    }

    std::map<int, Point> active;
    XINPUT_STATE state{};
    WORD previousButtons = 0;
    bool stickActive = false;
    Point lastStick{};
    const auto period = std::chrono::microseconds(1000000 / profile.pollHz);

    while (!gStop.load()) {
        const auto start = Clock::now();
        const DWORD result = XInputGetState(static_cast<DWORD>(controllerIndex), &state);
        if (result != ERROR_SUCCESS) {
            if (!active.empty()) ReleaseAll(bridge, active);
            stickActive = false;
            previousButtons = 0;
            std::cout << "\rWaiting for virtual XInput controller...          " << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        const SHORT sx = state.Gamepad.sThumbLX;
        const SHORT sy = state.Gamepad.sThumbLY;
        double nx = std::clamp(static_cast<double>(sx) / 32767.0, -1.0, 1.0);
        double ny = std::clamp(-static_cast<double>(sy) / 32768.0, -1.0, 1.0);
        const double magnitude = std::sqrt(nx * nx + ny * ny);
        const bool wantStick = magnitude > profile.deadzone;
        Point stickPoint = profile.stickCenter;
        if (wantStick) {
            const double scaled = std::min(1.0, (magnitude - profile.deadzone) / (1.0 - profile.deadzone));
            stickPoint.x = Clamp01(profile.stickCenter.x + nx / std::max(magnitude, 0.001) * scaled * profile.stickRadius);
            stickPoint.y = Clamp01(profile.stickCenter.y + ny / std::max(magnitude, 0.001) * scaled * profile.stickRadius);
        }

        std::map<int, Point> desired;
        if (wantStick) desired[0] = stickPoint;

        std::vector<std::string> heldButtonNames;
        for (const auto& binding : kBindings) {
            if ((state.Gamepad.wButtons & binding.mask) == 0) continue;
            auto it = profile.buttons.find(binding.name);
            if (it == profile.buttons.end()) continue;
            heldButtonNames.push_back(binding.name);
            // Reserve pointer IDs 1.. for buttons; iUsbBridge caps active contacts at five.
            desired[static_cast<int>(&binding - kBindings.data()) + 1] = it->second;
        }
        if (state.Gamepad.bLeftTrigger > 30) {
            auto it = profile.buttons.find("lt");
            if (it != profile.buttons.end()) desired[11] = it->second;
        }
        if (state.Gamepad.bRightTrigger > 30) {
            auto it = profile.buttons.find("rt");
            if (it != profile.buttons.end()) desired[12] = it->second;
        }

        // The bridge supports at most five simultaneous contacts. Keep the stick first,
        // then stable button priority. Pointer IDs remain stable across frames.
        while (desired.size() > 5) {
            auto it = std::prev(desired.end());
            desired.erase(it);
        }

        std::vector<std::pair<int, Touch>> changes;
        for (const auto& [id, point] : active) {
            if (desired.find(id) == desired.end()) changes.push_back({id, Touch{point, "up"}});
        }
        for (const auto& [id, point] : desired) {
            auto old = active.find(id);
            if (old == active.end()) changes.push_back({id, Touch{id == 0 ? profile.stickCenter : point, "down"}});
            else if (std::abs(old->second.x - point.x) > 0.001 || std::abs(old->second.y - point.y) > 0.001)
                changes.push_back({id, Touch{point, "move"}});
        }

        if (dryRun) {
            if (state.Gamepad.wButtons != previousButtons ||
                std::abs(nx) > 0.05 || std::abs(ny) > 0.05) {
                std::cout << "\rbuttons=0x" << std::hex << state.Gamepad.wButtons << std::dec
                          << " LX=" << sx << " LY=" << sy
                          << " LT=" << static_cast<int>(state.Gamepad.bLeftTrigger)
                          << " RT=" << static_cast<int>(state.Gamepad.bRightTrigger) << "     " << std::flush;
            }
        } else if (!bridge.SendTouches(changes)) {
            std::cerr << "\nFailed to send touch event; stopping to avoid stuck contacts.\n";
            ReleaseAll(bridge, active);
            break;
        }

        active = desired;
        previousButtons = state.Gamepad.wButtons;
        const auto elapsed = Clock::now() - start;
        if (elapsed < period) std::this_thread::sleep_for(period - elapsed);
    }

    if (!dryRun) ReleaseAll(bridge, active);
    bridge.Stop();
    std::cout << "\nIMPCS2 stopped.\n";
    return 0;
}
