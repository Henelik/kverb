#include "daisysp.h"
#include "kxmx_bluemchen/src/kxmx_bluemchen.h"
#include <string.h>

using namespace kxmx;
using namespace daisy;
using namespace daisysp;

Bluemchen bluemchen;

static ReverbSc   verb;
static DcBlock    blk[2];
Parameter         lpParam;
static float      drylevel, send;

Parameter knob1;
Parameter knob2;
Parameter cv1;
Parameter cv2;

/* Store for CV and Knob values*/
int cv_values[4] = {0,0,0,0};

/* Vars used for calculation */
float dryValue = 0.94f;
int dryCalc[2] {0,0};

float wetValue = 0.94f;
int wetCalc[2] {0,0};

float LPFValue = 10000.0f;
int filterCalc[2] {0,0};

float feedbackValue = 0.5f;
int feedbackCalc[2] {0,0};

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

// tracked whether the menu was already swapped with the current encoder press
bool menuSwapped = false;

/* variable for CV setting menu */
int cv_link[4] = {0,1,2,3};
std::string parameter_strings[5] {"dry", "wet", "LPF", "HPF", "feedback"};
std::string mapping_strings[5] {"bias", "Pot1", "Pot2", "CV 1", "CV 2"};
std::string sign_strings[3] {"-", "0", "+"};
std::string multiplier_strings[5] {"/4", "/2", "x1", "x2", "x4"};

float mapping_limits[5][3] = {
    // min, max, increment
    {-1, 1, 0.1}, // dry
    {-1, 1, 0.1}, // wet
    {-1, 1, 0.1}, // LPF
    {-1, 1, 0.1}, // HPF
    {-1, 1, 0.1}, // feedback
};

/* Value of the encoder */
int enc_val = 0;

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
            currentMenu = std::max(currentMenu - 1, 0);
            menuSwapped = true;
        }
    }

    if (bluemchen.encoder.FallingEdge()) {
        if (!menuSwapped) {
            currentMenu = std::min(currentMenu + 1, 2);
        }
        menuSwapped = false;
    }

    switch (currentMenu) {
        case 0:
            // main menu
            currentParam = std::min(std::max(int(currentParam+bluemchen.encoder.Increment()), 0), 4);
        case 1:
            // parameter menu
            currentMapping = std::min(std::max(int(currentMapping+bluemchen.encoder.Increment()), 0), 4);
        case 2:
            // mapping menu
            {}
    }
}

void calculateValues() {
    // reset values
    for(int j = 0; j < 2; j++) {
        dryCalc[j] = 0;
        wetCalc[j] = 0;
        filterCalc[j] = 0;
        feedbackCalc[j] = 0;
    }    

    for(int i = 0; i < 4; i++) {
        switch (cv_link[i]) {
        case 0:
            /* dry */
            dryCalc[0] += cv_values[i];
            dryCalc[1]++;
            break;
        
        case 1:
            /* wet */
            wetCalc[0] += cv_values[i];
            wetCalc[1]++;
            break;

        case 2:
            /* Filter */
            filterCalc[0] += cv_values[i];
            filterCalc[1]++;
            break;

        case 3:
            /* wet */
            feedbackCalc[0] += cv_values[i];
            feedbackCalc[1]++;
            break;
        
        default:
            break;
        }        
    }

    dryValue = ((dryCalc[0] / dryCalc[1]) * 0.00025f);
    wetValue = ((wetCalc[0] / wetCalc[1]) * 0.00025f);
    LPFValue = 10000.0f + ( (filterCalc[0] / filterCalc[1]) * 2.0f );
    feedbackValue = 0.5f + ( (feedbackCalc[0] / feedbackCalc[1]) / 10000.0f);
}

void processCVandKnobs() {
    cv_values[0] = knob1.Process();
    cv_values[1] = knob2.Process();  
    cv_values[2] = cv1.Process();
    cv_values[3] = cv2.Process();

    calculateValues();
}

void UpdateControls() {
    bluemchen.ProcessAllControls();

    processCVandKnobs();

    processEncoder();
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    float dryL, dryR, wetL, wetR, sendL, sendR;
   
    bluemchen.ProcessAnalogControls();
    
    for(size_t i = 0; i < size; i++)
    {
        // read some controls
        drylevel = dryValue;
        send     = wetValue;
        verb.SetFeedback(feedbackValue);
        verb.SetLpFreq(LPFValue);

        // Read Inputs (only stereo in are used)
        dryL = in[0][i];
        dryR = in[1][i];

        // Send Signal to Reverb
        sendL = dryL * send;
        sendR = dryR * send;
        verb.Process(sendL, sendR, &wetL, &wetR);

        // Dc Block
        wetL = blk[0].Process(wetL);
        wetR = blk[1].Process(wetR);

        // Out 1 and 2 are Mixed
        out[0][i] = (dryL * drylevel) + wetL;
        out[1][i] = (dryR * drylevel) + wetR;

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
    verb.SetFeedback(feedbackValue);
    verb.SetLpFreq(LPFValue);

    knob1.Init(bluemchen.controls[bluemchen.CTRL_1], 0.0f, 5000.0f, Parameter::LINEAR);
    knob2.Init(bluemchen.controls[bluemchen.CTRL_2], 0.0f, 5000.0f, Parameter::LINEAR);

    cv1.Init(bluemchen.controls[bluemchen.CTRL_3], -5000.0f, 5000.0f, Parameter::LINEAR);
    cv2.Init(bluemchen.controls[bluemchen.CTRL_4], -5000.0f, 5000.0f, Parameter::LINEAR);
    
    blk[0].Init(samplerate);
    blk[1].Init(samplerate);

    lpParam.Init(bluemchen.controls[3], 20, 20000, Parameter::LOGARITHMIC);

    bluemchen.StartAdc();
    bluemchen.StartAudio(AudioCallback);

    while (1)
    {
        UpdateControls();
        UpdateOled();
    }
}