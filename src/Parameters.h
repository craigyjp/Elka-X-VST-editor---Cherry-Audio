//Values below are just for initialising and will be changed when synth is initialised to current panel controls & EEPROM settings
byte midiChannel = MIDI_CHANNEL_OMNI;//(EEPROM)
byte midiOutCh = 1;//(EEPROM)
byte LEDintensity = 10;//(EEPROM)
byte oldLEDintensity;
int SLIDERintensity = 1;//(EEPROM)
int oldSLIDERintensity;
int learningDisplayNumber = 0;
int learningNote = 0;

static unsigned long lower_timer = 0;
static unsigned long upper_timer = 0;
static unsigned long chord_timerU = 0;
static unsigned long chord_timerL = 0;
static unsigned long learn_timer = 0;
const long interval = 250;

int readresdivider = 32;
int resolutionFrig = 5;
boolean recallPatchFlag = false;
boolean layerPatchFlag = false;
boolean learning = false;
boolean noteArrived = false;
int setCursorPos = 0;

int CC_ON = 127;
int CC_OFF = 127;

int MIDIThru = midi::Thru::Off;//(EEPROM)
String patchName = INITPATCHNAME;
boolean encCW = true;//This is to set the encoder to increment when turned CW - Settings Option
boolean updateParams = false;  //(EEPROM)
boolean sendNotes = false;  //(EEPROM)

// Synthex params

int masterVolume = 0;
int masterVolumemap = 0;
float masterVolumestr = 0;

int masterTune = 0;
int masterTunemap = 0;
float masterTunestr = 0;

int layerPanU = 0;
int layerPanL = 0;
int layerPanmap = 0;
float layerPanstr = 0;

int layerVolumeL = 0;
int layerVolumeU = 0;
int layerVolumemap = 0;
float layerVolumestr = 0;

int reverbLevelL = 0;
int reverbLevelU = 0;
int reverbLevelmap = 0;
float reverbLevelstr = 0;
int reverbDecayL = 0;
int reverbDecayU = 0;
int reverbDecaymap = 0;
float reverbDecaystr = 0;
int reverbEQL = 0;
int reverbEQU = 0;
int reverbEQmap = 0;
float reverbEQstr = 0;

int arpFrequencyL = 0;
int arpFrequencyU = 0;
int arpFrequencymapL = 0;
int arpFrequencymapU = 0;
float arpFrequencystr = 0;
String arpFrequencystring = "";

int ampVelocityL = 0;
int ampVelocityU = 0;
int ampVelocitymap = 0;
float ampVelocitystr = 0;
int ampReleaseL = 0;
int ampReleaseU = 0;
int ampReleasemap = 0;
float ampReleasestr = 0;
int ampSustainL = 0;
int ampSustainU = 0;
int ampSustainmap = 0;
float ampSustainstr = 0;
int ampDecayL = 0;
int ampDecayU = 0;
int ampDecaymap = 0;
float ampDecaystr = 0;
int ampAttackL = 0;
int ampAttackU = 0;
int ampAttackmap = 0;
float ampAttackstr = 0;

int osc2VolumeL = 0;
int osc2VolumeU = 0;
int osc2Volumemap = 0;
float osc2Volumestr = 0;
int osc2PWL = 0;
int osc2PWU = 0;
int osc2PWmap = 0;
float osc2PWstr = 0;

int osc1PWL = 0;
int osc1PWU = 0;
int osc1PWmap = 0;
float osc1PWstr = 0;
int osc1VolumeL = 0;
int osc1VolumeU = 0;
int osc1Volumemap = 0;
float osc1Volumestr = 0;

int filterCutoffL = 0;
int filterCutoffU = 0;
int filterCutoffmap = 0;
float filterCutoffstr = 0;
int filterKeyboardL = 0;
int filterKeyboardU = 0;
int filterKeyboardmap = 0;
float filterKeyboardstr = 0;
int filterResonanceL = 0;
int filterResonanceU = 0;
int filterResonancemap = 0;
float filterResonancestr = 0;
int filterEnvAmountL = 0;
int filterEnvAmountU = 0;
int filterEnvAmountmap = 0;
float filterEnvAmountstr = 0;
int filterAttackL = 0;
int filterAttackU = 0;
int filterAttackmap = 0;
float filterAttackstr = 0;
int filterDecayL = 0;
int filterDecayU = 0;
int filterDecaymap = 0;
float filterDecaystr = 0;
int filterSustainL = 0;
int filterSustainU = 0;
int filterSustainmap = 0;
float filterSustainstr = 0;
int filterReleaseL = 0;
int filterReleaseU = 0;
int filterReleasemap = 0;
float filterReleasestr = 0;
int filterVelocityL = 0;
int filterVelocityU = 0;
int filterVelocitymap = 0;
float filterVelocitystr = 0;

