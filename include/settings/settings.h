#pragma once
#include <string>

struct AppConfig {
    int cameraId = 0;
    int intervalMs = 1000;
    double darkThreshold = 20.0;
    int fps = 30;
    int resWidth = 2560;
    int resHeight = 1440;
    std::string imageFolder = "failsafe_frames";
    std::string videoOutput = "MasterLapse_Output.avi";
};

class Settings {
public:
    Settings(const std::string& configFile = "config.txt");
    ~Settings();

    void load();
    void save();
    
    AppConfig config;

private:
    std::string config_file_;
};
