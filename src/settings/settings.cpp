#include "settings/settings.h"
#include <iostream>
#include <fstream>
#include <sstream>

Settings::Settings(const std::string& configFile) : config_file_(configFile) {
    load();
}

Settings::~Settings() {
    save();
}

void Settings::load() {
    std::ifstream file(config_file_);
    if (!file.is_open()) {
        std::cout << "[Settings] No config file found. Using defaults. Creating new file..." << std::endl;
        save();
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                if (key == "cameraId") config.cameraId = std::stoi(value);
                else if (key == "intervalMs") config.intervalMs = std::stoi(value);
                else if (key == "darkThreshold") config.darkThreshold = std::stod(value);
                else if (key == "fps") config.fps = std::stoi(value);
                else if (key == "resWidth") config.resWidth = std::stoi(value);
                else if (key == "resHeight") config.resHeight = std::stoi(value);
            }
        }
    }
    std::cout << "[Settings] Configuration loaded successfully." << std::endl;
}

void Settings::save() {
    std::ofstream file(config_file_);
    if (file.is_open()) {
        file << "cameraId=" << config.cameraId << "\n";
        file << "intervalMs=" << config.intervalMs << "\n";
        file << "darkThreshold=" << config.darkThreshold << "\n";
        file << "fps=" << config.fps << "\n";
        file << "resWidth=" << config.resWidth << "\n";
        file << "resHeight=" << config.resHeight << "\n";
        std::cout << "[Settings] Configuration saved." << std::endl;
    } else {
        std::cerr << "[Settings] ERROR: Could not save configuration." << std::endl;
    }
}

