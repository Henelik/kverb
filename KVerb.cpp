#include "daisysp.h"
#include "kxmx_bluemchen/src/kxmx_bluemchen.h"
#include <string.h>

using namespace kxmx;
using namespace daisy;
using namespace daisysp;

Bluemchen bluemchen;

static ReverbSc   verb;
static DcBlock    blk[2];
static Compressor sidechain[2]; // Stereo compressor for ducking wet signal
static Svf        hpf[2]; // Stereo high-pass filter before reverb

Parameter knob1;
Parameter knob2;
Parameter cv1;
Parameter cv2;

enum Params {
    DRY,
    WET,
    LPF,
    HPF,
    FEED,
    DUCK,
    PARAM_COUNT
};

enum MenuState {
    MENU_MAIN,
    MENU_PARAMETER,
    MENU_MAPPING,
    MENU_CONFIRMATION
};

enum ControlIndex {
    CTRL_KNOB1,
    CTRL_KNOB2,
    CTRL_CV1,
    CTRL_CV2,
    CTRL_COUNT
};

enum MappingType {
    MAP_BIAS,
    MAP_POT1,
    MAP_POT2,
    MAP_CV1,
    MAP_CV2,
    MAP_TYPE_COUNT
};

enum SignValue {
    SIGN_NEGATIVE,
    SIGN_OFF,
    SIGN_POSITIVE,
    SIGN_COUNT
};

enum MultiplierValue {
    MULT_DIV4,
    MULT_DIV2,
    MULT_X1,
    MULT_X2,
    MULT_X4,
    MULT_COUNT
};

enum MappingMenuOption {
    MAPOPT_SIGN,
    MAPOPT_MULTIPLIER,
    MAPOPT_COUNT
};

enum ConfirmOption {
    CONFIRM_NO,
    CONFIRM_YES,
    CONFIRM_COUNT
};

/* Store for CV and Knob values*/
float cv_values[CTRL_COUNT] = {0, 0, 0, 0};

// values for each parameter
float param_values[PARAM_COUNT] = {0, 0, 0, 0, 0, 0};

/* value for the menu that is showing on screen */
MenuState currentMenu = MENU_MAIN;

/* value for the currently selected parameter */
int currentParam = 0;

/* value for the currently selected mapping */
MappingType currentMapping = MAP_BIAS;

MappingMenuOption mappingMenuSelection = MAPOPT_SIGN;
bool editing = false;

// tracked whether the menu was already swapped with the current encoder press
bool menuSwapped = false;

/* confirmation menu selection */
ConfirmOption confirmSelection = CONFIRM_NO;

/* variables for CV settings menu */
std::string parameter_strings[PARAM_COUNT] {"dry", "wet", "LPF", "HPF", "feed", "duck"};
std::string mapping_strings[MAP_TYPE_COUNT] {"bias", "Pot1", "Pot2", "CV1", "CV2"};
std::string sign_strings[SIGN_COUNT] {"-", "0", "+"};
std::string multiplier_strings[MULT_COUNT] {"/4", "/2", "x1", "x2", "x4"};

float bias_limits[PARAM_COUNT][3] = {
    // min, max, increment
    {-1, 1, 0.1}, // dry
    {-1, 1, 0.1}, // wet
    {-1, 1, 0.1}, // LPF
    {-1, 1, 0.1}, // HPF
    {-1, 1, 0.1}, // feedback
    {-1, 1, 0.1}, // ducking
};

struct Settings {
    float biases[PARAM_COUNT];

    // Pot1 sign, Pot1 multiplier, Pot2 sign, Pot2 multiplier, CV1 sign, CV1 multiplier, CV2 sign, CV2 multiplier
    int mapping_indices[PARAM_COUNT][8];

    bool operator!=(const Settings& a) const {
        return !(a.biases==biases && a.mapping_indices==mapping_indices);
    };
};

Settings LocalSettings;

PersistentStorage<Settings> SavedSettings(bluemchen.seed.qspi);

bool trigger_save = false;

/* Value of the encoder */
int enc_val = 0;

// Store default settings for reset
Settings DefaultSettings;

void drawParamVisual(int index, int x, int y) {
    bluemchen.display.DrawRect(x, y, x+std::min(int(param_values[index]*10), 10), y+8, true, true);
}

