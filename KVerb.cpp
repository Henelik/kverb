#include "daisysp.h"
#include "kxmx_bluemchen/src/kxmx_bluemchen.h"
#include <string.h>

using namespace kxmx;
using namespace daisy;
using namespace daisysp;

Bluemchen bluemchen;

static ReverbSc   verb;
static DcBlock    blk[2];

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
};

/* Store for CV and Knob values*/
float cv_values[4] = {0, 0, 0, 0};

// values for each parameter
float param_values[5] = {0, 0, 0, 0, 0};

/* value for the menu that is showing on screen
0 = main
1 = parameter
2 = mapping
*/
int currentMenu = 0;

/* value for the currently selected parameter
index of parameter_strings
*/
int currentParam = 0;

/* value for the currently selected mapping
index of mapping_strings
*/
int currentMapping = 0;

int mappingMenuSelection = 0;
bool editing = false;

// tracked whether the menu was already swapped with the current encoder press
bool menuSwapped = false;

/* variables for CV settings menu */
std::string parameter_strings[5] {"dry", "wet", "LPF", "HPF", "feed"};
std::string mapping_strings[5] {"bias", "Pot1", "Pot2", "CV1", "CV2"};
std::string sign_strings[3] {"-", "0", "+"};
std::string multiplier_strings[5] {"/4", "/2", "x1", "x2", "x4"};

float bias_limits[5][3] = {
    // min, max, increment
    {-1, 1, 0.1}, // dry
    {-1, 1, 0.1}, // wet
    {-1, 1, 0.1}, // LPF
    {-1, 1, 0.1}, // HPF
    {-1, 1, 0.1}, // feedback
};

struct Settings {
    float biases[5];

    // Pot1 sign, Pot1 multiplier, Pot2 sign, Pot2 multiplier, CV1 sign, CV1 multiplier, CV2 sign, CV2 multiplier
    int mapping_indices[5][8];

    bool operator!=(const Settings& a) const {
        return !(a.biases==biases && a.mapping_indices==mapping_indices);
    };
};

Settings LocalSettings;

PersistentStorage<Settings> SavedSettings(bluemchen.seed.qspi);

bool trigger_save = false;

/* Value of the encoder */
int enc_val = 0;

void drawParamVisual(int index, int x, int y) {
    bluemchen.display.DrawRect(x, y, x+std::min(int(param_values[index]*10), 10), y+8, true, true);
}

void MainMenu() {
    bluemchen.display.SetCursor(0, 0);
    std::string str = "  KVERB";
    char *cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    // draw up to 3 of the options, starting with the one before the current selection
    int firstOptionToDraw = std::min(std::max(currentParam - 1, 0), 2);
    for(int p = firstOptionToDraw; p < 5 && p-firstOptionToDraw < 4; p++){
        if (p == currentParam) {
            bluemchen.display.SetCursor(0, 8*(1+p-firstOptionToDraw));
            str = ">";
            bluemchen.display.WriteString(cstr, Font_6x8, true);
        }
        bluemchen.display.SetCursor(6, 8*(1+p-firstOptionToDraw));
        str = parameter_strings[p];
        bluemchen.display.WriteString(cstr, Font_6x8, true);
        drawParamVisual(p, 36, 8*(1+p-firstOptionToDraw));
    }
}

