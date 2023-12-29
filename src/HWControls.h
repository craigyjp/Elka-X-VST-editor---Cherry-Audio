// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Bounce.h>
#include "TButton.h"
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

#define LED_MUX_0 38
#define LED_MUX_1 39

#define SEGMENT_CLK 36
#define SEGMENT_DIO 37

#define TRILL_CLK 20
#define TRILL_DIO 19

//Teensy 4.1 - Mux Pins
#define MUX_0 28
#define MUX_1 32
#define MUX_2 30
#define MUX_3 31

#define MUX1_S A0   // ADC1
#define MUX2_S A1   // ADC1
#define MUX3_S A2   // ADC1
#define MUX4_S A10  // ADC0

//Mux 1 Connections
#define MUX1_masterVolume 0
#define MUX1_masterTune 1
#define MUX1_layerPan 2
#define MUX1_layerVolume 3
#define MUX1_reverbLevel 4
#define MUX1_reverbDecay 5
#define MUX1_reverbEQ 6
#define MUX1_spare7 7
#define MUX1_arpFrequency 8
#define MUX1_ampVelocity 9
#define MUX1_filterVelocity 10
#define MUX1_spare11 11
#define MUX1_spare12 12
#define MUX1_spare13 13
#define MUX1_spare14 14
#define MUX1_spare15 15

//Mux 2 Connections
#define MUX2_ampRelease 0
#define MUX2_ampSustain 1
#define MUX2_ampDecay 2
#define MUX2_ampAttack 3
#define MUX2_filterKeyboard 4
#define MUX2_filterResonance 5
#define MUX2_osc2Volume 6
#define MUX2_osc2PW 7
#define MUX2_osc1PW 8
#define MUX2_osc1Volume 9
#define MUX2_filterCutoff 10
#define MUX2_filterEnvAmount 11
#define MUX2_filterAttack 12
#define MUX2_filterDecay 13
#define MUX2_filterSustain 14
#define MUX2_filterRelease 15

//Mux 3 Connections
#define MUX3_echoEQ 0
#define MUX3_echoLevel 1
#define MUX3_echoFeedback 2
#define MUX3_echoSpread 3
#define MUX3_echoTime 4
#define MUX3_lfo2UpperLower 5
#define MUX3_spare6 6
#define MUX3_spare7 7
#define MUX3_unisonDetune 8
#define MUX3_glideSpeed 9
#define MUX3_osc1Transpose 10
#define MUX3_osc2Transpose 11
#define MUX3_noiseLevel 12
#define MUX3_glideAmount 13
#define MUX3_osc1Tune 14
#define MUX3_osc2Tune 15

//Mux 4 Connections
#define MUX4_bendToFilter 0
#define MUX4_lfo2ToFilter 1
#define MUX4_bendToOsc 2
#define MUX4_lfo2ToOsc 3
#define MUX4_lfo2FreqAcc 4
#define MUX4_lfo2InitFrequency 5
#define MUX4_lfo2InitAmount 6
#define MUX4_spare7 7
#define MUX4_seqAssign 8
#define MUX4_seqRate 9
#define MUX4_seqGate 10
#define MUX4_lfo1Frequency 11
#define MUX4_lfo1DepthA 12
#define MUX4_lfo1Delay 13
#define MUX4_lfo1DepthB 14
#define MUX4_spare15 15


// // New Buttons
// 1
#define ARP_RANGE_4_SW 0
#define ARP_RANGE_3_SW 1
#define ARP_RANGE_2_SW 2
#define ARP_RANGE_1_SW 3
#define ARP_SYNC_SW 4
#define ARP_HOLD_SW 5
#define ARP_RAND_SW 6
#define ARP_UP_DOWN_SW 7

// 2
#define ARP_DOWN_SW 8
#define ARP_UP_SW 9
#define ARP_OFF_SW 10
#define LAYER_SOLO_SW 11
#define LIMITER_SW 12
#define LOWER_SW 13
#define UPPER_SW 14
#define UTILITY_SW 15

// 3
#define FILT_ENV_INV_SW 16
#define FILT_HP_SW 17
#define FILT_BP2_SW 18
#define REV_GLTC_SW 19
#define REV_HALL_SW 20
#define REV_PLT_SW 21
#define REV_ROOM_SW 22
#define REV_OFF_SW 23

// 4
#define FILT_BP1_SW 24
#define FILT_LP2_SW 25
#define FILT_LP1_SW 26
#define PINK_SW 27
#define WHITE_SW 28
#define NOISE_OFF_SW 29
#define ECHO_SYNC_SW 30
#define OSC2_RINGMOD_SW 31