void resetToDefaults() {
    // Copy default settings to local settings
    for (int p = 0; p < PARAM_COUNT; p++) {
        LocalSettings.biases[p] = DefaultSettings.biases[p];
        for (int m = 0; m < 8; m++) {
            LocalSettings.mapping_indices[p][m] = DefaultSettings.mapping_indices[p][m];
        }
    }
    trigger_save = true;
}

void MainMenu() {
    bluemchen.display.SetCursor(0, 0);
    std::string str = "  KVERB";
    char *cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    // draw up to 3 of the options, starting with the one before the current selection
    int firstOptionToDraw = std::min(std::max(currentParam - 1, 0), PARAM_COUNT - 2);
    for(int p = firstOptionToDraw; p < PARAM_COUNT + 1 && p-firstOptionToDraw < 3; p++){
        if (p == currentParam) {
            bluemchen.display.SetCursor(0, 8*(1+p-firstOptionToDraw));
            str = ">";
            bluemchen.display.WriteString(cstr, Font_6x8, true);
        }
        bluemchen.display.SetCursor(6, 8*(1+p-firstOptionToDraw));
        if (p < PARAM_COUNT) {
            str = parameter_strings[p];
            bluemchen.display.WriteString(cstr, Font_6x8, true);
            drawParamVisual(p, 36, 8*(1+p-firstOptionToDraw));
        } else {
            // INIT option
            str = "INIT";
            bluemchen.display.WriteString(cstr, Font_6x8, true);
        }
    }
}

void ParameterMenu() {
    bluemchen.display.SetCursor(0, 0);
    std::string str = parameter_strings[currentParam];
    char *cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    // draw up to 3 of the options, starting with the one before the current selection
    int firstOptionToDraw = std::min(std::max(currentMapping - 1, 0), MAP_TYPE_COUNT - 3);
    for(int p = firstOptionToDraw; p < MAP_TYPE_COUNT && p-firstOptionToDraw < 4; p++){
        if (p == currentMapping) {
            bluemchen.display.SetCursor(0, 8*(1+p-firstOptionToDraw));
            str = ">";
            bluemchen.display.WriteString(cstr, Font_6x8, true);
        }
        bluemchen.display.SetCursor(6, 8*(1+p-firstOptionToDraw));
        str = mapping_strings[p];
        bluemchen.display.WriteString(cstr, Font_6x8, true);
    }
}

void MappingMenu() {
    bluemchen.display.SetCursor(0, 0);
    std::string str = parameter_strings[currentParam];
    char *cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    bluemchen.display.SetCursor(6, 8);
    str = mapping_strings[currentMapping];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    if (currentMapping == MAP_BIAS) {
        // bias mapping
        bluemchen.display.SetCursor(6, 16);
        // Format bias to 2 decimal places
        char bias_str[8];
        snprintf(bias_str, sizeof(bias_str), "%.2f", LocalSettings.biases[currentParam]);
        str = std::string(bias_str);
        bluemchen.display.WriteString(cstr, Font_6x8, true);
    }
    else {
        // CV or pot mapping
        bluemchen.display.SetCursor(0, 16 + 8*mappingMenuSelection);
        str = ">";
        bluemchen.display.WriteString(cstr, Font_6x8, true);

        bool inverted = editing && mappingMenuSelection == MAPOPT_SIGN;
        if (inverted) {
            bluemchen.display.DrawRect(11, 16, 17, 22, true, true);
        }
        bluemchen.display.SetCursor(12, 16);
        str = sign_strings[LocalSettings.mapping_indices[currentParam][(currentMapping - MAP_POT1)*2]];
        bluemchen.display.WriteString(cstr, Font_6x8, !inverted);

        inverted = editing && mappingMenuSelection == MAPOPT_MULTIPLIER;
        if (inverted) {
            bluemchen.display.DrawRect(11, 24, 17, 32, true, true);
        }
        bluemchen.display.SetCursor(12, 24);
        str = multiplier_strings[LocalSettings.mapping_indices[currentParam][(currentMapping - MAP_POT1)*2+1]];
        bluemchen.display.WriteString(cstr, Font_6x8, !inverted);
    }
}

