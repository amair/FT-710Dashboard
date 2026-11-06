#include "controller_mac.hpp"
#include <iostream>
#include <csignal>
#include <filesystem>
#include <vector>
#include <string>

namespace {
app::ControllerMac* gController = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[!] Received signal " << signum << ". Stopping gracefully..." << std::endl;
    if (gController) {
        gController->shutdown();
    }
    std::exit(0);
}

void printBanner() {
    std::cout << "================================================================================\n"
              << "       FT-710 Dashboard for macOS · Yaesu Remote Station Control\n"
              << "================================================================================\n"
              << " Developed and Ported to macOS by: Yury Jajitzky (YY3BIG)\n"
              << " Based on original FT-710Dashboard by: M0NXD (https://github.com/M0NXD/FT-710Dashboard)\n"
              << " License: Apache License 2.0 (See LICENSE and NOTICE files)\n"
              << "--------------------------------------------------------------------------------\n"
              << " [!] AVISO LEGAL Y DE SEGURIDAD (DISCLAIMER.md):\n"
              << " Este software es estrictamente EXPERIMENTAL para el servicio de radioaficionados.\n"
              << " El Operador de Control es el UNICO responsable de su estacion, frecuencia, ROE,\n"
              << " potencia de RF y accionamiento de PTT. Utilice carga ficticia en pruebas iniciales.\n"
              << "================================================================================\n\n";
}

std::string autoDetectFt710Port() {
    std::vector<std::string> candidates = {
        "/dev/cu.SLAB_USBtoUART",
        "/dev/cu.SLAB_USBtoUART1",
        "/dev/cu.usbserial-0001",
        "/dev/cu.usbserial-10",
        "/dev/cu.usbserial-110",
        "/dev/cu.usbserial-1410"
    };
    for (const auto& dev : candidates) {
        if (std::filesystem::exists(dev)) {
            return dev;
        }
    }
    // Search /dev for any SLAB or usbserial
    try {
        for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
            std::string path = entry.path().string();
            if (path.find("cu.SLAB_USBtoUART") != std::string::npos ||
                path.find("cu.usbserial") != std::string::npos) {
                return path;
            }
        }
    } catch (...) {}
    return "";
}
}

int main(int argc, char* argv[]) {
    printBanner();

    bool demo = false;
    bool openBrowser = true;
    std::string port;
    int baud = 38400;
    std::string passcode;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--demo") demo = true;
        else if (arg == "--no-browser") openBrowser = false;
        else if (arg == "--port" && i + 1 < argc) port = argv[++i];
        else if (arg == "--baud" && i + 1 < argc) baud = std::stoi(argv[++i]);
        else if (arg == "--passcode" && i + 1 < argc) passcode = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: FT710Dashboard-macOS [OPTIONS]\n"
                      << "  --demo            Run with Hamlib Dummy backend (no physical radio required)\n"
                      << "  --port <path>     Serial port for Enhanced CAT-1 (default: auto-detect)\n"
                      << "  --baud <rate>     CAT baud rate (default: 38400)\n"
                      << "  --passcode <code> Web dashboard passcode\n"
                      << "  --no-browser      Do not open web browser automatically at launch\n"
                      << "  --help, -h        Show this help message\n";
            return 0;
        }
    }

    if (!demo && port.empty()) {
        port = autoDetectFt710Port();
        if (!port.empty()) {
            std::cout << "[*] Auto-detected FT-710 serial port on macOS: " << port << std::endl;
        } else {
            std::cout << "[!] No FT-710 USB serial port detected in /dev/. Starting in Demo mode.\n"
                      << "    Connect radio USB or specify --port /dev/cu.SLAB_USBtoUART\n";
            demo = true;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    // Writing to a socket whose peer has gone away (browser tab closed,
    // page refresh, CAT client dropped) must not kill the station dashboard.
    // Ignoring SIGPIPE makes the send fail with EPIPE instead, which the web
    // and CAT servers already treat as a client disconnect.
    std::signal(SIGPIPE, SIG_IGN);

    try {
        app::ControllerMac controller;
        gController = &controller;

        controller.initialize(demo, port, baud, passcode);

        std::cout << "\n--------------------------------------------------------------------------------\n"
                  << "[+] Web Dashboard is running on port 8888\n"
                  << "    Local URL:   https://localhost:8888/\n"
                  << "    Setup URL:   http://localhost:8888/setup (for Safari/iPhone cert setup)\n"
                  << "    CAT Sharing: 127.0.0.1:4532 (NET rigctl for WSJT-X, fldigi, etc.)\n"
                  << "--------------------------------------------------------------------------------\n"
                  << "Press Ctrl+C to stop the station dashboard.\n\n";

        if (openBrowser) {
            system("open http://localhost:8888/setup");
        }

        controller.run();
    } catch (const std::exception& e) {
        std::cerr << "\n[!] Error starting station dashboard: " << e.what() << "\n\n";
        return 1;
    } catch (...) {
        std::cerr << "\n[!] Unexpected fatal error starting station dashboard.\n\n";
        return 1;
    }

    return 0;
}