int echoEQL = 0;
int echoEQU = 0;
int echoEQmap = 0;
float echoEQstr = 0;
int echoLevelL = 0;
int echoLevelU = 0;
int echoLevelmap = 0;
float echoLevelstr = 0;
int echoFeedbackL = 0;
int echoFeedbackU = 0;
int echoFeedbackmap = 0;
float echoFeedbackstr = 0;
int echoSpreadL = 0;
int echoSpreadU = 0;
int echoSpreadmap = 0;
float echoSpreadstr = 0;
int echoTimeL = 0;
int echoTimeU = 0;
int echoTimemapL = 0;
int echoTimemapU = 0;
float echoTimestr = 0;
String echoTimestring = "";

int unisonDetuneL = 0;
int unisonDetuneU = 0;
int unisonDetunemap = 0;
float unisonDetunestr = 0;

int glideSpeedL = 0;
int glideSpeedU = 0;
int glideSpeedmap = 0;
float glideSpeedstr = 0;
int glideAmountL = 0;
int glideAmountU = 0;
int glideAmountmap = 0;
float glideAmountstr = 0;

int osc1TransposeL = 0;
int osc1TransposeU = 0;
int osc1Transposemap = 0;
float osc1Transposestr = 0;
int osc2TransposeL = 0;
int osc2TransposeU = 0;
int osc2Transposemap = 0;
float osc2Transposestr = 0;

int noiseLevelL = 0;
int noiseLevelU = 0;
int noiseLevelmap = 0;
float noiseLevelstr = 0;

int osc1TuneL = 0;
int osc1TuneU = 0;
int osc1Tunemap = 0;
float osc1Tunestr = 0;
int osc2TuneL = 0;
int osc2TuneU = 0;
int osc2Tunemap = 0;
float osc2Tunestr = 0;

int bendToFilter = 0;
int bendToFilterstr = 0;
int lfo2ToFilter = 0;
int lfo2ToFilterstr = 0;
int bendToOsc = 0;
int bendToOscstr = 0;
int lfo2ToOsc = 0;
int lfo2ToOscstr = 0;
int lfo2FreqAcc = 0;
int lfo2FreqAccstr = 0;
int lfo2InitFrequency = 0;
float lfo2InitFrequencystr = 0;
int lfo2InitFrequencymap = 0;
String lfo2InitFrequencystring = "";
int lfo2InitAmount = 0;
int lfo2InitAmountstr = 0;
int lfo2Destination = 0;
int lfo2Destinationmap = 0;
int lfo2Destinationstr = 0;

int seqAssign = 0;
int seqAssignstr = 0;
int seqAssignmap = 0;
int seqRate = 0;
int seqRatestr = 0;
int seqGate = 0;
int seqGatestr = 0;

int lfo1FrequencyL = 0;
int lfo1FrequencyU = 0;
int lfo1FrequencymapL = 0;
int lfo1FrequencymapU = 0;
String lfo1Frequencystring = "";
float lfo1Frequencystr = 0;
int lfo1DepthAL = 0;
int lfo1DepthAU = 0;
int lfo1DepthAmap = 0;
float lfo1DepthAstr = 0;
int lfo1DelayL = 0;
int lfo1DelayU = 0;
int lfo1Delaymap = 0;
float lfo1Delaystr = 0;
int lfo1DepthBL = 0;
int lfo1DepthBU = 0;
int lfo1DepthBmap = 0;
float lfo1DepthBstr = 0;

// Synthex Switches

int lowerSW = 0;
int upperSW = 1;
int utilitySW = 0;

int arpRange4SW = 0;
int arpRange4SWU = 0;
int arpRange4SWL = 0;
int arpRange3SW = 0;
int arpRange3SWU = 0;
int arpRange3SWL = 0;
int arpRange2SW = 0;
int arpRange2SWU = 0;
int arpRange2SWL = 0;
int arpRange1SW = 0;
int arpRange1SWU = 0;
int arpRange1SWL = 0;
int arpSyncSW = 0;
int arpSyncSWU = 0;
int arpSyncSWL = 0;
int arpHoldSW = 0;
int arpHoldSWU = 0;
int arpHoldSWL = 0;
int arpRandSW = 0;
int arpRandSWU = 0;
int arpRandSWL = 0;
int arpUpDownSW = 0;
int arpUpDownSWU = 0;
int arpUpDownSWL = 0;
int arpDownSW = 0;
int arpDownSWU = 0;
int arpDownSWL = 0;
int arpUpSW = 0;
int arpUpSWU = 0;
int arpUpSWL = 0;
int arpOffSW = 0;
int arpOffSWU = 0;
int arpOffSWL = 0;