void ConfirmationMenu() {
    bluemchen.display.SetCursor(0, 0);
    std::string str = "RESET TO";
    char *cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);
    
    bluemchen.display.SetCursor(0, 8);
    str = "DEFAULTS?";
    bluemchen.display.WriteString(cstr, Font_6x8, true);
    
    // NO option
    if (confirmSelection == CONFIRM_NO) {
        bluemchen.display.SetCursor(0, 16);
        str = ">";
        bluemchen.display.WriteString(cstr, Font_6x8, true);
    }
    bluemchen.display.SetCursor(6, 16);
    str = "NO";
    bluemchen.display.WriteString(cstr, Font_6x8, true);
    
    // YES option
    if (confirmSelection == CONFIRM_YES) {
        bluemchen.display.SetCursor(0, 24);
        str = ">";
        bluemchen.display.WriteString(cstr, Font_6x8, true);
    }
    bluemchen.display.SetCursor(6, 24);
    str = "YES";
    bluemchen.display.WriteString(cstr, Font_6x8, true);
}

void UpdateOled() {
    bluemchen.display.Fill(false);

    switch (currentMenu) {
        case MENU_MAIN:
            MainMenu();
            break;
        case MENU_PARAMETER:
            ParameterMenu();
            break;
        case MENU_MAPPING:
            MappingMenu();
            break;
        case MENU_CONFIRMATION:
            ConfirmationMenu();
            break;
    }

    bluemchen.display.Update();
}

void processEncoder() {
    if (!menuSwapped && bluemchen.encoder.Pressed()) {
        if (bluemchen.encoder.TimeHeldMs() > 500) {
            // long press - go back
            if (currentMenu == MENU_CONFIRMATION) {
                // Reset confirmation selection and go back to main menu
                confirmSelection = CONFIRM_NO;
                currentMenu = MENU_MAIN;
            } else {
                currentMenu = static_cast<MenuState>(std::max(static_cast<int>(currentMenu) - 1, int(MENU_MAIN)));
            }
            menuSwapped = true;
        }
    }

    if (bluemchen.encoder.FallingEdge()) {
        if (!menuSwapped) {
            // short press
            if (currentMenu == MENU_MAPPING && currentMapping != MAP_BIAS) {
                if (editing) {
                    trigger_save = true;
                }
                editing = !editing;
            }
            else if (currentMenu == MENU_CONFIRMATION) {
                // Confirmation menu
                if (confirmSelection == CONFIRM_YES) {
                    // YES - reset to defaults
                    resetToDefaults();
                }
                // Go back to main menu
                confirmSelection = CONFIRM_NO;
                currentMenu = MENU_MAIN;
            }
            else if (currentMenu == MENU_MAIN && currentParam == PARAM_COUNT) {
                // Selected INIT from main menu
                currentMenu = MENU_CONFIRMATION;
                confirmSelection = CONFIRM_NO;
            }
            else {
                currentMenu = static_cast<MenuState>(std::min(static_cast<int>(currentMenu) + 1, int(MENU_MAPPING)));
            }
        }
        menuSwapped = false;
    }

    switch (currentMenu) {
        case MENU_MAIN:
            // main menu
            currentParam = std::min(std::max(int(currentParam+bluemchen.encoder.Increment()), 0), int(PARAM_COUNT));
            break;
        case MENU_PARAMETER:
            // parameter menu
            currentMapping = static_cast<MappingType>(std::min(std::max(int(currentMapping+bluemchen.encoder.Increment()), int(MAP_BIAS)), int(MAP_CV2)));
            break;
        case MENU_MAPPING:
            // mapping menu
            if (currentMapping == MAP_BIAS) {
                int increment = bluemchen.encoder.Increment();
                if (increment != 0) {
                    // Calculate exponential step size based on encoder speed
                    // Minimum step is 0.01, grows exponentially with speed
                    float abs_increment = fabsf(float(increment));
                    float step_size = 0.01f * powf(1.5f, abs_increment - 1.0f);
                    // Cap maximum step to 0.5 for safety
                    step_size = std::min(step_size, 0.5f);
                    
                    // Apply step with correct sign
                    float delta = step_size * (increment > 0 ? 1.0f : -1.0f);
                    
                    LocalSettings.biases[currentParam] = std::min(std::max(
                        LocalSettings.biases[currentParam] + delta,
                        bias_limits[currentParam][0]),
                        bias_limits[currentParam][1]);
                    trigger_save = true;
                }
            }
            else {
                if (editing) {
                    int increment = bluemchen.encoder.Increment();
                    if (increment != 0) {
                        LocalSettings.mapping_indices[currentParam][mappingMenuSelection+(currentMapping - MAP_POT1)*2] = std::min(std::max(
                            int(LocalSettings.mapping_indices[currentParam][mappingMenuSelection+(currentMapping - MAP_POT1)*2] + increment),
                            0),
                            mappingMenuSelection == MAPOPT_SIGN ? SIGN_COUNT - 1 : MULT_COUNT - 1);
                        trigger_save = true;
                    }
                }
                else {
                    mappingMenuSelection = static_cast<MappingMenuOption>(std::min(std::max(int(mappingMenuSelection + bluemchen.encoder.Increment()), int(MAPOPT_SIGN)), int(MAPOPT_MULTIPLIER)));
                }
            }
            break;
        case MENU_CONFIRMATION:
            // confirmation menu
            confirmSelection = static_cast<ConfirmOption>(std::min(std::max(int(confirmSelection + bluemchen.encoder.Increment()), int(CONFIRM_NO)), int(CONFIRM_YES)));
            break;
    }
}