// 5
#define OSC1_RINGMOD_SW 32
#define OSC1_OSC2_PWM_SW 33
#define OSC1_PULSE_SW 34
#define OSC1_SQUARE_SW 35
#define OSC1_SAW_SW 36
#define OSC1_TRIANGLE_SW 37
#define OSC2_OSC1_PWM_SW 38
#define OSC2_PULSE_SW 39

// 6
#define OSC1_1_SW 40
#define OSC1_2_SW 41
#define OSC1_4_SW 42
#define OSC1_8_SW 43
#define OSC1_16_SW 44
#define OSC2_SQUARE_SW 45
#define OSC2_SAW_SW 46
#define OSC2_TRIANGLE_SW 47

// 7
#define OSC2_1_SW 48
#define OSC2_2_SW 49
#define OSC2_4_SW 50
#define OSC2_8_SW 51
#define OSC2_16_SW 52
#define GLIDE_OSC2_SW 53
#define GLIDE_OSC1_SW 54
#define GLIDE_PORTA_SW 55

// 8
#define ECHO_PINGPONG_SW 56
#define ECHO_TAPE_SW 57
#define ECHO_STD_SW 58
#define ECHO_OFF_SW 59
#define CHORUS_3_SW 60
#define CHORUS_2_SW 61
#define CHORUS_1_SW 62
#define CHORUS_OFF_SW 63

// 9
#define GLIDE_GLIDE_SW 64
#define GLIDE_OFF_SW 65
#define SPLIT_SW 66
#define DOUBLE_SW 67
#define SINGLE_SW 68
#define MAX_VOICES_SW 69
#define LFO1_SYNC_SW 70
#define LFO1_WHEEL_SW 71

// 10
#define OSC2_SYNC_SW 72
#define MULTI_SW 73
#define CHORD_MEMORY_SW 74
#define UNI_MONO_SW 75
#define SINGLE_MONO_SW 76
#define POLY_SW 77
#define STEP_FW_SW 78
#define STEP_BW_SW 79

// 11
#define SEQ_LOOP_SW 80
#define SEQ_TRANS_SW 81
#define SEQ_KEY_SW 82
#define SEQ_PLAY_SW 83
#define SEQ_REST_SW 84
#define SEQ_UTIL_SW 85
#define LFO2_SYNC_SW 86
#define SEQ_STOP_SW 87

// 12
#define SEQ_ENABLE_1_SW 88
#define SEQ_ENABLE_2_SW 89
#define SEQ_ENABLE_3_SW 90
#define SEQ_ENABLE_4_SW 91
#define SEQ_ADD_STEP_SW 92
#define SEQ_DEL_STEP_SW 93
#define SEQ_INS_STEP_SW 94
#define EQ_REC_EDIT_SW 95

// 13
#define SEQ_SYNC_SW 96
#define LFO1_RANDOM_SW 97
#define LFO1_SQ_UNIPOLAR_SW 98
#define LFO1_SQ_BIPOLAR_SW 99
#define LFO1_SAW_UP_SW 100
#define LFO1_SAW_DOWN_SW 101
#define LFO1_TRIANGLE_SW 102
#define LFO1_RESET_SW 103

// 14
#define LFO1_SEQ_RATE_SW 104
#define LFO1_AMP_SW 105
#define LFO1_FILT_SW 106
#define LFO1_PW2_SW 107
#define LFO1_PW1_SW 108
#define LFO1_OSC2_SW 109
#define LFO1_OSC1_SW 110
// spare 111

// New LEDs

// 1
#define ARP_RANGE_4_LED 0
#define ARP_RANGE_3_LED 1
#define ARP_RANGE_2_LED 2
#define ARP_RANGE_1_LED 3
#define ARP_SYNC_LED 4
#define ARP_HOLD_LED 5
#define ARP_RAND_LED 6
#define ARP_UP_DOWN_LED 7

// 2
#define ARP_DOWN_LED 8
#define ARP_UP_LED 9
// spare 10
#define LAYER_SOLO_LED 11
#define LIMITER_LED 12
#define LOWER_LED 13
#define UPPER_LED 14
// spare 15

// 3
#define ENV_INV_LED 16
#define FILT_HP_LED 17
#define FILT_BP2_LED 18
#define REV_GLTC_LED 19
#define REV_HALL_LED 20
#define REV_PLT_LED 21
#define REV_ROOM_LED 22
// spare 23

// 4
#define FILT_BP1_LED 24
#define FILT_LP2_LED 25
#define FILT_LP1_LED 26
#define PINK_LED 27
#define WHITE_LED 28
// spare 29
#define ECHO_SYNC_LED 30
#define OSC2_RINGMOD_LED 31