int envInvSW = 0;
int envInvSWU = 0;
int envInvSWL = 0;
int filterHPSW = 0;
int filterHPSWU = 0;
int filterHPSWL = 0;
int filterBP2SW = 0;
int filterBP2SWU = 0;
int filterBP2SWL = 0;
int filterBP1SW = 0;
int filterBP1SWU = 0;
int filterBP1SWL = 0;
int filterLP2SW = 0;
int filterLP2SWU = 0;
int filterLP2SWL = 0;
int filterLP1SW = 0;
int filterLP1SWU = 0;
int filterLP1SWL = 0;

int revGLTCSW = 0;
int revGLTCSWU = 0;
int revGLTCSWL = 0;
int revHallSW = 0;
int revHallSWU = 0;
int revHallSWL = 0;
int revPlateSW = 0;
int revPlateSWU = 0;
int revPlateSWL = 0;
int revRoomSW = 0;
int revRoomSWU = 0;
int revRoomSWL = 0;
int revOffSW = 0;
int revOffSWU = 0;
int revOffSWL = 0;

int noisePinkSW = 0;
int noisePinkSWU = 0;
int noisePinkSWL = 0;
int noiseWhiteSW = 0;
int noiseWhiteSWU = 0;
int noiseWhiteSWL = 0;
int noiseOffSW = 0;
int noiseOffSWU = 0;
int noiseOffSWL = 0;

int osc1ringModSW = 0;
int osc1ringModSWU = 0;
int osc1ringModSWL = 0;
int osc2ringModSW = 0;
int osc2ringModSWU = 0;
int osc2ringModSWL = 0;

int osc1_osc2PWMSW = 0;
int osc1_osc2PWMSWU = 0;
int osc1_osc2PWMSWL = 0;
int osc1pulseSW = 0;
int osc1pulseSWU = 0;
int osc1pulseSWL = 0;
int osc1squareSW = 0;
int osc1squareSWU = 0;
int osc1squareSWL = 0;
int osc1sawSW = 0;
int osc1sawSWU = 0;
int osc1sawSWL = 0;
int osc1triangleSW = 0;
int osc1triangleSWU = 0;
int osc1triangleSWL = 0;

int osc1_1SW = 0;
int osc1_1SWU = 0;
int osc1_1SWL = 0;
int osc1_2SW = 0;
int osc1_2SWU = 0;
int osc1_2SWL = 0;
int osc1_4SW = 0;
int osc1_4SWU = 0;
int osc1_4SWL = 0;
int osc1_8SW = 0;
int osc1_8SWU = 0;
int osc1_8SWL = 0;
int osc1_16SW = 0;
int osc1_16SWU = 0;
int osc1_16SWL = 0;

int osc2_osc1PWMSW = 0;
int osc2_osc1PWMSWU = 0;
int osc2_osc1PWMSWL = 0;
int osc2pulseSW = 0;
int osc2pulseSWU = 0;
int osc2pulseSWL = 0;
int osc2squareSW = 0;
int osc2squareSWU = 0;
int osc2squareSWL = 0;
int osc2sawSW = 0;
int osc2sawSWU = 0;
int osc2sawSWL = 0;
int osc2triangleSW = 0;
int osc2triangleSWU = 0;
int osc2triangleSWL = 0;

int osc2_1SW = 0;
int osc2_1SWU = 0;
int osc2_1SWL = 0;
int osc2_2SW = 0;
int osc2_2SWU = 0;
int osc2_2SWL = 0;
int osc2_4SW = 0;
int osc2_4SWU = 0;
int osc2_4SWL = 0;
int osc2_8SW = 0;
int osc2_8SWU = 0;
int osc2_8SWL = 0;
int osc2_16SW = 0;
int osc2_16SWU = 0;
int osc2_16SWL = 0;