float calculateMappingEffect(int controlIndex, int mappingIndex) {
    // Normalize the control value based on its range
    // Pots (CTRL_KNOB1, CTRL_KNOB2): range 0-1 → normalized to 0-1
    // CVs (CTRL_CV1, CTRL_CV2): range -1 to +1 → normalized to -1 to +1 (already bipolar)
    float normalizedValue = cv_values[controlIndex];
    
    // Apply multiplier to normalized value
    switch (mappingIndex) {
        case MULT_DIV4:
            return normalizedValue / 4.0f;
        case MULT_DIV2:
            return normalizedValue / 2.0f;
        case MULT_X1:
            return normalizedValue;
        case MULT_X2:
            return normalizedValue * 2.0f;
        case MULT_X4:
            return normalizedValue * 4.0f;
    }

    return 0.0f;
}

void calculateValues() {
    for (int p = 0; p < PARAM_COUNT; p++) {
        param_values[p] = LocalSettings.biases[p];

        for (int cv = 0; cv < CTRL_COUNT; cv ++) {
            switch (LocalSettings.mapping_indices[p][cv*2]) {
                case SIGN_NEGATIVE:
                    // negative mapping
                    param_values[p] -= calculateMappingEffect(cv, LocalSettings.mapping_indices[p][cv*2+1]);
                    break;
                case SIGN_OFF:
                    // no mapping
                    break;
                case SIGN_POSITIVE:
                    // positive mapping
                    param_values[p] += calculateMappingEffect(cv, LocalSettings.mapping_indices[p][cv*2+1]);
                    break;
            }
        }

        // clamp all values between 0 and 1
        param_values[p] = std::min(std::max(param_values[p], 0.0f), 1.0f);
    }
}

void UpdateControls() {
    bluemchen.ProcessAllControls();

    cv_values[CTRL_KNOB1] = knob1.Process();
    cv_values[CTRL_KNOB2] = knob2.Process();
    cv_values[CTRL_CV1] = cv1.Process();
    cv_values[CTRL_CV2] = cv2.Process();

    calculateValues();

    processEncoder();
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    float dryL, dryR, wetL, wetR, sendL, sendR;
   
    bluemchen.ProcessAnalogControls();
    
    // Update compressor parameters based on ducking amount
    float duck_amount = param_values[DUCK];
    if (duck_amount > 0.01f) {
        // Convert 0-1 range to useful compressor parameters
        // Higher duck_amount = more aggressive ducking
        float threshold = -30.0f + (duck_amount * 25.0f); // -30dB to -5dB
        float ratio = 1.0f + (duck_amount * 9.0f);         // 1:1 to 10:1
        float attack = 0.001f + ((1.0f - duck_amount) * 0.019f); // 1ms to 20ms
        float release = 0.05f + ((1.0f - duck_amount) * 0.45f);  // 50ms to 500ms
        
        sidechain[0].SetThreshold(threshold);
        sidechain[0].SetRatio(ratio);
        sidechain[0].SetAttack(attack);
        sidechain[0].SetRelease(release);
        
        sidechain[1].SetThreshold(threshold);
        sidechain[1].SetRatio(ratio);
        sidechain[1].SetAttack(attack);
        sidechain[1].SetRelease(release);
    }
    
    for(size_t i = 0; i < size; i++) {
        verb.SetFeedback(param_values[FEED]);

        // transform the LPF value from a 0-1 range to exponential hertz
        float lpf_freq = param_values[LPF] * 100.0f;
        lpf_freq = lpf_freq * lpf_freq * 2.0f;
        verb.SetLpFreq(lpf_freq);

        // transform the HPF value from a 0-1 range to exponential hertz
        float hpf_freq = param_values[HPF] * 100.0f;
        hpf_freq = hpf_freq * hpf_freq * 2.0f;
        hpf[0].SetFreq(hpf_freq);
        hpf[1].SetFreq(hpf_freq);

        // Read Inputs (only stereo in are used)
        dryL = in[0][i];
        dryR = in[1][i];

        // Send Signal to Reverb with high-pass filtering
        sendL = dryL * param_values[WET];
        sendR = dryR * param_values[WET];
        
        // Apply high-pass filter before reverb
        hpf[0].Process(sendL);
        sendL = hpf[0].High();
        hpf[1].Process(sendR);
        sendR = hpf[1].High();
        
        verb.Process(sendL, sendR, &wetL, &wetR);

        // Dc Block
        wetL = blk[0].Process(wetL);
        wetR = blk[1].Process(wetR);

        // Apply sidechain ducking if enabled
        if (duck_amount > 0.01f) {
            // Use dry signal as sidechain key to duck the wet signal
            wetL = sidechain[0].Process(wetL, dryL);
            wetR = sidechain[1].Process(wetR, dryR);
        }

        // Out 1 and 2 are Mixed
        out[0][i] = (dryL * param_values[DRY]) + wetL;
        out[1][i] = (dryR * param_values[DRY]) + wetR;

        // Out 3 and 4 are just wet
        out[2][i] = wetL;
        out[3][i] = wetR;
    }
    UpdateControls();
}