// 5
#define OSC1_RINGMOD_LED 32
#define OSC1_OSC2_PWM_LED 33
#define OSC1_PULSE_LED 34
#define OSC1_SQUARE_LED 35
#define OSC1_SAW_LED 36
#define OSC1_TRIANGLE_LED 37
#define OSC2_OSC1_PWM_LED 38
#define OSC2_PULSE_LED 39

// 6
#define OSC1_1_LED 40
#define OSC1_2_LED 41
#define OSC1_4_LED 42
#define OSC1_8_LED 43
#define OSC1_16_LED 44
#define OSC2_SQUARE_LED 45
#define OSC2_SAW_LED 46
#define OSC2_TRIANGLE_LED 47

// 7
#define OSC2_1_LED 48
#define OSC2_2_LED 49
#define OSC2_4_LED 50
#define OSC2_8_LED 51
#define OSC2_16_LED 52
#define GLIDE_OSC2_LED 53
#define GLIDE_OSC1_LED 54
#define GLIDE_PORTA_LED 55

// 8
#define ECHO_PINGPONG_LED 56
#define ECHO_TAPE_LED 57
#define ECHO_STD_LED 58
// spare 59
#define CHORUS_3_LED 60
#define CHORUS_2_LED 61
#define CHORUS_1_LED 62
// spare 63


// 9
#define GLIDE_GLIDE_LED 64
// spare 65
#define SPLIT_LED 66
#define DOUBLE_LED 67
#define SINGLE_LED 68
// spare 69
#define LFO1_SYNC_LED 70
#define LFO1_WHEEL_LED 71

// 10
#define OSC2_SYNC_LED 72
#define MULTI_LED 73
#define CHORD_MEMORY_LED 74
#define UNI_MONO_LED 75
#define SINGLE_MONO_LED 76
#define POLY_LED 77
#define LFO1_SEQ_RATE_LED 78
#define LFO1_AMP_LED 79

// 11
#define SEQ_LOOP_LED 80
#define SEQ_TRANS_LED 81
#define SEQ_KEY_LED 82
#define SEQ_PLAY_LED 83
#define LFO1_PW2_LED 84
#define LFO1_FILT_LED 85
#define LFO2_SYNC_LED 86
#define SEQ_STOP_LED 87

// 12
#define SEQ_ENABLE_1_LED 88
#define SEQ_ENABLE_2_LED 89
#define SEQ_ENABLE_3_LED 90
#define SEQ_ENABLE_4_LED 91
#define LFO1_OSC1_LED 92
#define LFO1_OSC2_LED 93
#define LFO1_PW1_LED 94
#define EQ_REC_EDIT_LED 95

// 13
#define SEQ_SYNC_LED 96
#define LFO1_RANDOM_LED 97
#define LFO1_SQ_UNIPOLAR_LED 98
#define LFO1_SQ_BIPOLAR_LED 99
#define LFO1_SAW_UP_LED 100
#define LFO1_SAW_DOWN_LED 101
#define LFO1_TRIANGLE_LED 102
#define LFO1_RESET_LED 103

// Green LEDS

#define GREEN_ARP_RANGE_4_LED 0
#define GREEN_ARP_RANGE_3_LED 1
#define GREEN_ARP_RANGE_2_LED 2
#define GREEN_ARP_RANGE_1_LED 3
#define GREEN_ARP_SYNC_LED 4
#define GREEN_ARP_HOLD_LED 5
#define GREEN_ARP_RAND_LED 6
#define GREEN_ARP_UP_DOWN_LED 7

#define GREEN_ARP_DOWN_LED 8
#define GREEN_ARP_UP_LED 9
#define GREEN_LAYER_SOLO_LED 10
#define GREEN_ENV_INV_LED 11
#define GREEN_FILT_HP_LED 12
#define GREEN_FILT_BP2_LED 13
#define GREEN_REV_GLTC_LED 14
#define GREEN_REV_HALL_LED 15

#define GREEN_REV_PLT_LED 16
#define GREEN_REV_ROOM_LED 17
#define GREEN_FILT_BP1_LED 18
#define GREEN_FILT_LP2_LED 19
#define GREEN_FILT_LP1_LED 20
#define GREEN_PINK_LED 21
#define GREEN_WHITE_LED 22
#define GREEN_ECHO_SYNC_LED 23

#define GREEN_OSC2_RINGMOD_LED 24
#define GREEN_OSC1_RINGMOD_LED 25
#define GREEN_OSC1_OSC2_PWM_LED 26
#define GREEN_OSC1_PULSE_LED 27
#define GREEN_OSC1_SQUARE_LED 28
#define GREEN_OSC1_SAW_LED 29
#define GREEN_OSC1_TRIANGLE_LED 30
#define GREEN_OSC2_OSC1_PWM_LED 31