int echoSyncSW = 0;
int echoSyncSWU = 0;
int echoSyncSWL = 0;
int echoPingPongSW = 0;
int echoPingPongSWU = 0;
int echoPingPongSWL = 0;
int echoTapeSW = 0;
int echoTapeSWU = 0;
int echoTapeSWL = 0;
int echoSTDSW = 0;
int echoSTDSWU = 0;
int echoSTDSWL = 0;
int echoOffSW = 0;
int echoOffSWU = 0;
int echoOffSWL = 0;

int chorus3SW = 0;
int chorus3SWU = 0;
int chorus3SWL = 0;
int chorus2SW = 0;
int chorus2SWU = 0;
int chorus2SWL = 0;
int chorus1SW = 0;
int chorus1SWU = 0;
int chorus1SWL = 0;
int chorusOffSW = 0;
int chorusOffSWU = 0;
int chorusOffSWL = 0;

int osc1glideSW = 0;
int osc1glideSWU = 0;
int osc1glideSWL = 0;
int osc2glideSW = 0;
int osc2glideSWU = 0;
int osc2glideSWL = 0;

int portSW = 0;
int portSWU = 0;
int portSWL = 0;
int glideSW = 0;
int glideSWU = 0;
int glideSWL = 0;
int glideOffSW = 0;
int glideOffSWU = 0;
int glideOffSWL = 0;

int osc2SyncSW = 0;
int osc2SyncSWU = 0;
int osc2SyncSWL = 0;

int multiTriggerSW = 0;
int multiTriggerSWU = 0;
int multiTriggerSWL = 0;

int polySW = 0;
int polySWU = 0;
int polySWL = 0;
int singleMonoSW = 0;
int singleMonoSWU = 0;
int singleMonoSWL = 0;
int unisonMonoSW = 0;
int unisonMonoSWU = 0;
int unisonMonoSWL = 0;

int lfo1SyncSW = 0;
int lfo1SyncSWU = 0;
int lfo1SyncSWL = 0;
int lfo1modWheelSW = 0;
int lfo1modWheelSWU = 0;
int lfo1modWheelSWL = 0;
int lfo1randSW = 0;
int lfo1randSWU = 0;
int lfo1randSWL = 0;
int lfo1squareUniSW = 0;
int lfo1squareUniSWU = 0;
int lfo1squareUniSWL = 0;
int lfo1squareBipSW = 0;
int lfo1squareBipSWU = 0;
int lfo1squareBipSWL = 0;
int lfo1sawUpSW = 0;
int lfo1sawUpSWU = 0;
int lfo1sawUpSWL = 0;
int lfo1sawDnSW = 0;
int lfo1sawDnSWU = 0;
int lfo1sawDnSWL = 0;
int lfo1triangleSW = 0;
int lfo1triangleSWU = 0;
int lfo1triangleSWL = 0;
int lfo1resetSW = 0;
int lfo1resetSWU = 0;
int lfo1resetSWL = 0;
int lfo1osc1SW = 0;
int lfo1osc1SWU = 0;
int lfo1osc1SWL = 0;
int lfo1osc2SW = 0;
int lfo1osc2SWU = 0;
int lfo1osc2SWL = 0;
int lfo1pw1SW = 0;
int lfo1pw1SWU = 0;
int lfo1pw1SWL = 0;
int lfo1pw2SW = 0;
int lfo1pw2SWU = 0;
int lfo1pw2SWL = 0;
int lfo1filtSW = 0;
int lfo1filtSWU = 0;
int lfo1filtSWL = 0;
int lfo1ampSW = 0;
int lfo1ampSWU = 0;
int lfo1ampSWL = 0;
int lfo1seqRateSW = 0;
int lfo1seqRateSWU = 0;
int lfo1seqRateSWL = 0;

int lfo2SyncSW = 0;

int limiterSW = 0;
int singleSW = 0;
int doubleSW = 0;
int splitSW = 0;
int maxVoicesSW = 0;
int layerSoloSW = 0;
int layerSoloSWU = 0;
int layerSoloSWL = 0;
int chordMemorySW = 0;
int chordMemorySWL = 0;
int chordMemorySWU = 0;
boolean chordMemoryWaitL = false;
boolean chordMemoryWaitU = false;

// End Synthex Switches

int returnvalue = 0;

//Pick-up - Experimental feature
//Control will only start changing when the Knob/MIDI control reaches the current parameter value
//Prevents jumps in value when the patch parameter and control are different values
boolean pickUp = false;//settings option (EEPROM)
boolean pickUpActive = false;
#define TOLERANCE 2 //Gives a window of when pick-up occurs, this is due to the speed of control changing and Mux reading
