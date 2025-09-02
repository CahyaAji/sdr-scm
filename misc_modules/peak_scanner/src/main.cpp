#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <chrono>
#include <algorithm>
#include <vector>

SDRPP_MOD_INFO{
    /* Name:            */ "peak_scanner",
    /* Description:     */ "Peak-detecting frequency scanner for SDR++",
    /* Author:          */ "Based on Ryzerth's scanner with peak detection",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

class PeakScannerModule : public ModuleManager::Instance {
public:
    PeakScannerModule(std::string name) {
        this->name = name;
        gui::menu.registerEntry(name, menuHandler, this, NULL);
    }

    ~PeakScannerModule() {
        gui::menu.removeEntry(name);
        stop();
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void menuHandler(void* ctx) {
        PeakScannerModule* _this = (PeakScannerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (_this->running) { ImGui::BeginDisabled(); }
        ImGui::LeftLabel("Start");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##start_freq_peak_scanner", &_this->startFreq, 100.0, 100000.0, "%0.0f")) {
            _this->startFreq = round(_this->startFreq);
        }
        ImGui::LeftLabel("Stop");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##stop_freq_peak_scanner", &_this->stopFreq, 100.0, 100000.0, "%0.0f")) {
            _this->stopFreq = round(_this->stopFreq);
        }
        ImGui::LeftLabel("Interval");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##interval_peak_scanner", &_this->interval, 100.0, 100000.0, "%0.0f")) {
            _this->interval = round(_this->interval);
        }
        ImGui::LeftLabel("Search Width (Hz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##search_width_peak_scanner", &_this->searchWidth, 1000.0, 10000.0, "%0.0f")) {
            _this->searchWidth = std::clamp<double>(round(_this->searchWidth), 1000.0, 100000.0);
        }
        ImGui::LeftLabel("Peak Step (Hz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##peak_step_peak_scanner", &_this->peakStep, 100.0, 1000.0, "%0.0f")) {
            _this->peakStep = std::clamp<double>(round(_this->peakStep), 100.0, 10000.0);
        }
        ImGui::LeftLabel("Passband Ratio (%)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##pb_ratio_peak_scanner", &_this->passbandRatio, 1.0, 10.0, "%0.0f")) {
            _this->passbandRatio = std::clamp<double>(round(_this->passbandRatio), 1.0, 100.0);
        }
        ImGui::LeftLabel("Tuning Time (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##tuning_time_peak_scanner", &_this->tuningTime, 100, 1000)) {
            _this->tuningTime = std::clamp<int>(_this->tuningTime, 100, 10000.0);
        }
        ImGui::LeftLabel("Linger Time (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##linger_time_peak_scanner", &_this->lingerTime, 100, 1000)) {
            _this->lingerTime = std::clamp<int>(_this->lingerTime, 100, 10000.0);
        }
        if (_this->running) { ImGui::EndDisabled(); }

        ImGui::LeftLabel("Level");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        ImGui::SliderFloat("##peak_scanner_level", &_this->level, -150.0, 0.0);

        // Show peak detection info
        if (_this->running && _this->receiving) {
            ImGui::Separator();
            ImGui::Text("Peak Freq: %.0f Hz", _this->peakFreq);
            ImGui::Text("Peak Level: %.1f dB", _this->peakLevel);
        }

        ImGui::BeginTable(("peak_scanner_bottom_btn_table" + _this->name).c_str(), 2);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button(("<<##peak_scanner_back_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            std::lock_guard<std::mutex> lck(_this->scanMtx);
            _this->reverseLock = true;
            _this->receiving = false;
            _this->scanUp = false;
            // Force move to next frequency step
            _this->current -= _this->interval;
            if (_this->current < _this->startFreq) { _this->current = _this->stopFreq; }
            _this->forceMove = true;
        }
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button((">>##peak_scanner_forw_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            std::lock_guard<std::mutex> lck(_this->scanMtx);
            _this->reverseLock = true;
            _this->receiving = false;
            _this->scanUp = true;
            // Force move to next frequency step
            _this->current += _this->interval;
            if (_this->current > _this->stopFreq) { _this->current = _this->startFreq; }
            _this->forceMove = true;
        }
        ImGui::EndTable();

        if (!_this->running) {
            if (ImGui::Button("Start##peak_scanner_start", ImVec2(menuWidth, 0))) {
                _this->start();
            }
            ImGui::Text("Status: Idle");
        }
        else {
            if (ImGui::Button("Stop##peak_scanner_start", ImVec2(menuWidth, 0))) {
                _this->stop();
            }
            if (_this->receiving) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Receiving (Peak Mode)");
            }
            else if (_this->tuning) {
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "Status: Tuning");
            }
            else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: Scanning");
            }
        }
    }

    void start() {
        if (running) { return; }
        current = startFreq;
        running = true;
        workerThread = std::thread(&PeakScannerModule::worker, this);
    }

    void stop() {
        if (!running) { return; }
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void worker() {
        // 10Hz scan loop
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> lck(scanMtx);
                auto now = std::chrono::high_resolution_clock::now();

                // Enforce tuning
                if (gui::waterfall.selectedVFO.empty()) {
                    running = false;
                    return;
                }
                tuner::normalTuning(gui::waterfall.selectedVFO, current);

                // Check if we are waiting for a tune
                if (tuning) {
                    flog::warn("Tuning");
                    if ((std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTuneTime)).count() > tuningTime) {
                        tuning = false;
                    }
                    continue;
                }

                // Get FFT data
                int dataWidth = 0;
                float* data = gui::waterfall.acquireLatestFFT(dataWidth);
                if (!data) { continue; }

                // Get gather waterfall data
                double wfCenter = gui::waterfall.getViewOffset() + gui::waterfall.getCenterFrequency();
                double wfWidth = gui::waterfall.getViewBandwidth();
                double wfStart = wfCenter - (wfWidth / 2.0);
                double wfEnd = wfCenter + (wfWidth / 2.0);

                // Gather VFO data
                double vfoWidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);

                if (receiving) {
                    flog::warn("Receiving on peak");

                    // Check for forced move (button press)
                    if (forceMove) {
                        forceMove = false;
                        receiving = false;
                        // Force retune to new frequency
                        if (current - (vfoWidth / 2.0) < wfStart || current + (vfoWidth / 2.0) > wfEnd) {
                            lastTuneTime = now;
                            tuning = true;
                        }
                        gui::waterfall.releaseLatestFFT();
                        continue;
                    }

                    // Check if we're still on the peak - use fine-tuned peak detection
                    double actualPeak = findPeakInRange(data, current, vfoWidth, dataWidth, wfStart, wfWidth);
                    float maxLevel = getMaxLevel(data, actualPeak, vfoWidth, dataWidth, wfStart, wfWidth);

                    if (maxLevel >= level) {
                        lastSignalTime = now;
                        peakFreq = actualPeak; // Update peak frequency display
                        peakLevel = maxLevel;  // Update peak level display

                        // If peak moved significantly, retune to actual peak
                        if (std::abs(actualPeak - current) > (vfoWidth * 0.1)) { // 10% of VFO width
                            current = actualPeak;
                            if (current - (vfoWidth / 2.0) < wfStart || current + (vfoWidth / 2.0) > wfEnd) {
                                lastTuneTime = now;
                                tuning = true;
                            }
                        }
                    }
                    else if ((std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSignalTime)).count() > lingerTime) {
                        receiving = false;
                    }
                }
                else {
                    flog::warn("Seeking signal with peak detection");
                    double bottomLimit = current;
                    double topLimit = current;

                    // Search for a signal in scan direction
                    if (findSignalPeak(scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, vfoWidth, data, dataWidth)) {
                        gui::waterfall.releaseLatestFFT();
                        continue;
                    }

                    // Search for signal in the inverse scan direction if direction isn't enforced
                    if (!reverseLock) {
                        if (findSignalPeak(!scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, vfoWidth, data, dataWidth)) {
                            gui::waterfall.releaseLatestFFT();
                            continue;
                        }
                    }
                    else { reverseLock = false; }


                    // There is no signal on the visible spectrum, tune in scan direction and retry
                    if (scanUp) {
                        current = topLimit + interval;
                        if (current > stopFreq) { current = startFreq; }
                    }
                    else {
                        current = bottomLimit - interval;
                        if (current < startFreq) { current = stopFreq; }
                    }

                    // If the new current frequency is outside the visible bandwidth, wait for retune
                    if (current - (vfoWidth / 2.0) < wfStart || current + (vfoWidth / 2.0) > wfEnd) {
                        lastTuneTime = now;
                        tuning = true;
                    }
                }

                // Release FFT Data
                gui::waterfall.releaseLatestFFT();
            }
        }
    }

    // NEW: Peak-detecting version of findSignal
    bool findSignalPeak(bool scanDir, double& bottomLimit, double& topLimit, double wfStart, double wfEnd, double wfWidth, double vfoWidth, float* data, int dataWidth) {
        bool found = false;
        double freq = current;
        for (freq += scanDir ? interval : -interval;
             scanDir ? (freq <= stopFreq) : (freq >= startFreq);
             freq += scanDir ? interval : -interval) {

            // Check if signal is within bounds
            if (freq - (vfoWidth / 2.0) < wfStart) { break; }
            if (freq + (vfoWidth / 2.0) > wfEnd) { break; }

            if (freq < bottomLimit) { bottomLimit = freq; }
            if (freq > topLimit) { topLimit = freq; }

            // Check signal level first (like original)
            float maxLevel = getMaxLevel(data, freq, vfoWidth * (passbandRatio * 0.01f), dataWidth, wfStart, wfWidth);
            if (maxLevel >= level) {
                // PEAK DETECTION: Find actual peak frequency near this detection
                double actualPeak = findPeakInRange(data, freq, searchWidth, dataWidth, wfStart, wfWidth);

                // Verify the peak is still above threshold
                float peakLevel = getMaxLevel(data, actualPeak, vfoWidth * (passbandRatio * 0.01f), dataWidth, wfStart, wfWidth);
                if (peakLevel >= level) {
                    found = true;
                    receiving = true;
                    current = actualPeak; // Tune to actual peak, not threshold crossing point!
                    peakFreq = actualPeak;
                    peakLevel = peakLevel;
                    break;
                }
            }
        }
        return found;
    }

    // NEW: Find the actual peak frequency within a range, quantized to specified steps
    double findPeakInRange(float* data, double centerFreq, double searchRange, int dataWidth, double wfStart, double wfWidth) {
        double searchStart = centerFreq - (searchRange / 2.0);
        double searchEnd = centerFreq + (searchRange / 2.0);

        // Convert frequency range to FFT bin indices
        int startBin = std::clamp<int>((searchStart - wfStart) * (double)dataWidth / wfWidth, 0, dataWidth - 1);
        int endBin = std::clamp<int>((searchEnd - wfStart) * (double)dataWidth / wfWidth, 0, dataWidth - 1);

        // Instead of finding single peak bin, find peak within quantized grid
        double peakFreq = centerFreq;
        float maxPower = -INFINITY;

        // Step through the search range in specified increments (default 1 kHz)
        for (double testFreq = searchStart; testFreq <= searchEnd; testFreq += peakStep) {
            // Round to nearest step
            double quantizedFreq = round(testFreq / peakStep) * peakStep;

            // Make sure we're still in bounds
            if (quantizedFreq < searchStart || quantizedFreq > searchEnd) continue;

            // Get power level at this step
            float power = getMaxLevel(data, quantizedFreq, peakStep * 0.8, dataWidth, wfStart, wfWidth);

            if (power > maxPower) {
                maxPower = power;
                peakFreq = quantizedFreq;
            }
        }

        return peakFreq;
    }

    // Keep the original getMaxLevel function
    float getMaxLevel(float* data, double freq, double width, int dataWidth, double wfStart, double wfWidth) {
        double low = freq - (width / 2.0);
        double high = freq + (width / 2.0);
        int lowId = std::clamp<int>((low - wfStart) * (double)dataWidth / wfWidth, 0, dataWidth - 1);
        int highId = std::clamp<int>((high - wfStart) * (double)dataWidth / wfWidth, 0, dataWidth - 1);
        float max = -INFINITY;
        for (int i = lowId; i <= highId; i++) {
            if (data[i] > max) { max = data[i]; }
        }
        return max;
    }

    std::string name;
    bool enabled = true;

    bool running = false;
    double startFreq = 88000000.0;
    double stopFreq = 108000000.0;
    double interval = 100000.0;
    double searchWidth = 25000.0; // NEW: Width to search for peaks (25 kHz default)
    double peakStep = 1000.0;     // NEW: Step size for peak quantization (1 kHz default)
    double current = 88000000.0;
    double passbandRatio = 10.0;
    int tuningTime = 250;
    int lingerTime = 1000.0;
    float level = -50.0;
    bool receiving = true;
    bool tuning = false;
    bool scanUp = true;
    bool reverseLock = false;
    bool forceMove = false; // NEW: Force move to next frequency when button pressed

    // NEW: Peak detection results
    double peakFreq = 0.0;
    float peakLevel = -INFINITY;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastSignalTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastTuneTime;
    std::thread workerThread;
    std::mutex scanMtx;
};

MOD_EXPORT void _INIT_() {
    // Nothing here
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new PeakScannerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (PeakScannerModule*)instance;
}

MOD_EXPORT void _END_() {
    // Nothing here
}