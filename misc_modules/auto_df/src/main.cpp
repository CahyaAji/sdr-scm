#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <core.h>
#include <chrono>
#include <sstream>
#include <iomanip>

SDRPP_MOD_INFO{
    /* Name:            */ "auto_df",
    /* Description:     */ "Auto DF - Simple frequency watcher",
    /* Author:          */ "Your Name",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

class AutoDFModule : public ModuleManager::Instance {
public:
    AutoDFModule(std::string name) {
        this->name = name;
        enabled = true; // Start enabled by default since we removed the checkbox
        watcherActive = false;

        // Timer variables
        currentFrequency = 100000000.0; // Start with 100 MHz
        lastFrequency = 100000000.0;
        lastChangeTime = std::chrono::steady_clock::now();
        timeoutSeconds = 5.0;
        hasPendingNotification = false; // No pending notification initially

        // Register GUI callback
        gui::menu.registerEntry(name, menuHandler, this, NULL);
    }

    ~AutoDFModule() {
        gui::menu.removeEntry(name);
    }

    void postInit() {}
    void enable() { enabled = true; }
    void disable() {
        enabled = false;
        watcherActive = false;
    }
    bool isEnabled() { return enabled; }

private:
    static void menuHandler(void* ctx) {
        AutoDFModule* _this = (AutoDFModule*)ctx;
        _this->menuHandlerImpl();
    }

    void menuHandlerImpl() {
        // Removed the checkbox - plugin is always enabled

        ImGui::Text("Status: %s", watcherActive ? "ACTIVE" : "STOPPED");

        if (ImGui::Button("Start Auto DF")) {
            watcherActive = true;
            updateFrequency();

            // Force initial notification when starting
            lastChangeTime = std::chrono::steady_clock::now();
            hasPendingNotification = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Auto DF")) {
            watcherActive = false;
            hasPendingNotification = false; // Clear any pending notifications
        }

        if (watcherActive) {
            updateFrequency();
            checkForStableFrequency();

            ImGui::Separator();
            ImGui::Text("Current Frequency: %.0f Hz", currentFrequency);
            ImGui::Text("Timeout: %.1f seconds", timeoutSeconds);

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChangeTime).count() / 1000.0;

            if (hasPendingNotification) {
                ImGui::Text("Sending in: %.1f seconds", timeoutSeconds - elapsed);
            }
            else {
                ImGui::Text("Waiting for frequency change...");
            }
        }
    }

    void updateFrequency() {
        double newFreq = 0.0;

        // Get the selected VFO frequency (the actual tuned frequency)
        if (!gui::waterfall.selectedVFO.empty()) {
            // VFO is selected - get center frequency + VFO offset
            newFreq = gui::waterfall.getCenterFrequency() + gui::waterfall.vfos[gui::waterfall.selectedVFO]->centerOffset;
        }
        else {
            // No VFO selected - fall back to waterfall center
            newFreq = gui::waterfall.getCenterFrequency();
        }

        // Check if frequency actually changed (1 Hz sensitivity)
        if (abs(newFreq - currentFrequency) > 1.0) {
            lastFrequency = currentFrequency;
            currentFrequency = newFreq;

            // Reset timer when frequency changes
            lastChangeTime = std::chrono::steady_clock::now();
            hasPendingNotification = true; // Mark that we need to send notification later
        }
    }

    void checkForStableFrequency() {
        // Only check if we have a pending notification and frequency changed
        if (!hasPendingNotification) {
            return; // No frequency change, no counting
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastChangeTime).count();

        // Send notification 5 seconds after frequency stopped changing
        if (elapsed >= timeoutSeconds) {
            sendNotification();
            hasPendingNotification = false; // Clear the pending flag
        }
    }

    void sendNotification() {
        printf("FREQUENCY STABLE: %.0f Hz (stable for %.0f seconds)\n",
               currentFrequency, timeoutSeconds);

        // Always send UDP packet when frequency is stable
        std::thread udpThread(&AutoDFModule::sendUDP, this, currentFrequency);
        udpThread.detach(); // Run in background
    }

    std::string getCurrentTimestamp() {
        // Get current time as milliseconds since epoch
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(timestamp);
    }

    std::string createJsonMessage(double frequency) {
        std::stringstream json;
        json << std::fixed << std::setprecision(0); // No decimal places for frequency

        json << "{"
             << "\"type\":\"number\","
             << "\"data\":{"
             << "\"value\":" << frequency
             << "},"
             << "\"timestamp\":" << getCurrentTimestamp()
             << "}";

        return json.str();
    }

    void sendUDP(double frequency) {
        // Create JSON message
        std::string jsonMessage = createJsonMessage(frequency);

        // Use netcat (nc) command to send UDP packet - using private port 55555
        std::stringstream command;
        command << "echo '" << jsonMessage << "' | nc -u -w1 127.0.0.1 55555 2>/dev/null";

        int result = system(command.str().c_str());

        if (result == 0) {
            printf("UDP sent: %s to 127.0.0.1:55555\n", jsonMessage.c_str());
        }
        else {
            printf("UDP ERROR: Failed to send packet (is netcat installed?)\n");
        }
    }

private:
    std::string name;
    bool enabled;
    bool watcherActive;
    double currentFrequency;
    double lastFrequency;
    std::chrono::steady_clock::time_point lastChangeTime;
    double timeoutSeconds;
    bool hasPendingNotification; // Track if we need to send notification after timeout
};

MOD_EXPORT void _INIT_() {}
MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new AutoDFModule(name);
}
MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (AutoDFModule*)instance;
}
MOD_EXPORT void _END_() {}