#ifndef WAKE_WORD_CONFIG_H
#define WAKE_WORD_CONFIG_H

#include <string>
#include <vector>

class WakeWordConfig {
public:
    enum class Mode {
        kPreset,
        kCustom,
    };

    struct Preset {
        const char* display_text;
        const char* model_name;
    };

    struct State {
        Mode mode = Mode::kPreset;
        std::string display_text;
        std::string preset_model;
        std::string command_pinyin;
        float threshold = 0.63f;
        bool fallback = false;
        std::string fallback_reason;
    };

    static WakeWordConfig& GetInstance();

    State GetState() const;
    bool SavePreset(const std::string& model_name, float threshold, std::string& error);
    bool SaveCustom(const std::string& display_text, const std::string& command_pinyin,
                    float threshold, std::string& error);
    void FallBackToDefault(const std::string& reason);

    static const std::vector<Preset>& GetPresets();
    static const char* DefaultModel();
    static const char* DefaultDisplayText();
    static const char* DefaultPinyin();
    static std::string ModeName(Mode mode);
    static float DefaultThreshold(Mode mode);

private:
    WakeWordConfig() = default;

    static bool IsKnownPreset(const std::string& model_name, const Preset** preset = nullptr);
    static bool ValidateChineseText(const std::string& text, size_t& character_count,
                                    std::string& error);
    static bool NormalizeAndValidatePinyin(const std::string& value, size_t expected_tokens,
                                           std::string& normalized, std::string& error);
    static bool ValidateThreshold(float threshold, Mode mode, std::string& error);
};

#endif