#define GREEN_OSC2_PULSE_LED 32
#define GREEN_OSC1_1_LED 33
#define GREEN_OSC1_2_LED 34
#define GREEN_OSC1_4_LED 35
#define GREEN_OSC1_8_LED 36
#define GREEN_OSC1_16_LED 37
#define GREEN_OSC2_SQUARE_LED 38
#define GREEN_OSC2_SAW_LED 39

#define GREEN_OSC2_TRIANGLE_LED 40
#define GREEN_OSC2_1_LED 41
#define GREEN_OSC2_2_LED 42
#define GREEN_OSC2_4_LED 43
#define GREEN_OSC2_8_LED 44
#define GREEN_OSC2_16_LED 45
#define GREEN_GLIDE_OSC2_LED 46
#define GREEN_GLIDE_OSC1_LED 47

#define GREEN_GLIDE_PORTA_LED 48
#define GREEN_ECHO_PINGPONG_LED 49
#define GREEN_ECHO_TAPE_LED 50
#define GREEN_ECHO_STD_LED 51
#define GREEN_CHORUS_3_LED 52
#define GREEN_CHORUS_2_LED 53
#define GREEN_CHORUS_1_LED 54
#define GREEN_GLIDE_GLIDE_LED 55

#define GREEN_LFO1_SYNC_LED 56
#define GREEN_LFO1_WHEEL_LED 57
#define GREEN_OSC2_SYNC_LED 58
#define GREEN_MULTI_LED 59
#define GREEN_CHORD_MEMORY_LED 60
#define GREEN_UNI_MONO_LED 61
#define GREEN_SINGLE_MONO_LED 62
#define GREEN_POLY_LED 63

#define GREEN_LFO1_SEQ_RATE_LED 64
#define GREEN_LFO1_AMP_LED 65
#define GREEN_LFO1_FILT_LED 66
#define GREEN_LFO1_PW2_LED 67
#define GREEN_LFO1_PW1_LED 68
#define GREEN_LFO1_OSC2_LED 69
#define GREEN_LFO1_OSC1_LED 70
#define GREEN_LFO1_RANDOM_LED 71

#define GREEN_LFO1_SQ_UNIPOLAR_LED 72
#define GREEN_LFO1_SQ_BIPOLAR_LED 73
#define GREEN_LFO1_SAW_UP_LED 74
#define GREEN_LFO1_SAW_DOWN_LED 75
#define GREEN_LFO1_TRIANGLE_LED 76
#define GREEN_LFO1_RESET_LED 77

//Teensy 4.1 Pins

#define RECALL_SW 17
#define SAVE_SW 41
#define SETTINGS_SW 12
#define BACK_SW 10

#define ENCODER_PINA 5
#define ENCODER_PINB 4

#define MUXCHANNELS 16
#define QUANTISE_FACTOR 31

#define DEBOUNCE 30

static byte muxInput = 0;

static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux2ValuesPrev[MUXCHANNELS] = {};
static int mux3ValuesPrev[MUXCHANNELS] = {};
static int mux4ValuesPrev[MUXCHANNELS] = {};

static int mux1Read = 0;
static int mux2Read = 0;
static int mux3Read = 0;
static int mux4Read = 0;

static long encPrevious = 0;

//These are pushbuttons and require debouncing

TButton saveButton{ SAVE_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton settingsButton{ SETTINGS_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton backButton{ BACK_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton recallButton{ RECALL_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };  //On encoder

Encoder encoder(ENCODER_PINB, ENCODER_PINA);  //This often needs the pins swapping depending on the encoder

void setupHardware() {
  //Volume Pot is on ADC0
  adc->adc0->setAveraging(32);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(12);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(32);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(12);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  //Mux address pins

  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  digitalWrite(MUX_0, LOW);
  digitalWrite(MUX_1, LOW);
  digitalWrite(MUX_2, LOW);
  digitalWrite(MUX_3, LOW);

  pinMode(LED_MUX_0, OUTPUT);
  pinMode(LED_MUX_1, OUTPUT);

  digitalWrite(LED_MUX_0, LOW);
  digitalWrite(LED_MUX_1, LOW);

  //Mux ADC
  pinMode(MUX1_S, INPUT_DISABLE);
  pinMode(MUX2_S, INPUT_DISABLE);
  pinMode(MUX3_S, INPUT_DISABLE);
  pinMode(MUX4_S, INPUT_DISABLE);

  //Switches
  pinMode(RECALL_SW, INPUT_PULLUP);  //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);
}