int main(void) {
    float samplerate;
    bluemchen.Init();
    samplerate = bluemchen.AudioSampleRate();

    verb.Init(samplerate);
    verb.SetFeedback(param_values[FEED]);
    verb.SetLpFreq(param_values[LPF]);

    DefaultSettings = {
        {1, 0, 1, 0.2, 0.5, 0}, //biases

        { // mapping_indices - all set to SIGN_OFF and MULT_X1
            {SIGN_NEGATIVE, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1}, // dry
            {SIGN_POSITIVE, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1}, // wet
            {SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1}, // LPF
            {SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1}, // HPF
            {SIGN_OFF, MULT_X1, SIGN_POSITIVE, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1}, // feedback
            {SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1, SIGN_OFF, MULT_X1}, // ducking
        }
    };

    SavedSettings.Init(DefaultSettings);

    // Load saved settings into LocalSettings
    LocalSettings = SavedSettings.GetSettings();

    // Initialize sidechain compressors
    sidechain[0].Init(samplerate);
    sidechain[1].Init(samplerate);
    sidechain[0].AutoMakeup(false); // No makeup gain for ducking
    sidechain[1].AutoMakeup(false);

    // Initialize high-pass filters
    hpf[0].Init(samplerate);
    hpf[1].Init(samplerate);
    hpf[0].SetRes(0.5f); // Set resonance to a neutral value
    hpf[1].SetRes(0.5f);

    knob1.Init(bluemchen.controls[bluemchen.CTRL_1], 0.0f, 1.0f, Parameter::LINEAR);
    knob2.Init(bluemchen.controls[bluemchen.CTRL_2], 0.0f, 1.0f, Parameter::LINEAR);

    cv1.Init(bluemchen.controls[bluemchen.CTRL_3], -1.0f, 1.0f, Parameter::LINEAR);
    cv2.Init(bluemchen.controls[bluemchen.CTRL_4], -1.0f, 1.0f, Parameter::LINEAR);
    
    blk[0].Init(samplerate);
    blk[1].Init(samplerate);

    bluemchen.StartAdc();
    bluemchen.StartAudio(AudioCallback);

    while (1) {
        UpdateOled();
		if(trigger_save) {
			trigger_save = false;

            Settings &SavedSettingsPointer = SavedSettings.GetSettings();

            // copy over the settings
            for (int p = 0; p < PARAM_COUNT; p++) {
                for (int m = 0; m < 8; m++) {
                    SavedSettingsPointer.mapping_indices[p][m] = LocalSettings.mapping_indices[p][m];
                }

                SavedSettingsPointer.biases[p] = LocalSettings.biases[p];
            }

            SavedSettings.Save(); // Writing locally stored settings to the external flash
		}
    }
}