void ParameterMenu() {
    bluemchen.display.SetCursor(0, 0);
    std::string str = parameter_strings[currentParam];
    char *cstr = &str[0];
    bluemchen.display.WriteString(cstr, Font_6x8, true);

    // draw up to 3 of the options, starting with the one before the current selection
    int firstOptionToDraw = std::min(std::max(currentMapping - 1, 0), 2);
    for(int p = firstOptionToDraw; p < 5 && p-firstOptionToDraw < 4; p++){
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

    if (currentMapping == 0) {
        // bias mapping
        bluemchen.display.SetCursor(6, 16);
        str = std::to_string(LocalSettings.biases[currentParam]);
        bluemchen.display.WriteString(cstr, Font_6x8, true);
    }
    else {
        // CV or pot mapping
        bluemchen.display.SetCursor(0, 16 + 8*mappingMenuSelection);
        str = ">";
        bluemchen.display.WriteString(cstr, Font_6x8, true);

        bool inverted = editing && mappingMenuSelection == 0;
        if (inverted) {
            bluemchen.display.DrawRect(11, 16, 17, 22, true, true);
        }
        bluemchen.display.SetCursor(12, 16);
        str = sign_strings[LocalSettings.mapping_indices[currentParam][(currentMapping-1)*2]];
        bluemchen.display.WriteString(cstr, Font_6x8, !inverted);

        inverted = editing && mappingMenuSelection == 1;
        if (inverted) {
            bluemchen.display.DrawRect(11, 24, 17, 32, true, true);
        }
        bluemchen.display.SetCursor(12, 24);
        str = multiplier_strings[LocalSettings.mapping_indices[currentParam][(currentMapping-1)*2+1]];
        bluemchen.display.WriteString(cstr, Font_6x8, !inverted);
    }
}

void UpdateOled() {
    bluemchen.display.Fill(false);

    switch (currentMenu) {
        case 0:
            MainMenu();
            break;
        case 1:
            ParameterMenu();
            break;
        case 2:
            MappingMenu();
            break;
    }

    bluemchen.display.Update();
}

void processEncoder() {
    if (!menuSwapped && bluemchen.encoder.Pressed()) {
        if (bluemchen.encoder.TimeHeldMs() > 500) {
            // long press
            currentMenu = std::max(currentMenu - 1, 0);
            menuSwapped = true;
        }
    }

    if (bluemchen.encoder.FallingEdge()) {
        if (!menuSwapped) {
            // short press
            if (currentMenu == 2 && currentMapping != 0) {
                if (editing) {
                    trigger_save = true;
                }
                editing = !editing;
            }
            else {
                currentMenu = std::min(currentMenu + 1, 2);
            }
        }
        menuSwapped = false;
    }

    switch (currentMenu) {
        case 0:
            // main menu
            currentParam = std::min(std::max(int(currentParam+bluemchen.encoder.Increment()), 0), 4);
            break;
        case 1:
            // parameter menu
            currentMapping = std::min(std::max(int(currentMapping+bluemchen.encoder.Increment()), 0), 4);
            break;
        case 2:
            // mapping menu
            if (currentMapping == 0) {
                int increment = bluemchen.encoder.Increment();
                if (increment != 0) {
                    LocalSettings.biases[currentParam] = std::min(std::max(
                        LocalSettings.biases[currentParam] + bias_limits[currentParam][2] * increment,
                        bias_limits[currentParam][0]),
                        bias_limits[currentParam][1]);
                    trigger_save = true;
                }
            }
            else {
                if (editing) {
                    int increment = bluemchen.encoder.Increment();
                    if (increment != 0) {
                        LocalSettings.mapping_indices[currentParam][mappingMenuSelection+(currentMapping-1)*2] = std::min(std::max(
                            int(LocalSettings.mapping_indices[currentParam][mappingMenuSelection+(currentMapping-1)*2] + increment),
                            0),
                            mappingMenuSelection == 0 ? 2 : 4);
                        trigger_save = true;
                    }
                }
                else {
                    mappingMenuSelection = std::min(std::max(int(mappingMenuSelection + bluemchen.encoder.Increment()), 0), 1);
                }
            }
            break;
    }
}

float calculateMappingEffect(int controlIndex, int mappingIndex) {
    switch (mappingIndex) {
        // "/4", "/2", "x1", "x2", "x4"
        case 0:
            return cv_values[controlIndex] / 4.0f;
        case 1:
            return cv_values[controlIndex] / 2.0f;
        case 2:
            return cv_values[controlIndex];
        case 3:
            return cv_values[controlIndex] * 2.0f;
        case 4:
            return cv_values[controlIndex] * 4.0f;
    }

    return 0.0f;
}

void calculateValues() {
    for (int p = 0; p < 5; p++) {
        param_values[p] = LocalSettings.biases[p];

        for (int cv = 0; cv < 4; cv ++) {
            switch (LocalSettings.mapping_indices[p][cv*2]) {
                case 0:
                    // negative mapping
                    param_values[p] -= calculateMappingEffect(cv, LocalSettings.mapping_indices[p][cv*2+1]);
                    break;
                case 1:
                    // no mapping
                    break;
                case 2:
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

    cv_values[0] = knob1.Process();
    cv_values[1] = knob2.Process();  
    cv_values[2] = cv1.Process();
    cv_values[3] = cv2.Process();

    calculateValues();

    processEncoder();
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    float dryL, dryR, wetL, wetR, sendL, sendR;
   
    bluemchen.ProcessAnalogControls();
    
    for(size_t i = 0; i < size; i++) {
        verb.SetFeedback(param_values[FEED]);

        // transform the LPF value from a 0-1 range to exponential hertz
        float lpf_freq = param_values[LPF] * 100.0f;
        lpf_freq = lpf_freq * lpf_freq * 2.0f;
        verb.SetLpFreq(lpf_freq);

        // Read Inputs (only stereo in are used)
        dryL = in[0][i];
        dryR = in[1][i];

        // Send Signal to Reverb
        sendL = dryL * param_values[WET];
        sendR = dryR * param_values[WET];
        verb.Process(sendL, sendR, &wetL, &wetR);

        // Dc Block
        wetL = blk[0].Process(wetL);
        wetR = blk[1].Process(wetR);

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

    Settings DefaultSettings = {
        {0, 0, 0, 0, 0}, //biases

        { // mapping_indices
            {1, 2, 1, 2, 1, 2, 1, 2}, // dry
            {1, 2, 1, 2, 1, 2, 1, 2}, // wet
            {1, 2, 1, 2, 1, 2, 1, 2}, // LPF
            {1, 2, 1, 2, 1, 2, 1, 2}, // HPF
            {1, 2, 1, 2, 1, 2, 1, 2}, // feedback
        }
    };

    SavedSettings.Init(DefaultSettings);

    // Load saved settings into LocalSettings
    LocalSettings = SavedSettings.GetSettings();

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
            for (int p = 0; p < 5; p++) {
                for (int m = 0; m < 8; m++) {
                    SavedSettingsPointer.mapping_indices[p][m] = LocalSettings.mapping_indices[p][m];
                }

                SavedSettingsPointer.biases[p] = LocalSettings.biases[p];
            }

			SavedSettings.Save(); // Writing locally stored settings to the external flash
		}
    }
}