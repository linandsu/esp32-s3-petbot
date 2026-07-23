#include "wake_word_config.h"

#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <esp_log.h>
#include <nvs.h>
#include <sstream>

namespace {
constexpr char kNamespace[] = "wake_word";
constexpr char kModeKey[] = "mode";
constexpr char kDisplayKey[] = "display_text";
constexpr char kPresetKey[] = "preset_model";
constexpr char kPinyinKey[] = "command_pinyin";
// NVS key names are limited to 15 characters.  The previous
// "threshold_x10000" name is 16 characters and made every save fail after
// the other fields had already been staged.
constexpr char kThresholdKey[] = "wake_threshold";
constexpr char kFallbackKey[] = "fallback";
constexpr char kFallbackReasonKey[] = "fallback_reason";
constexpr char kTag[] = "WakeWordConfig";

esp_err_t WriteState(const char* mode, const char* display_text, const char* preset_model,
                     const char* command_pinyin, float threshold, bool fallback,
                     const char* fallback_reason) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    if (err == ESP_OK) err = nvs_set_str(handle, kModeKey, mode);
    if (err == ESP_OK) err = nvs_set_str(handle, kDisplayKey, display_text);
    if (err == ESP_OK) err = nvs_set_str(handle, kPresetKey, preset_model);
    if (err == ESP_OK) err = nvs_set_str(handle, kPinyinKey, command_pinyin);
    if (err == ESP_OK) err = nvs_set_i32(handle, kThresholdKey,
                                         static_cast<int32_t>(std::lround(threshold * 10000)));
    if (err == ESP_OK) err = nvs_set_u8(handle, kFallbackKey, fallback ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, kFallbackReasonKey, fallback_reason);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool DecodeUtf8(const std::string& text, std::vector<uint32_t>& codepoints) {
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        uint32_t cp = 0;
        size_t extra = 0;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07;
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= text.size()) return false;
        for (size_t j = 1; j <= extra; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3F);
        }
        codepoints.push_back(cp);
        i += extra + 1;
    }
    return true;
}

bool IsChineseCodepoint(uint32_t cp) {
    return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2EBEF);
}
}  // namespace

WakeWordConfig& WakeWordConfig::GetInstance() {
    static WakeWordConfig instance;
    return instance;
}

const std::vector<WakeWordConfig::Preset>& WakeWordConfig::GetPresets() {
    static const std::vector<Preset> presets = {
        {"你好小智", "wn9_nihaoxiaozhi_tts"},
        {"小爱同学", "wn9_xiaoaitongxue"},
        {"嗨小欧", "wn9_hai1xiao3ou1_tts3"},
        {"你好小瑞", "wn9_ni3hao3xiao3rui4_tts3"},
    };
    return presets;
}

const char* WakeWordConfig::DefaultModel() { return GetPresets().front().model_name; }
const char* WakeWordConfig::DefaultDisplayText() { return GetPresets().front().display_text; }
const char* WakeWordConfig::DefaultPinyin() { return "ni hao xiao zhi"; }

float WakeWordConfig::DefaultThreshold(Mode mode) {
    (void)mode;
    return 0.63f;
}

std::string WakeWordConfig::ModeName(Mode mode) {
    return mode == Mode::kCustom ? "custom" : "preset";
}

WakeWordConfig::State WakeWordConfig::GetState() const {
    Settings settings(kNamespace);
    State state;
    state.mode = settings.GetString(kModeKey, "preset") == "custom" ? Mode::kCustom
                                                                      : Mode::kPreset;
    state.display_text = settings.GetString(kDisplayKey, DefaultDisplayText());
    state.preset_model = settings.GetString(kPresetKey, DefaultModel());
    state.command_pinyin = settings.GetString(kPinyinKey, DefaultPinyin());
    state.threshold = settings.GetInt(kThresholdKey,
                                      static_cast<int32_t>(DefaultThreshold(state.mode) * 10000)) /
                      10000.0f;
    state.fallback = settings.GetBool(kFallbackKey, false);
    state.fallback_reason = settings.GetString(kFallbackReasonKey, "");

    std::string threshold_error;
    if (!ValidateThreshold(state.threshold, state.mode, threshold_error)) {
        state.threshold = DefaultThreshold(state.mode);
    }

    if (state.mode == Mode::kPreset) {
        const Preset* preset = nullptr;
        if (!IsKnownPreset(state.preset_model, &preset)) {
            state.preset_model = DefaultModel();
            state.display_text = DefaultDisplayText();
        } else {
            state.display_text = preset->display_text;
        }
    }
    return state;
}

bool WakeWordConfig::SavePreset(const std::string& model_name, float threshold, std::string& error) {
    const Preset* preset = nullptr;
    if (!IsKnownPreset(model_name, &preset)) {
        error = "不支持的预设唤醒词";
        return false;
    }
    if (!ValidateThreshold(threshold, Mode::kPreset, error)) return false;
    const esp_err_t err = WriteState("preset", preset->display_text, preset->model_name,
                                     DefaultPinyin(), threshold, false, "");
    if (err != ESP_OK) {
        error = std::string("保存唤醒词失败：") + esp_err_to_name(err);
        return false;
    }
    return true;
}

bool WakeWordConfig::SaveCustom(const std::string& display_text,
                                const std::string& command_pinyin, float threshold,
                                std::string& error) {
    size_t character_count = 0;
    if (!ValidateChineseText(display_text, character_count, error)) return false;

    std::string normalized;
    if (!NormalizeAndValidatePinyin(command_pinyin, character_count, normalized, error)) {
        return false;
    }
    if (!ValidateThreshold(threshold, Mode::kCustom, error)) return false;

    const esp_err_t err = WriteState("custom", display_text.c_str(), DefaultModel(),
                                     normalized.c_str(), threshold, false, "");
    if (err != ESP_OK) {
        error = std::string("保存唤醒词失败：") + esp_err_to_name(err);
        return false;
    }
    return true;
}

void WakeWordConfig::FallBackToDefault(const std::string& reason) {
    const auto short_reason = reason.substr(0, 120);
    const esp_err_t err = WriteState("preset", DefaultDisplayText(), DefaultModel(),
                                     DefaultPinyin(), DefaultThreshold(Mode::kPreset), true,
                                     short_reason.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to persist wake-word fallback: %s", esp_err_to_name(err));
    }
}

bool WakeWordConfig::IsKnownPreset(const std::string& model_name, const Preset** preset) {
    for (const auto& item : GetPresets()) {
        if (model_name == item.model_name) {
            if (preset != nullptr) *preset = &item;
            return true;
        }
    }
    return false;
}

bool WakeWordConfig::ValidateChineseText(const std::string& text, size_t& character_count,
                                         std::string& error) {
    std::vector<uint32_t> codepoints;
    if (text.empty() || !DecodeUtf8(text, codepoints)) {
        error = "请输入有效的中文唤醒词";
        return false;
    }
    if (codepoints.size() < 2 || codepoints.size() > 8) {
        error = "唤醒词长度需要为 2 到 8 个汉字";
        return false;
    }
    if (!std::all_of(codepoints.begin(), codepoints.end(), IsChineseCodepoint)) {
        error = "唤醒词只能包含中文，不能包含数字、字母或符号";
        return false;
    }
    character_count = codepoints.size();
    return true;
}

bool WakeWordConfig::NormalizeAndValidatePinyin(const std::string& value,
                                                size_t expected_tokens,
                                                std::string& normalized,
                                                std::string& error) {
    std::istringstream input(value);
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) {
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (token.size() < 1 || token.size() > 6 ||
            !std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                return ch >= 'a' && ch <= 'z';
            })) {
            error = "识别拼音只能使用小写拼音，并用空格分隔";
            return false;
        }
        tokens.push_back(token);
    }
    if (tokens.size() != expected_tokens) {
        error = "识别拼音数量需要与汉字数量一致";
        return false;
    }
    normalized.clear();
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) normalized.push_back(' ');
        normalized += tokens[i];
    }
    return true;
}

bool WakeWordConfig::ValidateThreshold(float threshold, Mode mode, std::string& error) {
    (void)mode;
    if (!std::isfinite(threshold) || threshold < 0.40f || threshold > 0.95f) {
        error = "唤醒词阈值需在 0.40 到 0.95 之间";
        return false;
    }
    return true;
}
