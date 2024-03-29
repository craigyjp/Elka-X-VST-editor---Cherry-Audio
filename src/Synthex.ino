/*
  Synthex Editor - Firmware Rev 1.0

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html
    ElectroTechnique for general method of menus and updates.

  Arduino IDE
  Tools Settings:
  Board: "Teensy4,1"
  USB Type: "Serial + MIDI"
  CPU Speed: "600"
  Optimize: "Fastest"

  Additional libraries:
    Agileware CircularBuffer available in Arduino libraries manager
    Replacement files are in the Modified Libraries folder and need to be placed in the teensy Audio folder.
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include "MidiCC.h"
#include "ButtonsPots.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"
#include <RoxMux.h>
#include <SevenSegmentTM1637.h>
#include <SevenSegmentExtended.h>

#define PARAMETER 0      //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1         //Patches list
#define SAVE 2           //Save patch page
#define REINITIALISE 3   // Reinitialise message
#define PATCH 4          // Show current patch bypassing PARAMETER
#define PATCHNAMING 5    // Patch naming page
#define DELETE 6         //Delete patch page
#define DELETEMSG 7      //Delete patch message page
#define SETTINGS 8       //Settings page
#define SETTINGSVALUE 9  //Settings page

unsigned int state = PARAMETER;

#include "ST7735Display.h"

boolean cardStatus = false;

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial6, MIDI6);

#define OCTO_TOTAL 14
#define BTN_DEBOUNCE 50
RoxOctoswitch<OCTO_TOTAL, BTN_DEBOUNCE> octoswitch;

// pins for 74HC165
#define PIN_DATA 34  // pin 9 on 74HC165 (DATA)
#define PIN_LOAD 35  // pin 1 on 74HC165 (LOAD)
#define PIN_CLK 33   // pin 2 on 74HC165 (CLK))

#define SR_TOTAL 13
Rox74HC595<SR_TOTAL> sr;

#define GREEN_TOTAL 10
Rox74HC595<GREEN_TOTAL> green;

// pins for 74HC595
#define LED_DATA 21   // pin 14 on 74HC595 (DATA)
#define LED_LATCH 23  // pin 12 on 74HC595 (LATCH)
#define LED_CLK 22    // pin 11 on 74HC595 (CLK)
#define LED_PWM -1    // pin 13 on 74HC595

// pins for 74HC595
#define GREEN_LED_DATA 6   // pin 14 on 74HC595 (DATA)
#define GREEN_LED_LATCH 8  // pin 12 on 74HC595 (LATCH)
#define GREEN_LED_CLK 7    // pin 11 on 74HC595 (CLK)
#define GREEN_LED_PWM -1   // pin 13 on 74HC595

//RoxLed UPPER_LED;

byte ccType = 0;  //(EEPROM)

#include "Settings.h"

int count = 0;                 //For MIDI Clk Sync
int patchNo = 1;               //Current patch no
int voiceToReturn = -1;        //Initialise
long earliestTime = millis();  //For voice allocation - initialise to now

// LED displays
SevenSegmentExtended trilldisplay(TRILL_CLK, TRILL_DIO);
SevenSegmentExtended display0(SEGMENT_CLK, SEGMENT_DIO);
SevenSegmentExtended display1(SEGMENT_CLK, SEGMENT_DIO);
SevenSegmentExtended display2(SEGMENT_CLK, SEGMENT_DIO);

void setup() {
  SPI.begin();
  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);
  octoswitch.setIgnoreAfterHold(UTILITY_SW, true);
  octoswitch.setIgnoreAfterHold(MAX_VOICES_SW, true);
  sr.begin(LED_DATA, LED_LATCH, LED_CLK, LED_PWM);
  green.begin(GREEN_LED_DATA, GREEN_LED_LATCH, GREEN_LED_CLK, GREEN_LED_PWM);
  setupDisplay();
  setUpSettings();
  setupHardware();
  //UPPER_LED.begin();
  //UPPER_LED.setMode(ROX_DEFAULT);
  //setUpLEDS();

  // Initialize the array with some values (optional)
  for (int i = 0; i < 128; i++) {
    for (int j = 0; j < 4; j++) {
      // Assign some values, for example, i * j
      seqArray[i][j] = -1;
    }
  }

  LEDintensity = getLEDintensity();
  LEDintensity = LEDintensity * 10;
  oldLEDintensity = LEDintensity;

  trilldisplay.begin();                     // initializes the display
  trilldisplay.setBacklight(LEDintensity);  // set the brightness to 100 %
  trilldisplay.print("   8");               // display INIT on the display
  delay(10);

  setLEDDisplay0();
  display0.begin();          // initializes the display
  display0.setBacklight(0);  // set the brightness to intensity
  display0.print(" 127");    // display INIT on the display
  delay(10);

  setLEDDisplay1();
  display1.begin();          // initializes the display
  display1.setBacklight(0);  // set the brightness to intensity
  display1.print("   0");    // display INIT on the display
  delay(10);

  setLEDDisplay2();
  display2.begin();                     // initializes the display
  display2.setBacklight(LEDintensity);  // set the brightness to intensity
  display2.print(" 127");               // display INIT on the display
  delay(10);

  setLEDDisplay0();

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus) {
    Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0) {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
  } else {
    Serial.println("SD card is not connected or unusable");
    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable");
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //Read CC type from EEPROM
  ccType = getCCType();

  //Read UpdateParams type from EEPROM
  updateParams = getUpdateParams();

  //Read SendNotes type from EEPROM
  sendNotes = getSendNotes();

  //USB HOST MIDI Class Compliant
  delay(400);  //Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(myConvertControlChange);
  midi1.setHandleProgramChange(myProgramChange);
  midi1.setHandleNoteOff(myNoteOff);
  midi1.setHandleNoteOn(myNoteOn);
  midi1.setHandlePitchChange(myPitchBend);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myConvertControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleControlChange(myConvertControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);


  MIDI6.begin();
  Serial.println("MIDI In DIN Listening");

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  //Read MIDI Out Channel from EEPROM
  midiOutCh = getMIDIOutCh();

  sr.writePin(UPPER_LED, HIGH);
  sr.writePin(SEQ_STOP_LED, HIGH);

  // Blank the split display
  setLEDDisplay2();
  display2.setBacklight(0);

  recallPatch(patchNo);  //Load first patch
}

void myNoteOn(byte channel, byte note, byte velocity) {
  // if (learning) {
  //   learningNote = note;
  //   noteArrived = true;
  // }

  if (!learning) {
    MIDI.sendNoteOn(note, velocity, channel);
    if (sendNotes) {
      usbMIDI.sendNoteOn(note, velocity, channel);
    }
  }

  if (chordMemoryWaitL) {
    chordMemoryWaitL = false;
    sr.writePin(CHORD_MEMORY_LED, LOW);
    green.writePin(GREEN_CHORD_MEMORY_LED, HIGH);
  }

  if (chordMemoryWaitU) {
    chordMemoryWaitU = false;
    sr.writePin(CHORD_MEMORY_LED, HIGH);
    green.writePin(GREEN_CHORD_MEMORY_LED, LOW);
  }
}

void myNoteOff(byte channel, byte note, byte velocity) {
  if (!learning) {
    MIDI.sendNoteOff(note, velocity, channel);
    if (sendNotes) {
      usbMIDI.sendNoteOff(note, velocity, channel);
    }
  }
}

void convertIncomingNote() {

  if (learning && noteArrived) {
    splitNote = learningNote;
    learning = false;
    noteArrived = false;
    sr.writePin(SPLIT_LED, HIGH);
    setLEDDisplay2();
    display2.setBacklight(LEDintensity);
    displayLEDNumber(2, splitNote);

    MIDI.sendNoteOn(splitNote, 127, 1);
    MIDI.sendNoteOff(splitNote, 0, 1);
  }
}

void myConvertControlChange(byte channel, byte number, byte value) {
  int newvalue = value;
  myControlChange(channel, number, newvalue);
}

void myPitchBend(byte channel, int bend) {
  MIDI.sendPitchBend(bend, channel);
  if (sendNotes) {
    usbMIDI.sendPitchBend(bend, channel);
  }
}

void myAfterTouch(byte channel, byte pressure) {
  MIDI.sendAfterTouch(pressure, channel);
  if (sendNotes) {
    usbMIDI.sendAfterTouch(pressure, channel);
  }
}

void allNotesOff() {
}

void updatemasterTune() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Tune", String(masterTunestr) + " Semi");
  }
  midiCCOut(MIDImasterTune, masterTune);
}

void updatemasterVolume() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Master Volume", String(masterVolumestr) + " %");
  }
  midiCCOut(MIDImasterVolume, masterVolume);
}

void updatelayerPan() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Layer Pan", String(layerPanstr) + " %");
  }
  if (upperSW) {
    midiCCOut(MIDIlayerPanU, layerPanU);
  }
  if (lowerSW) {
    midiCCOut(MIDIlayerPanL, layerPanL);
  }
}

void updatelayerVolume() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Layer Volume", String(layerVolumestr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIlayerVolumeL, layerVolumeL);
  }
  if (upperSW) {
    midiCCOut(MIDIlayerVolumeU, layerVolumeU);
  }
}

void updatereverbLevel() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb Level", String(reverbLevelstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIreverbLevelL, reverbLevelL);
  }
  if (upperSW) {
    midiCCOut(MIDIreverbLevelU, reverbLevelU);
  }
}

void updatereverbDecay() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb Decay", String(reverbDecaystr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIreverbDecayL, reverbDecayL);
  }
  if (upperSW) {
    midiCCOut(MIDIreverbDecayU, reverbDecayU);
  }
}

void updatereverbEQ() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb EQ", String(reverbEQstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIreverbEQL, reverbEQL);
  }
  if (upperSW) {
    midiCCOut(MIDIreverbEQU, reverbEQU);
  }
}

void updatearpFrequency() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      if (arpSyncSWL) {
        showCurrentParameterPage("Arp Sync", String(arpFrequencystring));
      } else {
        showCurrentParameterPage("Arp Frequency", String(arpFrequencystr) + " Hz");
      }
    }
    if (upperSW) {
      if (arpSyncSWU) {
        showCurrentParameterPage("Arp Sync", String(arpFrequencystring));
      } else {
        showCurrentParameterPage("Arp Frequency", String(arpFrequencystr) + " Hz");
      }
    }
  }

  if (lowerSW) {
    midiCCOut(MIDIarpFrequencyL, arpFrequencyL);
  }
  if (upperSW) {
    midiCCOut(MIDIarpFrequencyU, arpFrequencyU);
  }
}

void updateampVelocity() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Amp Velocity", String(ampVelocitystr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIampVelocityL, ampVelocityL);
  }
  if (upperSW) {
    midiCCOut(MIDIampVelocityU, ampVelocityU);
  }
}

void updatefilterVelocity() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Velocity", String(filterVelocitystr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterVelocityL, filterVelocityL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterVelocityU, filterVelocityU);
  }
}

void updateampRelease() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Amp Release", String(ampReleasestr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIampReleaseL, ampReleaseL);
  }
  if (upperSW) {
    midiCCOut(MIDIampReleaseU, ampReleaseU);
  }
}

void updateampSustain() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Amp Sustain", String(ampSustainstr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIampSustainL, ampSustainL);
  }
  if (upperSW) {
    midiCCOut(MIDIampSustainU, ampSustainU);
  }
}

void updateampDecay() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Amp Decay", String(ampDecaystr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIampDecayU, ampDecayL);
  }
  if (upperSW) {
    midiCCOut(MIDIampDecayU, ampDecayU);
  }
}

void updateampAttack() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Amp Attack", String(ampAttackstr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIampAttackL, ampAttackL);
  }
  if (upperSW) {
    midiCCOut(MIDIampAttackU, ampAttackU);
  }
}

void updatefilterKeyboard() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Key Track", String(filterKeyboardstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterKeyboardU, filterKeyboardL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterKeyboardU, filterKeyboardU);
  }
}

void updatefilterResonance() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Res", String(filterResonancestr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterResonanceL, filterResonanceL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterResonanceU, filterResonanceU);
  }
}

void updateosc2Volume() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Osc2 Volume", String(osc2Volumestr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc2VolumeL, osc2VolumeL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc2VolumeU, osc2VolumeU);
  }
}

void updateosc2PW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Osc2 PW", String(osc2PWstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc2PWL, osc2PWL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc2PWU, osc2PWU);
  }
}

void updateosc1PW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Osc1 PW", String(osc1PWstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc1PWL, osc1PWL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc1PWU, osc1PWU);
  }
}

void updateosc1Volume() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Osc1 Volume", String(osc1Volumestr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc1VolumeL, osc1VolumeL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc1VolumeU, osc1VolumeU);
  }
}

void updatefilterCutoff() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Cutoff", String(filterCutoffstr) + " Hz");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterCutoffL, filterCutoffL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterCutoffU, filterCutoffU);
  }
}

void updatefilterEnvAmount() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Envelope", String(filterEnvAmountstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterEnvAmountL, filterEnvAmountL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterEnvAmountU, filterEnvAmountU);
  }
}

void updatefilterAttack() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Attack", String(filterAttackstr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterAttackL, filterAttackL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterAttackU, filterAttackU);
  }
}

void updatefilterDecay() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Decay", String(filterDecaystr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterDecayL, filterDecayL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterDecayU, filterDecayU);
  }
}

void updatefilterSustain() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Sustain", String(filterSustainstr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterSustainL, filterSustainL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterSustainU, filterSustainU);
  }
}

void updatefilterRelease() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter Release", String(filterReleasestr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIfilterReleaseU, filterReleaseL);
  }
  if (upperSW) {
    midiCCOut(MIDIfilterReleaseU, filterReleaseU);
  }
}

void updateechoEQ() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo EQ", String(echoEQstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIechoEQL, echoEQL);
  }
  if (upperSW) {
    midiCCOut(MIDIechoEQU, echoEQU);
  }
}

void updateechoLevel() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo Level", String(echoLevelstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIechoLevelL, echoLevelL);
  }
  if (upperSW) {
    midiCCOut(MIDIechoLevelU, echoLevelU);
  }
}

void updateechoFeedback() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo Feedbck", String(echoFeedbackstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIechoFeedbackL, echoFeedbackL);
  }
  if (upperSW) {
    midiCCOut(MIDIechoFeedbackU, echoFeedbackU);
  }
}

void updateechoSpread() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo Spread", String(echoSpreadstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIechoSpreadL, echoSpreadL);
  }
  if (upperSW) {
    midiCCOut(MIDIechoSpreadU, echoSpreadU);
  }
}

void updateechoTime() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      if (echoSyncSWL) {
        showCurrentParameterPage("Echo Sync", String(echoTimestring));
      } else {
        showCurrentParameterPage("Echo Time", String(echoTimestr) + " Hz");
      }
    }
    if (upperSW) {
      if (echoSyncSWU) {
        showCurrentParameterPage("Echo Sync", String(echoTimestring));
      } else {
        showCurrentParameterPage("Echo Time", String(echoTimestr) + " Hz");
      }
    }
  }
  if (lowerSW) {
    midiCCOut(MIDIechoTimeL, echoTimeL);
  }
  if (upperSW) {
    midiCCOut(MIDIechoTimeU, echoTimeU);
  }
}

void updatelfo2Destination() {
  if (!recallPatchFlag) {
    switch (lfo2Destinationstr) {
      case 0:
        showCurrentParameterPage("LFO2 Dest", "Lower");
        break;

      case 1:
        showCurrentParameterPage("LFO2 Dest", "Both");
        break;

      case 2:
        showCurrentParameterPage("LFO2 Dest", "Upper");
        break;
    }
  }
  midiCCOut(MIDIlfo2UpperLower, lfo2Destination);
}

void updateunisonDetune() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Unison Detune", String(unisonDetunestr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIunisonDetuneL, unisonDetuneL);
  }
  if (upperSW) {
    midiCCOut(MIDIunisonDetuneU, unisonDetuneU);
  }
}

void updateglideSpeed() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Glide Speed", String(glideSpeedstr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIglideSpeedL, glideSpeedL);
  }
  if (upperSW) {
    midiCCOut(MIDIglideSpeedU, glideSpeedU);
  }
}

void updateosc1Transpose() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 Trans", String(osc1Transposestr) + " Semi");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc1TransposeL, osc1TransposeL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc1TransposeU, osc1TransposeU);
  }
}

void updateosc2Transpose() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 Trans", String(osc2Transposestr) + " Semi");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc2TransposeL, osc2TransposeL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc2TransposeU, osc2TransposeU);
  }
}

void updatenoiseLevel() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Noise Level", String(noiseLevelstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDInoiseLevelL, noiseLevelL);
  }
  if (upperSW) {
    midiCCOut(MIDInoiseLevelU, noiseLevelU);
  }
}

void updateglideAmount() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Glide Amount", String(glideAmountstr) + " Cents");
  }
  if (lowerSW) {
    midiCCOut(MIDIglideAmountL, glideAmountL);
  }
  if (upperSW) {
    midiCCOut(MIDIglideAmountU, glideAmountU);
  }
}

void updateosc1Tune() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 Tune", String(osc1Tunestr) + " Cents");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc1TuneL, osc1TuneL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc1TuneU, osc1TuneU);
  }
}

void updateosc2Tune() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 Tune", String(osc2Tunestr) + " Cents");
  }
  if (lowerSW) {
    midiCCOut(MIDIosc2TuneL, osc2TuneL);
  }
  if (upperSW) {
    midiCCOut(MIDIosc2TuneU, osc2TuneU);
  }
}

void updatebendToFilter() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Bend to Filter", String(bendToFilterstr) + " %");
  }
  midiCCOut(MIDIbendToFilter, bendToFilter);
}

void updatelfo2ToFilter() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 to Filter", String(lfo2ToFilterstr) + " %");
  }
  midiCCOut(MIDIlfo2ToFilter, lfo2ToFilter);
}

void updatebendToOsc() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Bend to OSC", String(bendToOscstr) + " Semi");
  }
  midiCCOut(MIDIbendToOsc, bendToOsc);
}

void updatelfo2ToOsc() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 to OSC", String(lfo2ToOscstr) + " %");
  }
  midiCCOut(MIDIlfo2ToOsc, lfo2ToOsc);
}

void updatelfo2FreqAcc() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 Freq Acc", String(lfo2FreqAccstr) + " %");
  }
  midiCCOut(MIDIlfo2FreqAcc, lfo2FreqAcc);
}

void updatelfo2InitFrequency() {
  if (!recallPatchFlag) {
    if (lfo2SyncSW) {
      showCurrentParameterPage("LFO2 SYNC", String(lfo2InitFrequencystring));
    } else {
      showCurrentParameterPage("LFO2 Freq", String(lfo2InitFrequencystr) + " Hz");
    }
  }
  midiCCOut(MIDIlfo2InitFrequency, lfo2InitFrequency);
}

void updatelfo2InitAmount() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 Init Amnt", String(lfo2InitAmountstr) + " %");
  }
  midiCCOut(MIDIlfo2InitAmount, lfo2InitAmount);
}

void updateseqAssign() {
  if (!recallPatchFlag) {
    switch (seqAssignstr) {
      case 0:
        showCurrentParameterPage("Seq Assign", "Lower");
        break;

      case 1:
        showCurrentParameterPage("Seq Assign", "Upper");
        break;
    }
  }
  midiCCOut(CCseqAssign, seqAssign);
}

void updateseqRate() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Seq Rate", String(seqRatestr) + " %");
  }
  midiCCOut(CCseqRate, seqRate);
}

void updateseqGate() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Seq Gate", String(seqGatestr) + " %");
  }
  midiCCOut(CCseqGate, seqGate);
}

void updatelfo1Frequency() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      if (lfo1SyncSWL) {
        showCurrentParameterPage("LFO1 SYNC", String(lfo1Frequencystring));
      } else {
        showCurrentParameterPage("LFO1 Freq", String(lfo1Frequencystr) + " Hz");
      }
    }
    if (upperSW) {
      if (lfo1SyncSWU) {
        showCurrentParameterPage("LFO1 SYNC", String(lfo1Frequencystring));
      } else {
        showCurrentParameterPage("LFO1 Freq", String(lfo1Frequencystr) + " Hz");
      }
    }
  }
  if (lowerSW) {
    midiCCOut(MIDIlfo1FrequencyL, lfo1FrequencyL);
  }
  if (upperSW) {
    midiCCOut(MIDIlfo1FrequencyU, lfo1FrequencyU);
  }
}

void updatelfo1DepthA() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Depth A", String(lfo1DepthAstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIlfo1DepthAL, lfo1DepthAL);
  }
  if (upperSW) {
    midiCCOut(MIDIlfo1DepthAU, lfo1DepthAU);
  }
}

void updatelfo1Delay() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Delay", String(lfo1Delaystr) + " mS");
  }
  if (lowerSW) {
    midiCCOut(MIDIlfo1DelayL, lfo1DelayL);
  }
  if (upperSW) {
    midiCCOut(MIDIlfo1DelayU, lfo1DelayU);
  }
}

void updatelfo1DepthB() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Depth B", String(lfo1DepthBstr) + " %");
  }
  if (lowerSW) {
    midiCCOut(MIDIlfo1DepthBL, lfo1DepthBL);
  }
  if (upperSW) {
    midiCCOut(MIDIlfo1DepthBU, lfo1DepthBU);
  }
}

void updatearpRange4SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Range", "4 Octaves");
  }
  if (arpRange4SWL && lowerSW) {
    green.writePin(GREEN_ARP_RANGE_4_LED, HIGH);
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    arpRange3SWL = 0;
    arpRange2SWL = 0;
    arpRange1SWL = 0;
  }
  if (arpRange4SWU && upperSW) {
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_4_LED, HIGH);
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    arpRange3SWU = 0;
    arpRange2SWU = 0;
    arpRange1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpRange4SWU) {
      midiCCOut(MIDIarpRangeU, 65);
    }
    if (lowerSW && arpRange4SWL) {
      midiCCOut(MIDIarpRangeL, 65);
    }
  }
}

void updatearpRange3SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Range", "3 Octaves");
  }
  if (arpRange3SWL && lowerSW) {
    green.writePin(GREEN_ARP_RANGE_3_LED, HIGH);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    arpRange4SWL = 0;
    arpRange2SWL = 0;
    arpRange1SWL = 0;
  }
  if (arpRange3SWU && upperSW) {
    sr.writePin(ARP_RANGE_3_LED, HIGH);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    arpRange4SWU = 0;
    arpRange2SWU = 0;
    arpRange1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpRange3SWU) {
      midiCCOut(MIDIarpRangeU, 75);
    }
    if (lowerSW && arpRange3SWL) {
      midiCCOut(MIDIarpRangeL, 75);
    }
  }
}

void updatearpRange2SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Range", "2 Octaves");
  }
  if (arpRange2SWL && lowerSW) {
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, HIGH);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    arpRange4SWL = 0;
    arpRange3SWL = 0;
    arpRange1SWL = 0;
  }
  if (arpRange2SWU && upperSW) {
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, HIGH);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    arpRange4SWU = 0;
    arpRange3SWU = 0;
    arpRange1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpRange2SWU) {
      midiCCOut(MIDIarpRangeU, 85);
    }
    if (lowerSW && arpRange2SWL) {
      midiCCOut(MIDIarpRangeL, 85);
    }
  }
}

void updatearpRange1SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Range", "1 Octave");
  }
  if (arpRange1SWL && lowerSW) {
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, HIGH);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, LOW);
    arpRange4SWL = 0;
    arpRange2SWL = 0;
    arpRange3SWL = 0;
  }
  if (arpRange1SWU && upperSW) {
    sr.writePin(ARP_RANGE_3_LED, LOW);
    sr.writePin(ARP_RANGE_4_LED, LOW);
    sr.writePin(ARP_RANGE_2_LED, LOW);
    sr.writePin(ARP_RANGE_1_LED, HIGH);
    green.writePin(GREEN_ARP_RANGE_4_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_3_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_2_LED, LOW);
    green.writePin(GREEN_ARP_RANGE_1_LED, LOW);
    arpRange4SWU = 0;
    arpRange3SWU = 0;
    arpRange2SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpRange1SWU) {
      midiCCOut(MIDIarpRangeU, 97);
    }
    if (lowerSW && arpRange1SWL) {
      midiCCOut(MIDIarpRangeL, 97);
    }
  }
}

void updatearpSyncSW() {

  if (arpSyncSWL && lowerSW) {
    green.writePin(GREEN_ARP_SYNC_LED, HIGH);
    sr.writePin(ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIarpSyncL, 127);
    }
    arpSyncSW = 1;
  }
  if (!arpSyncSWL && lowerSW) {
    green.writePin(GREEN_ARP_SYNC_LED, LOW);
    sr.writePin(ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIarpSyncL, 127);
      }
    }
    arpSyncSW = 0;
  }
  if (arpSyncSWU && upperSW) {
    sr.writePin(ARP_SYNC_LED, HIGH);
    green.writePin(GREEN_ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIarpSyncU, 127);
    }
    arpSyncSW = 1;
  }
  if (!arpSyncSWU && upperSW) {
    sr.writePin(ARP_SYNC_LED, LOW);
    green.writePin(GREEN_ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIarpSyncU, 127);
      }
    }
    arpSyncSW = 0;
  }
}

void updatearpHoldSW() {

  if (arpHoldSWL && lowerSW) {
    green.writePin(GREEN_ARP_HOLD_LED, HIGH);
    sr.writePin(ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIarpHoldL, 127);
    }
    arpHoldSW = 1;
  }
  if (!arpHoldSWL && lowerSW) {
    green.writePin(GREEN_ARP_HOLD_LED, LOW);
    sr.writePin(ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIarpHoldL, 127);
      }
    }
    arpHoldSW = 0;
  }
  if (arpHoldSWU && upperSW) {
    sr.writePin(ARP_HOLD_LED, HIGH);
    green.writePin(GREEN_ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIarpHoldU, 127);
    }
    arpHoldSW = 1;
  }
  if (!arpHoldSWU && upperSW) {
    sr.writePin(ARP_HOLD_LED, LOW);
    green.writePin(GREEN_ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIarpHoldU, 127);
      }
    }
    arpHoldSW = 0;
  }
}

void updatelayerSoloSW() {

  if (layerSoloSW) {
    if (lowerSW) {
      green.writePin(GREEN_LAYER_SOLO_LED, HIGH);
      sr.writePin(LAYER_SOLO_LED, LOW);
    }
    if (upperSW) {
      green.writePin(GREEN_LAYER_SOLO_LED, LOW);
      sr.writePin(LAYER_SOLO_LED, HIGH);
    }
    if (!recallPatchFlag) {
      showCurrentParameterPage("Layer Solo", "On");
    }
  }
  if (!layerSoloSW) {
    green.writePin(GREEN_LAYER_SOLO_LED, LOW);
    sr.writePin(LAYER_SOLO_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Layer Solo", "Off");
    }
    if (upperSW) {
      sr.writePin(UPPER_LED, HIGH);
    }
    if (lowerSW) {
      sr.writePin(LOWER_LED, HIGH);
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(MIDIlayerSolo, 127);
  }
}

void updatemaxVoicesSW() {

  if (maxVoicesSW == 1 && maxVoicesFirstPress == 0) {
    maxVoices_timer = millis();
    maxVoices = 2;
    if (!recallPatchFlag) {
      showCurrentParameterPage("Max Voices", maxVoices);
    }
    midi6CCOut(MIDImaxVoicesSW, 127);
    midi6CCOut(MIDIDownArrow, 127);
    maxVoicesFirstPress++;
    updateMaxVoicesDisplay(maxVoices);
  } else if (maxVoicesSW == 1 && maxVoicesFirstPress > 0) {
    maxVoices++;
    if (maxVoices > 16) {
      maxVoices = 2;
    }
    updateMaxVoicesDisplay(maxVoices);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Max Voices", maxVoices);
    }
    midi6CCOut(MIDIDownArrow, 127);
    maxVoicesFirstPress++;
  }
}

void updatemaxVoicesExitSW() {
  if (maxVoicesExitSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Max Voices", maxVoices);
    }
    midi6CCOut(MIDIEnter, 127);
    maxVoicesFirstPress = 0;
    maxVoicesSW = 0;
    updateMaxVoicesDisplay(maxVoices);
  }
}

void updateMaxVoicesDisplay(int maxVoices) {
  switch (maxVoices) {
    case 2:
      trilldisplay.print("   2");
      break;
    case 3:
      trilldisplay.print("   3");
      break;
    case 4:
      trilldisplay.print("   4");
      break;
    case 5:
      trilldisplay.print("   5");
      break;
    case 6:
      trilldisplay.print("   6");
      break;
    case 7:
      trilldisplay.print("   7");
      break;
    case 8:
      trilldisplay.print("   8");
      break;
    case 9:
      trilldisplay.print("   9");
      break;
    case 10:
      trilldisplay.print("  10");
      break;
    case 11:
      trilldisplay.print("  11");
      break;
    case 12:
      trilldisplay.print("  12");
      break;
    case 13:
      trilldisplay.print("  13");
      break;
    case 14:
      trilldisplay.print("  14");
      break;
    case 15:
      trilldisplay.print("  15");
      break;
    case 16:
      trilldisplay.print("  16");
      break;
  }
}

void updateLowerSW() {
  if (lowerSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Lower", "On");
    }
    sr.writePin(LOWER_LED, HIGH);
    sr.writePin(UPPER_LED, LOW);
    upperSW = 0;
    if (layerSoloSW) {
      lower_timer = millis();
    }
    midiCCOut(MIDIpanel, 100);
    switchLEDs();
  }
}

void updateUpperSW() {
  if (upperSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Upper", "On");
    }
    sr.writePin(LOWER_LED, LOW);
    sr.writePin(UPPER_LED, HIGH);
    lowerSW = 0;
    if (layerSoloSW) {
      upper_timer = millis();
    }
    midiCCOut(MIDIpanel, 50);
    switchLEDs();
  }
}

void updatelimiterSW() {
  if (limiterSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Limiter", "On");
    }
    sr.writePin(LIMITER_LED, HIGH);
    midiCCOut(MIDIlimiter, 127);
  }
  if (!limiterSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Limiter", "Off");
    }
    sr.writePin(LIMITER_LED, LOW);
    midiCCOut(MIDIlimiter, 127);
  }
}

void updateseqPlaySW() {
  if (seqPlaySW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Play");
    }
    sr.writePin(SEQ_PLAY_LED, HIGH);
    sr.writePin(SEQ_STOP_LED, LOW);
    seqStopSW = 0;
    midi6CCOut(MIDIseqPlaySW, 127);
  }
}

void updateseqStopSW() {
  if (seqStopSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Stop");
    }
    sr.writePin(SEQ_PLAY_LED, LOW);
    sr.writePin(SEQ_STOP_LED, HIGH);
    seqPlaySW = 0;
    midi6CCOut(MIDIseqStopSW, 127);
  }
}

void updateseqKeySW() {
  if (seqKeySW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Key Start On");
    }
    sr.writePin(SEQ_KEY_LED, HIGH);
    midi6CCOut(MIDIseqKeySW, 127);
  }
  if (!seqKeySW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Key Start Off");

      sr.writePin(SEQ_KEY_LED, LOW);
      midi6CCOut(MIDIseqKeySW, 127);
    }
  }
}

void updateseqTransSW() {
  if (seqTransSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Transpose On");
    }
    sr.writePin(SEQ_TRANS_LED, HIGH);
    midi6CCOut(MIDIseqTransSW, 127);
  }
  if (!seqTransSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Transpose Off");

      sr.writePin(SEQ_TRANS_LED, LOW);
      midi6CCOut(MIDIseqTransSW, 127);
    }
  }
}

void updateseqLoopSW() {
  if (seqLoopSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Loop On");
    }
    sr.writePin(SEQ_LOOP_LED, HIGH);
    midi6CCOut(MIDIseqLoopSW, 127);
  }
  if (!seqLoopSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Loop Off");

      sr.writePin(SEQ_LOOP_LED, LOW);
      midi6CCOut(MIDIseqLoopSW, 127);
    }
  }
}

void updateseqFwSW() {
  if (seqFwSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "FF");
    }
    midi6CCOut(MIDIseqFwSW, 127);
  }
}

void updateseqBwSW() {
  if (seqBwSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Rew");
    }
    midi6CCOut(MIDIseqBwSW, 127);
  }
}

void updateseqEnable1SW() {
  if (seqEnable1SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", " Track 1");
    }
    sr.writePin(SEQ_ENABLE_1_LED, HIGH);
    midi6CCOut(MIDIseqEnable1SW, 127);
  }
  if (!seqEnable1SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Track 1 Off");
      sr.writePin(SEQ_ENABLE_1_LED, LOW);
      midi6CCOut(MIDIseqEnable1SW, 127);
    }
  }
}

void updateseqEnable2SW() {
  if (seqEnable2SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", " Track 2");
    }
    sr.writePin(SEQ_ENABLE_2_LED, HIGH);
    midi6CCOut(MIDIseqEnable2SW, 127);
  }
  if (!seqEnable2SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Track 2 Off");
      sr.writePin(SEQ_ENABLE_2_LED, LOW);
      midi6CCOut(MIDIseqEnable2SW, 127);
    }
  }
}

void updateseqEnable3SW() {
  if (seqEnable3SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", " Track 3");
    }
    sr.writePin(SEQ_ENABLE_3_LED, HIGH);
    midi6CCOut(MIDIseqEnable3SW, 127);
  }
  if (!seqEnable3SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Track 3 Off");
      sr.writePin(SEQ_ENABLE_3_LED, LOW);
      midi6CCOut(MIDIseqEnable3SW, 127);
    }
  }
}

void updateseqEnable4SW() {
  if (seqEnable4SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", " Track 4");
    }
    sr.writePin(SEQ_ENABLE_4_LED, HIGH);
    midi6CCOut(MIDIseqEnable4SW, 127);
  }
  if (!seqEnable4SW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Track 4 Off");
      sr.writePin(SEQ_ENABLE_4_LED, LOW);
      midi6CCOut(MIDIseqEnable4SW, 127);
    }
  }
}

void updateseqSyncSW() {
  if (seqSyncSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", " Sync On");
    }
    sr.writePin(SEQ_SYNC_LED, HIGH);
    midi6CCOut(MIDIseqSyncSW, 127);
  }
  if (!seqSyncSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Sync Off");
      sr.writePin(SEQ_SYNC_LED, LOW);
      midi6CCOut(MIDIseqSyncSW, 127);
    }
  }
}

void updateseqrecEditSW() {
  if (seqrecEditSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", " Rec/Edit On");
    }
    sr.writePin(SEQ_REC_EDIT_LED, HIGH);
    midi6CCOut(MIDIseqrecEditSW, 127);
  }
  if (!seqrecEditSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Rec/Edit Off");
    }
    sr.writePin(SEQ_REC_EDIT_LED, LOW);
    midi6CCOut(MIDIseqrecEditSW, 127);
  }
}

void updateseqinsStepSW() {
  if (seqinsStepSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Insert Step");
    }
    midi6CCOut(MIDIseqinsStepSW, 127);
  }
}

void updateseqdelStepSW() {
  if (seqdelStepSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Delete Step");
    }
    midi6CCOut(MIDIseqdelStepSW, 127);
  }
}

void updateseqaddStepSW() {
  if (seqaddStepSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Add Step");
    }
    midi6CCOut(MIDIseqaddStepSW, 127);
  }
}

void updateseqRestSW() {
  if (seqRestSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Insert Rest");
    }
    midi6CCOut(MIDIseqRestSW, 127);
  }
}

void updateseqUtilSW() {
  if (seqUtilSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Sequencer", "Utilities");
    }
    midi6CCOut(MIDIseqUtilSW, 127);
  }
}

void updateUtilitySW() {
  switch (utilitySW) {
    case 1:
      showCurrentParameterPage("Swap Layers", "Upper - Lower");
      oldutilitySW = utilitySW;
      break;

    case 2:
      showCurrentParameterPage("Copy Upper", "to Lower");
      oldutilitySW = utilitySW;
      break;

    case 3:
      showCurrentParameterPage("Copy Lower", "to Upper");
      oldutilitySW = utilitySW;
      break;

    case 4:
      showCurrentParameterPage("Reset Upper", "to Defaults");
      oldutilitySW = utilitySW;
      break;

    case 5:
      showCurrentParameterPage("Reset Lower", "to Defaults");
      oldutilitySW = utilitySW;
      break;

    case 6:
      showCurrentParameterPage("Copy FX Upper", "to Lower FX");
      oldutilitySW = utilitySW;
      break;

    default:
      showCurrentParameterPage("Copy FX Lower", "to Upper FX");
      oldutilitySW = 0;
      break;
  }
}

void swap(int &lowerVal, int &upperVal) {
  temp = lowerVal;
  lowerVal = upperVal;
  upperVal = temp;
}

void updateUtilityAction() {
  switch (utilitySW) {
    case 1:
      //Serial.println("1");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);

      swap(layerPanU, layerPanL);
      swap(layerVolumeU, layerVolumeL);
      swap(arpFrequencyU, arpFrequencyL);
      swap(ampVelocityU, ampVelocityL);
      swap(filterVelocityU, filterVelocityL);

      swap(ampReleaseL, ampReleaseU);
      swap(ampSustainL, ampSustainU);
      swap(ampDecayL, ampDecayU);
      swap(ampAttackL, ampAttackU);
      swap(filterKeyboardL, filterKeyboardU);
      swap(filterResonanceL, filterResonanceU);
      swap(osc2VolumeL, osc2VolumeU);
      swap(osc2PWL, osc2PWU);
      swap(osc1PWL, osc1PWU);
      swap(osc1VolumeL, osc1VolumeU);
      swap(filterCutoffL, filterCutoffU);
      swap(filterEnvAmountL, filterEnvAmountU);
      swap(filterAttackL, filterAttackU);
      swap(filterDecayL, filterDecayU);
      swap(filterSustainL, filterSustainU);
      swap(filterReleaseL, filterReleaseU);
      swap(unisonDetuneL, unisonDetuneU);
      swap(glideSpeedL, glideSpeedU);
      swap(osc1TransposeL, osc1TransposeU);
      swap(osc2TransposeL, osc2TransposeU);
      swap(noiseLevelL, noiseLevelU);
      swap(glideAmountL, glideAmountU);
      swap(osc1TuneL, osc1TuneU);
      swap(osc2TuneL, osc2TuneU);
      swap(lfo1FrequencyL, lfo1FrequencyU);
      swap(lfo1DepthAL, lfo1DepthAU);
      swap(lfo1DepthBL, lfo1DepthBU);
      swap(lfo1DelayL, lfo1DelayU);
      swap(arpRange4SWL, arpRange4SWU);
      swap(arpRange3SWL, arpRange3SWU);
      swap(arpRange2SWL, arpRange2SWU);
      swap(arpRange1SWL, arpRange1SWU);
      swap(arpSyncSWL, arpSyncSWU);
      swap(arpHoldSWL, arpHoldSWU);
      swap(arpRandSWL, arpRandSWU);
      swap(arpUpDownSWL, arpUpDownSWU);
      swap(arpDownSWL, arpDownSWU);
      swap(arpUpSWL, arpUpSWU);
      swap(arpOffSWL, arpOffSWU);
      swap(envInvSWL, envInvSWU);
      swap(filterHPSWL, filterHPSWU);
      swap(filterBP2SWL, filterBP2SWU);
      swap(filterBP1SWL, filterBP1SWU);
      swap(filterLP2SWL, filterLP2SWU);
      swap(filterLP1SWL, filterLP1SWU);
      swap(noisePinkSWL, noisePinkSWU);
      swap(noiseWhiteSWL, noiseWhiteSWU);
      swap(noiseOffSWL, noiseOffSWU);
      swap(osc1ringModSWL, osc1ringModSWU);
      swap(osc2ringModSWL, osc2ringModSWU);
      swap(osc1_osc2PWMSWL, osc1_osc2PWMSWU);
      swap(osc1squareSWL, osc1squareSWU);
      swap(osc1pulseSWL, osc1pulseSWU);
      swap(osc1squareSWL, osc1squareSWU);
      swap(osc1sawSWL, osc1sawSWU);
      swap(osc1triangleSWL, osc1triangleSWU);
      swap(osc2_osc1PWMSWL, osc2_osc1PWMSWU);
      swap(osc2pulseSWL, osc2pulseSWU);
      swap(osc2squareSWL, osc2squareSWU);
      swap(osc2sawSWL, osc2sawSWU);
      swap(osc2triangleSWL, osc2triangleSWU);
      swap(osc1_1SWL, osc1_1SWU);
      swap(osc1_2SWL, osc1_2SWU);
      swap(osc1_4SWL, osc1_4SWU);
      swap(osc1_8SWL, osc1_8SWU);
      swap(osc1_16SWL, osc1_16SWU);
      swap(osc2_1SWL, osc2_1SWU);
      swap(osc2_2SWL, osc2_2SWU);
      swap(osc2_4SWL, osc2_4SWU);
      swap(osc2_8SWL, osc2_8SWU);
      swap(osc2_16SWL, osc2_16SWU);
      swap(osc1glideSWL, osc1glideSWU);
      swap(osc2glideSWL, osc2glideSWU);
      swap(portSWL, portSWU);
      swap(glideSWL, glideSWU);
      swap(glideOffSWL, glideOffSWU);
      swap(osc2SyncSWL, osc2SyncSWU);
      swap(multiTriggerSWL, multiTriggerSWU);
      swap(polySWL, polySWU);
      swap(singleMonoSWL, singleMonoSWU);
      swap(unisonMonoSWL, unisonMonoSWU);
      swap(lfo1SyncSWL, lfo1SyncSWU);
      swap(lfo1modWheelSWL, lfo1modWheelSWU);
      swap(lfo1randSWL, lfo1randSWU);
      swap(lfo1resetSWL, lfo1resetSWU);
      swap(lfo1osc1SWL, lfo1osc1SWU);
      swap(lfo1osc2SWL, lfo1osc2SWU);
      swap(lfo1pw1SWL, lfo1pw1SWU);
      swap(lfo1pw2SWL, lfo1pw2SWU);
      swap(lfo1filtSWL, lfo1filtSWU);
      swap(lfo1ampSWL, lfo1ampSWU);
      swap(lfo1seqRateSWL, lfo1seqRateSWU);
      swap(lfo1squareUniSWL, lfo1squareUniSWU);
      swap(lfo1squareBipSWL, lfo1squareBipSWU);
      swap(lfo1sawUpSWL, lfo1sawUpSWU);
      swap(lfo1sawDnSWL, lfo1sawDnSWU);
      swap(lfo1triangleSWL, lfo1triangleSWU);

      // effects

      swap(reverbLevelL, reverbLevelU);
      swap(reverbDecayL, reverbDecayU);
      swap(reverbEQL, reverbEQU);
      swap(revGLTCSWL, revGLTCSWU);
      swap(revHallSWL, revHallSWU);
      swap(revPlateSWL, revPlateSWU);
      swap(revRoomSWL, revRoomSWU);
      swap(revOffSWL, revOffSWU);

      swap(echoEQL, echoEQU);
      swap(echoLevelL, echoLevelU);
      swap(echoFeedbackL, echoFeedbackU);
      swap(echoSpreadL, echoSpreadU);
      swap(echoTimeL, echoTimeU);
      swap(echoSyncSWL, echoSyncSWU);
      swap(echoPingPongSWL, echoPingPongSWU);
      swap(echoTapeSWL, echoTapeSWU);
      swap(echoSTDSWL, echoSTDSWU);
      swap(echoOffSWL, echoOffSWU);

      swap(chorus3SWL, chorus3SWU);
      swap(chorus2SWL, chorus2SWU);
      swap(chorus1SWL, chorus1SWU);
      swap(chorusOffSWL, chorusOffSWU);

      updateEverything();

      showCurrentParameterPage("Layer Swap", "Completed");
      recallPatchFlag = false;
      break;

    case 2:
      //Serial.println("2");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);

      layerPanL = layerPanU;
      layerVolumeL = layerVolumeU;
      arpFrequencyL = arpFrequencyU;
      ampVelocityL = ampVelocityU;
      filterVelocityL = filterVelocityU;
      ampReleaseL = ampReleaseU;
      ampSustainL = ampSustainU;
      ampDecayL = ampDecayU;
      ampAttackL = ampAttackU;
      filterKeyboardL = filterKeyboardU;
      filterResonanceL = filterResonanceU;
      osc2VolumeL = osc2VolumeU;
      osc2PWL = osc2PWU;
      osc1PWL = osc1PWU;
      osc1VolumeL = osc1VolumeU;
      filterCutoffL = filterCutoffU;
      filterEnvAmountL = filterEnvAmountU;
      filterAttackL = filterAttackU;
      filterDecayL = filterDecayU;
      filterSustainL = filterSustainU;
      filterReleaseL = filterReleaseU;
      unisonDetuneL = unisonDetuneU;
      glideSpeedL = glideSpeedU;
      osc1TransposeL = osc1TransposeU;
      osc2TransposeL = osc2TransposeU;
      noiseLevelL = noiseLevelU;
      glideAmountL = glideAmountU;
      osc1TuneL = osc1TuneU;
      osc2TuneL = osc2TuneU;
      lfo1FrequencyL = lfo1FrequencyU;
      lfo1DepthAL = lfo1DepthAU;
      lfo1DepthBL = lfo1DepthBU;
      lfo1DelayL = lfo1DelayU;
      arpRange4SWL = arpRange4SWU;
      arpRange3SWL = arpRange3SWU;
      arpRange2SWL = arpRange2SWU;
      arpRange1SWL = arpRange1SWU;
      arpSyncSWL = arpSyncSWU;
      arpHoldSWL = arpHoldSWU;
      arpRandSWL = arpRandSWU;
      arpUpDownSWL = arpUpDownSWU;
      arpDownSWL = arpDownSWU;
      arpUpSWL = arpUpSWU;
      arpOffSWL = arpOffSWU;
      envInvSWL = envInvSWU;
      filterHPSWL = filterHPSWU;
      filterBP2SWL = filterBP2SWU;
      filterBP1SWL = filterBP1SWU;
      filterLP2SWL = filterLP2SWU;
      filterLP1SWL = filterLP1SWU;
      noisePinkSWL = noisePinkSWU;
      noiseWhiteSWL = noiseWhiteSWU;
      noiseOffSWL = noiseOffSWU;
      osc1ringModSWL = osc1ringModSWU;
      osc2ringModSWL = osc2ringModSWU;
      osc1_osc2PWMSWL = osc1_osc2PWMSWU;
      osc1pulseSWL = osc1pulseSWU;
      osc1squareSWL = osc1squareSWU;
      osc1sawSWL = osc1sawSWU;
      osc1triangleSWL = osc1triangleSWU;
      osc2_osc1PWMSWL = osc2_osc1PWMSWU;
      osc2pulseSWL = osc2pulseSWU;
      osc2squareSWL = osc2squareSWU;
      osc2sawSWL = osc2sawSWU;
      osc2triangleSWL = osc2triangleSWU;
      osc1_1SWL = osc1_1SWU;
      osc1_2SWL = osc1_2SWU;
      osc1_4SWL = osc1_4SWU;
      osc1_8SWL = osc1_8SWU;
      osc1_16SWL = osc1_16SWU;
      osc2_1SWL = osc2_1SWU;
      osc2_2SWL = osc2_2SWU;
      osc2_4SWL = osc2_4SWU;
      osc2_8SWL = osc2_8SWU;
      osc2_16SWL = osc2_16SWU;
      osc1glideSWL = osc1glideSWU;
      osc2glideSWL = osc2glideSWU;
      portSWL = portSWU;
      glideSWL = glideSWU;
      glideOffSWL = glideOffSWU;
      osc2SyncSWL = osc2SyncSWU;
      multiTriggerSWL = multiTriggerSWU;
      polySWL = polySWU;
      singleMonoSWL = singleMonoSWU;
      unisonMonoSWL = unisonMonoSWU;
      lfo1SyncSWL = lfo1SyncSWU;
      lfo1modWheelSWL = lfo1modWheelSWU;
      lfo1randSWL = lfo1randSWU;
      lfo1resetSWL = lfo1resetSWU;
      lfo1osc1SWL = lfo1osc1SWU;
      lfo1osc2SWL = lfo1osc2SWU;
      lfo1pw1SWL = lfo1pw1SWU;
      lfo1pw2SWL = lfo1pw2SWU;
      lfo1filtSWL = lfo1filtSWU;
      lfo1ampSWL = lfo1ampSWU;
      lfo1seqRateSWL = lfo1seqRateSWU;
      lfo1squareUniSWL = lfo1squareUniSWU;
      lfo1squareBipSWL = lfo1squareBipSWU;
      lfo1sawUpSWL = lfo1sawUpSWU;
      lfo1sawDnSWL = lfo1sawDnSWU;
      lfo1triangleSWL = lfo1triangleSWU;

      // effects

      reverbLevelL = reverbLevelU;
      reverbDecayL = reverbDecayU;
      reverbEQL = reverbEQU;
      revGLTCSWL = revGLTCSWU;
      revHallSWL = revHallSWU;
      revPlateSWL = revPlateSWU;
      revRoomSWL = revRoomSWU;
      revOffSWL = revOffSWU;

      echoEQL = echoEQU;
      echoLevelL = echoLevelU;
      echoFeedbackL = echoFeedbackU;
      echoSpreadL = echoSpreadU;
      echoTimeL = echoTimeU;
      echoSyncSWL = echoSyncSWU;
      echoPingPongSWL = echoPingPongSWU;
      echoTapeSWL = echoTapeSWU;
      echoSTDSWL = echoSTDSWU;
      echoOffSWL = echoOffSWU;

      chorus3SWL = chorus3SWU;
      chorus2SWL = chorus2SWU;
      chorus1SWL = chorus1SWU;
      chorusOffSWL = chorusOffSWU;

      updateEverything();

      showCurrentParameterPage("Copy to Lower", "Completed");
      recallPatchFlag = false;
      break;

    case 3:
      //Serial.println("3");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);
      layerPanU = layerPanL;
      layerVolumeU = layerVolumeL;
      arpFrequencyU = arpFrequencyL;
      ampVelocityU = ampVelocityL;
      filterVelocityU = filterVelocityL;
      ampReleaseU = ampReleaseL;
      ampSustainU = ampSustainL;
      ampDecayU = ampDecayL;
      ampAttackU = ampAttackL;
      filterKeyboardU = filterKeyboardL;
      filterResonanceU = filterResonanceL;
      osc2VolumeU = osc2VolumeL;
      osc2PWU = osc2PWL;
      osc1PWU = osc1PWL;
      osc1VolumeU = osc1VolumeL;
      filterCutoffU = filterCutoffL;
      filterEnvAmountU = filterEnvAmountL;
      filterAttackU = filterAttackL;
      filterDecayU = filterDecayL;
      filterSustainU = filterSustainL;
      filterReleaseU = filterReleaseL;
      unisonDetuneU = unisonDetuneL;
      glideSpeedU = glideSpeedL;
      osc1TransposeU = osc1TransposeL;
      osc2TransposeU = osc2TransposeL;
      noiseLevelU = noiseLevelL;
      glideAmountU = glideAmountL;
      osc1TuneU = osc1TuneL;
      osc2TuneU = osc2TuneL;
      lfo1FrequencyU = lfo1FrequencyL;
      lfo1DepthAU = lfo1DepthAL;
      lfo1DepthBU = lfo1DepthBL;
      lfo1DelayU = lfo1DelayL;
      arpRange4SWU = arpRange4SWL;
      arpRange3SWU = arpRange3SWL;
      arpRange2SWU = arpRange2SWL;
      arpRange1SWU = arpRange1SWL;
      arpSyncSWU = arpSyncSWL;
      arpHoldSWU = arpHoldSWL;
      arpRandSWU = arpRandSWL;
      arpUpDownSWU = arpUpDownSWL;
      arpDownSWU = arpDownSWL;
      arpUpSWU = arpUpSWL;
      arpOffSWU = arpOffSWL;
      envInvSWU = envInvSWL;
      filterHPSWU = filterHPSWL;
      filterBP2SWU = filterBP2SWL;
      filterBP1SWU = filterBP1SWL;
      filterLP2SWU = filterLP2SWL;
      filterLP1SWU = filterLP1SWL;
      noisePinkSWU = noisePinkSWL;
      noiseWhiteSWU = noiseWhiteSWL;
      noiseOffSWU = noiseOffSWL;
      osc1ringModSWU = osc1ringModSWL;
      osc2ringModSWU = osc2ringModSWL;
      osc1_osc2PWMSWU = osc1_osc2PWMSWL;
      osc1pulseSWU = osc1pulseSWL;
      osc1squareSWU = osc1squareSWL;
      osc1sawSWU = osc1sawSWL;
      osc1triangleSWU = osc1triangleSWL;
      osc2_osc1PWMSWU = osc2_osc1PWMSWL;
      osc2pulseSWU = osc2pulseSWL;
      osc2squareSWU = osc2squareSWL;
      osc2sawSWU = osc2sawSWL;
      osc2triangleSWU = osc2triangleSWL;
      osc1_1SWU = osc1_1SWL;
      osc1_2SWU = osc1_2SWL;
      osc1_4SWU = osc1_4SWL;
      osc1_8SWU = osc1_8SWL;
      osc1_16SWU = osc1_16SWL;
      osc2_1SWU = osc2_1SWL;
      osc2_2SWU = osc2_2SWL;
      osc2_4SWU = osc2_4SWL;
      osc2_8SWU = osc2_8SWL;
      osc2_16SWU = osc2_16SWL;
      osc1glideSWU = osc1glideSWL;
      osc2glideSWU = osc2glideSWL;
      portSWU = portSWL;
      glideSWU = glideSWL;
      glideOffSWU = glideOffSWL;
      osc2SyncSWU = osc2SyncSWL;
      multiTriggerSWU = multiTriggerSWL;
      polySWU = polySWL;
      singleMonoSWU = singleMonoSWL;
      unisonMonoSWU = unisonMonoSWL;
      lfo1SyncSWU = lfo1SyncSWL;
      lfo1modWheelSWU = lfo1modWheelSWL;
      lfo1randSWU = lfo1randSWL;
      lfo1resetSWU = lfo1resetSWL;
      lfo1osc1SWU = lfo1osc1SWL;
      lfo1osc2SWU = lfo1osc2SWL;
      lfo1pw1SWU = lfo1pw1SWL;
      lfo1pw2SWU = lfo1pw2SWL;
      lfo1filtSWU = lfo1filtSWL;
      lfo1ampSWU = lfo1ampSWL;
      lfo1seqRateSWU = lfo1seqRateSWL;
      lfo1squareUniSWU = lfo1squareUniSWL;
      lfo1squareBipSWU = lfo1squareBipSWL;
      lfo1sawUpSWU = lfo1sawUpSWL;
      lfo1sawDnSWU = lfo1sawDnSWL;
      lfo1triangleSWU = lfo1triangleSWL;

      reverbLevelU = reverbLevelL;
      reverbDecayU = reverbDecayL;
      reverbEQU = reverbEQL;
      revGLTCSWU = revGLTCSWL;
      revHallSWU = revHallSWL;
      revPlateSWU = revPlateSWL;
      revRoomSWU = revRoomSWL;
      revOffSWU = revOffSWL;

      echoEQU = echoEQL;
      echoLevelU = echoLevelL;
      echoFeedbackU = echoFeedbackL;
      echoSpreadU = echoSpreadL;
      echoTimeU = echoTimeL;
      echoSyncSWU = echoSyncSWL;
      echoPingPongSWU = echoPingPongSWL;
      ;
      echoTapeSWU = echoTapeSWL;
      echoSTDSWU = echoSTDSWL;
      echoOffSWU = echoOffSWL;

      chorus3SWU = chorus3SWL;
      chorus2SWU = chorus2SWL;
      chorus1SWU = chorus1SWL;
      chorusOffSWU = chorusOffSWL;

      updateEverything();

      showCurrentParameterPage("Copy to Upper", "Completed");
      recallPatchFlag = false;
      break;

    case 4:
      //Serial.println("4");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);

      layerPanU = 63;
      layerVolumeU = 63;
      arpFrequencyU = 60;
      ampVelocityU = 0;
      filterVelocityU = 0;
      ampReleaseU = 55;
      ampSustainU = 127;
      ampDecayU = 55;
      ampAttackU = 0;
      filterKeyboardU = 127;
      filterResonanceU = 0;
      osc2VolumeU = 77;
      osc2PWU = 63;
      osc1PWU = 63;
      osc1VolumeU = 77;
      filterCutoffU = 127;
      filterEnvAmountU = 0;
      filterAttackU = 0;
      filterDecayU = 55;
      filterSustainU = 127;
      filterReleaseU = 55;
      unisonDetuneU = 22;
      glideSpeedU = 37;
      osc1TransposeU = 0;
      osc2TransposeU = 0;
      noiseLevelU = 0;
      glideAmountU = 55;
      osc1TuneU = 63;
      osc2TuneU = 63;
      lfo1FrequencyU = 82;
      lfo1DepthAU = 0;
      lfo1DepthBU = 0;
      lfo1DelayU = 0;
      arpRange4SWU = 0;
      arpRange3SWU = 0;
      arpRange2SWU = 1;
      arpRange1SWU = 0;
      arpSyncSWU = 0;
      arpHoldSWU = 0;
      arpRandSWU = 0;
      arpUpDownSWU = 0;
      arpDownSWU = 0;
      arpUpSWU = 0;
      arpOffSWU = 1;
      envInvSWU = 0;
      filterHPSWU = 0;
      filterBP2SWU = 0;
      filterBP1SWU = 0;
      filterLP2SWU = 0;
      filterLP1SWU = 1;
      noisePinkSWU = 0;
      noiseWhiteSWU = 1;
      noiseOffSWU = 0;
      osc1ringModSWU = 0;
      osc2ringModSWU = 0;
      osc1_osc2PWMSWU = 0;
      osc1pulseSWU = 0;
      osc1squareSWU = 0;
      osc1sawSWU = 1;
      osc1triangleSWU = 0;
      osc2_osc1PWMSWU = 0;
      osc2pulseSWU = 0;
      osc2squareSWU = 0;
      osc2sawSWU = 1;
      osc2triangleSWU = 0;
      osc1_1SWU = 0;
      osc1_2SWU = 0;
      osc1_4SWU = 0;
      osc1_8SWU = 1;
      osc1_16SWU = 0;
      osc2_1SWU = 0;
      osc2_2SWU = 0;
      osc2_4SWU = 0;
      osc2_8SWU = 1;
      osc2_16SWU = 0;
      osc1glideSWU = 1;
      osc2glideSWU = 1;
      portSWU = 0;
      glideSWU = 0;
      glideOffSWU = 1;
      osc2SyncSWU = 0;
      multiTriggerSWU = 1;
      polySWU = 1;
      singleMonoSWU = 0;
      unisonMonoSWU = 0;
      lfo1SyncSWU = 0;
      lfo1modWheelSWU = 0;
      lfo1resetSWU = 0;
      lfo1osc1SWU = 1;
      lfo1osc2SWU = 1;
      lfo1pw1SWU = 0;
      lfo1pw2SWU = 0;
      lfo1filtSWU = 1;
      lfo1ampSWU = 0;
      lfo1seqRateSWU = 0;
      lfo1randSWU = 0;
      lfo1squareUniSWU = 0;
      lfo1squareBipSWU = 0;
      lfo1sawUpSWU = 0;
      ;
      lfo1sawDnSWU = 0;
      lfo1triangleSWU = 1;

      reverbLevelU = 63;
      reverbDecayU = 63;
      reverbEQU = 63;
      revGLTCSWU = 0;
      revHallSWU = 0;
      revPlateSWU = 0;
      revRoomSWU = 0;
      revOffSWU = 1;

      echoEQU = 63;
      echoLevelU = 63;
      echoFeedbackU = 22;
      echoSpreadU = 48;
      echoTimeU = 63;
      echoSyncSWU = 0;
      echoPingPongSWU = 0;
      echoTapeSWU = 0;
      echoSTDSWU = 0;
      echoOffSWU = 1;

      chorus3SWU = 0;
      chorus2SWU = 0;
      chorus1SWU = 0;
      chorusOffSWU = 1;

      updateEverything();

      showCurrentParameterPage("Reset Upper", "Completed");
      recallPatchFlag = false;
      break;

    case 5:
      //Serial.println("5");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);

      layerPanL = 63;
      layerVolumeL = 63;
      arpFrequencyL = 60;
      ampVelocityL = 0;
      filterVelocityL = 0;
      ampReleaseL = 55;
      ampSustainL = 127;
      ampDecayL = 55;
      ampAttackL = 0;
      filterKeyboardL = 127;
      filterResonanceL = 0;
      osc2VolumeL = 80;
      osc2PWL = 63;
      osc1PWL = 63;
      osc1VolumeL = 80;
      filterCutoffL = 127;
      filterEnvAmountL = 0;
      filterAttackL = 0;
      filterDecayL = 55;
      filterSustainL = 127;
      filterReleaseL = 55;
      unisonDetuneL = 22;
      glideSpeedL = 37;
      osc1TransposeL = 0;
      osc2TransposeL = 0;
      noiseLevelL = 0;
      glideAmountL = 55;
      osc1TuneL = 63;
      osc2TuneL = 63;
      lfo1FrequencyL = 75;
      lfo1DepthAL = 0;
      lfo1DepthBL = 0;
      lfo1DelayL = 0;
      arpRange4SWL = 0;
      arpRange3SWL = 0;
      arpRange2SWL = 1;
      arpRange1SWL = 0;
      arpSyncSWL = 0;
      arpHoldSWL = 0;
      arpRandSWL = 0;
      arpUpDownSWL = 0;
      arpDownSWL = 0;
      arpUpSWL = 0;
      arpOffSWL = 1;
      envInvSWL = 0;
      filterHPSWL = 0;
      filterBP2SWL = 0;
      filterBP1SWL = 0;
      filterLP2SWL = 0;
      filterLP1SWL = 1;
      noisePinkSWL = 0;
      noiseWhiteSWL = 1;
      noiseOffSWL = 0;
      osc1ringModSWL = 0;
      osc2ringModSWL = 0;
      osc1_osc2PWMSWL = 0;
      osc1pulseSWL = 0;
      osc1squareSWL = 0;
      osc1sawSWL = 1;
      osc1triangleSWL = 0;
      osc2_osc1PWMSWL = 0;
      osc2pulseSWL = 0;
      osc2squareSWL = 0;
      osc2sawSWL = 1;
      osc2triangleSWL = 0;
      osc1_1SWL = 0;
      osc1_2SWL = 0;
      osc1_4SWL = 0;
      osc1_8SWL = 1;
      osc1_16SWL = 0;
      osc2_1SWL = 0;
      osc2_2SWL = 0;
      osc2_4SWL = 0;
      osc2_8SWL = 1;
      osc2_16SWL = 0;
      osc1glideSWL = 1;
      osc2glideSWL = 1;
      portSWL = 0;
      glideSWL = 0;
      glideOffSWL = 1;
      osc2SyncSWL = 0;
      multiTriggerSWL = 1;
      polySWL = 1;
      singleMonoSWL = 0;
      unisonMonoSWL = 0;
      lfo1SyncSWL = 0;
      lfo1modWheelSWL = 0;
      lfo1resetSWL = 0;
      lfo1osc1SWL = 1;
      lfo1osc2SWL = 1;
      lfo1pw1SWL = 0;
      lfo1pw2SWL = 0;
      lfo1filtSWL = 1;
      lfo1ampSWL = 0;
      lfo1seqRateSWL = 0;
      lfo1randSWL = 0;
      lfo1squareUniSWL = 0;
      lfo1squareBipSWL = 0;
      lfo1sawUpSWL = 0;
      lfo1sawDnSWL = 0;
      lfo1triangleSWL = 1;

      // effects

      reverbLevelL = 63;
      reverbDecayL = 63;
      reverbEQL = 63;
      revGLTCSWL = 0;
      revHallSWL = 0;
      revPlateSWL = 0;
      revRoomSWL = 0;
      revOffSWL = 1;

      echoEQL = 63;
      echoLevelL = 63;
      echoFeedbackL = 22;
      echoSpreadL = 55;
      echoTimeL = 63;
      echoSyncSWL = 0;
      echoPingPongSWL = 0;
      echoTapeSWL = 0;
      echoSTDSWL = 0;
      echoOffSWL = 1;

      chorus3SWL = 0;
      chorus2SWL = 0;
      chorus1SWL = 0;
      chorusOffSWL = 1;

      updateEverything();

      showCurrentParameterPage("Reset Lower", "Completed");
      recallPatchFlag = false;
      break;

    case 6:
      //Serial.println("6");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);

      reverbLevelL = reverbLevelU;
      reverbDecayL = reverbDecayU;
      reverbEQL = reverbEQU;
      revGLTCSWL = revGLTCSWU;
      revHallSWL = revHallSWU;
      revPlateSWL = revPlateSWU;
      revRoomSWL = revRoomSWU;
      revOffSWL = revOffSWU;

      echoEQL = echoEQU;
      echoLevelL = echoLevelU;
      echoFeedbackL = echoFeedbackU;
      echoSpreadL = echoSpreadU;
      echoTimeL = echoTimeU;
      echoSyncSWL = echoSyncSWU;
      echoPingPongSWL = echoPingPongSWU;
      echoTapeSWL = echoTapeSWU;
      echoSTDSWL = echoSTDSWU;
      echoOffSWL = echoOffSWU;

      chorus3SWL = chorus3SWU;
      chorus2SWL = chorus2SWU;
      chorus1SWL = chorus1SWU;
      chorusOffSWL = chorusOffSWU;

      updateEverything();

      showCurrentParameterPage("Copy FX Upper", "Completed");
      recallPatchFlag = false;
      break;

    default:
      //Serial.println("Default");
      recallPatchFlag = true;
      MIDI.sendProgramChange(0, midiOutCh);
      delay(20);

      reverbLevelU = reverbLevelL;
      reverbDecayU = reverbDecayL;
      reverbEQU = reverbEQL;
      revGLTCSWU = revGLTCSWL;
      revHallSWU = revHallSWL;
      revPlateSWU = revPlateSWL;
      revRoomSWU = revRoomSWL;
      revOffSWU = revOffSWL;

      echoEQU = echoEQL;
      echoLevelU = echoLevelL;
      echoFeedbackU = echoFeedbackL;
      echoSpreadU = echoSpreadL;
      echoTimeU = echoTimeL;
      echoSyncSWU = echoSyncSWL;
      echoPingPongSWU = echoPingPongSWL;
      echoTapeSWU = echoTapeSWL;
      echoSTDSWU = echoSTDSWL;
      echoOffSWU = echoOffSWL;

      chorus3SWU = chorus3SWL;
      chorus2SWU = chorus2SWL;
      chorus1SWU = chorus1SWL;
      chorusOffSWU = chorusOffSWL;

      updateEverything();

      showCurrentParameterPage("Copy FX Lower", "Completed");
      recallPatchFlag = false;
      break;
  }
}

void updatearpRandSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Random", "On");
  }
  if (arpRandSWL && lowerSW) {
    green.writePin(GREEN_ARP_RAND_LED, HIGH);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    arpUpDownSWL = 0;
    arpDownSWL = 0;
    arpUpSWL = 0;
    arpOffSWL = 0;
  }
  if (arpRandSWU && upperSW) {
    sr.writePin(ARP_RAND_LED, HIGH);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    arpUpDownSWU = 0;
    arpDownSWU = 0;
    arpUpSWU = 0;
    arpOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpRandSWU) {
      midiCCOut(MIDIarpDirectionU, 65);
    }
    if (lowerSW && arpRandSWL) {
      midiCCOut(MIDIarpDirectionL, 65);
    }
  }
}

void updatearpUpDownSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Up/Down", "On");
  }
  if (arpUpDownSWL && lowerSW) {
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, HIGH);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    arpRandSWL = 0;
    arpDownSWL = 0;
    arpUpSWL = 0;
    arpOffSWL = 0;
  }
  if (arpUpDownSWU && upperSW) {
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, HIGH);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    arpRandSWU = 0;
    arpDownSWU = 0;
    arpUpSWU = 0;
    arpOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpUpDownSWU) {
      midiCCOut(MIDIarpDirectionU, 75);
    }
    if (lowerSW && arpUpDownSWL) {
      midiCCOut(MIDIarpDirectionL, 75);
    }
  }
}

void updatearpDownSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Down", "On");
  }
  if (arpDownSWL && lowerSW) {
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, HIGH);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    arpRandSWL = 0;
    arpUpDownSWL = 0;
    arpUpSWL = 0;
    arpOffSWL = 0;
  }
  if (arpDownSWU && upperSW) {
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, HIGH);
    sr.writePin(ARP_UP_LED, LOW);
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    arpRandSWU = 0;
    arpUpDownSWU = 0;
    arpUpSWU = 0;
    arpOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpDownSWU) {
      midiCCOut(MIDIarpDirectionU, 85);
    }
    if (lowerSW && arpDownSWL) {
      midiCCOut(MIDIarpDirectionL, 85);
    }
  }
}

void updatearpUpSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP Up", "On");
  }
  if (arpUpSWL && lowerSW) {
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, HIGH);
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    arpRandSWL = 0;
    arpDownSWL = 0;
    arpUpDownSWL = 0;
    arpOffSWU = 0;
  }
  if (arpUpSWU && upperSW) {
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, HIGH);
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    arpRandSWU = 0;
    arpDownSWU = 0;
    arpUpDownSWU = 0;
    arpOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpUpSWU) {
      midiCCOut(MIDIarpDirectionU, 97);
    }
    if (lowerSW && arpUpSWL) {
      midiCCOut(MIDIarpDirectionL, 97);
    }
  }
}

void updatearpOffSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("ARP", "Off");
  }
  if (arpOffSWL && lowerSW) {
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    arpRandSWL = 0;
    arpDownSWL = 0;
    arpUpDownSWL = 0;
    arpUpSWL = 0;
  }
  if (arpOffSWU && upperSW) {
    sr.writePin(ARP_RAND_LED, LOW);
    sr.writePin(ARP_UP_DOWN_LED, LOW);
    sr.writePin(ARP_DOWN_LED, LOW);
    sr.writePin(ARP_UP_LED, LOW);
    green.writePin(GREEN_ARP_RAND_LED, LOW);
    green.writePin(GREEN_ARP_UP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_DOWN_LED, LOW);
    green.writePin(GREEN_ARP_UP_LED, LOW);
    arpRandSWU = 0;
    arpDownSWU = 0;
    arpUpDownSWU = 0;
    arpUpSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && arpOffSWU) {
      midiCCOut(MIDIarpDirectionU, 120);
    }
    if (lowerSW && arpOffSWL) {
      midiCCOut(MIDIarpDirectionL, 120);
    }
  }
}

void updateenvInvSW() {

  if (envInvSWL && lowerSW) {
    green.writePin(GREEN_ENV_INV_LED, HIGH);
    sr.writePin(ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIenvInvL, 127);
    }
    envInvSW = 1;
  }
  if (!envInvSWL && lowerSW) {
    green.writePin(GREEN_ENV_INV_LED, LOW);
    sr.writePin(ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIenvInvL, 127);
      }
    }
    envInvSW = 0;
  }
  if (envInvSWU && upperSW) {
    sr.writePin(ENV_INV_LED, HIGH);
    green.writePin(GREEN_ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIenvInvU, 127);
    }
    envInvSW = 1;
  }
  if (!envInvSWU && upperSW) {
    sr.writePin(ENV_INV_LED, LOW);
    green.writePin(GREEN_ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIenvInvU, 127);
      }
    }
    envInvSW = 0;
  }
}

void updatefilterHPSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter HP", "On");
  }
  if (filterHPSWL && lowerSW) {
    green.writePin(GREEN_FILT_HP_LED, HIGH);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    filterBP2SWL = 0;
    filterBP1SWL = 0;
    filterLP2SWL = 0;
    filterLP1SWL = 0;
  }
  if (filterHPSWU && upperSW) {
    sr.writePin(FILT_HP_LED, HIGH);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    filterBP2SWU = 0;
    filterBP1SWU = 0;
    filterLP2SWU = 0;
    filterLP1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && filterHPSWU) {
      midiCCOut(MIDIfilterU, 65);
    }
    if (lowerSW && filterHPSWL) {
      midiCCOut(MIDIfilterL, 65);
    }
  }
}

void updatefilterBP2SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter BP2", "On");
  }
  if (filterBP2SWL && lowerSW) {
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, HIGH);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    filterHPSWL = 0;
    filterBP1SWL = 0;
    filterLP2SWL = 0;
    filterLP1SWL = 0;
  }
  if (filterBP2SWU && upperSW) {
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, HIGH);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    filterHPSWU = 0;
    filterBP1SWU = 0;
    filterLP2SWU = 0;
    filterLP1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && filterBP2SWU) {
      midiCCOut(MIDIfilterU, 75);
    }
    if (lowerSW && filterBP2SWL) {
      midiCCOut(MIDIfilterL, 75);
    }
  }
}

void updatefilterBP1SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter BP1", "On");
  }
  if (filterBP1SWL && lowerSW) {
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, HIGH);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    filterHPSWL = 0;
    filterBP2SWL = 0;
    filterLP2SWL = 0;
    filterLP1SWL = 0;
  }
  if (filterBP1SWU && upperSW) {
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, HIGH);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    filterHPSWU = 0;
    filterBP2SWU = 0;
    filterLP2SWU = 0;
    filterLP1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && filterBP1SWU) {
      midiCCOut(MIDIfilterU, 85);
    }
    if (lowerSW && filterBP1SWL) {
      midiCCOut(MIDIfilterL, 85);
    }
  }
}

void updatefilterLP2SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter LP2", "On");
  }
  if (filterLP2SWL && lowerSW) {
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, HIGH);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    filterHPSWL = 0;
    filterBP2SWL = 0;
    filterBP1SWL = 0;
    filterLP1SWL = 0;
  }
  if (filterLP2SWU && upperSW) {
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, HIGH);
    sr.writePin(FILT_LP1_LED, LOW);
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    filterHPSWU = 0;
    filterBP2SWU = 0;
    filterBP1SWU = 0;
    filterLP1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && filterLP2SWU) {
      midiCCOut(MIDIfilterU, 97);
    }
    if (lowerSW && filterLP2SWL) {
      midiCCOut(MIDIfilterL, 97);
    }
  }
}

void updatefilterLP1SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Filter LP1", "On");
  }
  if (filterLP1SWL && lowerSW) {
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, HIGH);
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, LOW);
    filterHPSWL = 0;
    filterBP2SWL = 0;
    filterLP2SWL = 0;
    filterBP1SWL = 0;
  }
  if (filterLP1SWU && upperSW) {
    sr.writePin(FILT_HP_LED, LOW);
    sr.writePin(FILT_BP2_LED, LOW);
    sr.writePin(FILT_BP1_LED, LOW);
    sr.writePin(FILT_LP2_LED, LOW);
    sr.writePin(FILT_LP1_LED, HIGH);
    green.writePin(GREEN_FILT_HP_LED, LOW);
    green.writePin(GREEN_FILT_BP2_LED, LOW);
    green.writePin(GREEN_FILT_BP1_LED, LOW);
    green.writePin(GREEN_FILT_LP2_LED, LOW);
    green.writePin(GREEN_FILT_LP1_LED, LOW);
    filterHPSWU = 0;
    filterBP2SWU = 0;
    filterLP2SWU = 0;
    filterBP1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && filterLP1SWU) {
      midiCCOut(MIDIfilterU, 120);
    }
    if (lowerSW && filterLP1SWL) {
      midiCCOut(MIDIfilterL, 120);
    }
  }
}

void updaterevGLTCSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb GLTC", "On");
  }
  if (revGLTCSWL && lowerSW) {
    green.writePin(GREEN_REV_GLTC_LED, HIGH);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    revHallSWL = 0;
    revPlateSWL = 0;
    revRoomSWL = 0;
    revOffSWL = 0;
  }
  if (revGLTCSWU && upperSW) {
    sr.writePin(REV_GLTC_LED, HIGH);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    revHallSWU = 0;
    revPlateSWU = 0;
    revRoomSWU = 0;
    revOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && revGLTCSWU) {
      midiCCOut(MIDIreverbU, 65);
    }
    if (lowerSW && revGLTCSWL) {
      midiCCOut(MIDIreverbL, 65);
    }
  }
}

void updaterevHallSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb Hall", "On");
  }
  if (revHallSWL && lowerSW) {
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, HIGH);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    revGLTCSWL = 0;
    revPlateSWL = 0;
    revRoomSWL = 0;
    revOffSWL = 0;
  }
  if (revHallSWU && upperSW) {
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, HIGH);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    revGLTCSWU = 0;
    revPlateSWU = 0;
    revRoomSWU = 0;
    revOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && revHallSWU) {
      midiCCOut(MIDIreverbU, 75);
    }
    if (lowerSW && revHallSWL) {
      midiCCOut(MIDIreverbL, 75);
    }
  }
}

void updaterevPlateSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb Plate", "On");
  }
  if (revPlateSWL && lowerSW) {
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, HIGH);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    revGLTCSWL = 0;
    revHallSWL = 0;
    revRoomSWL = 0;
    revOffSWL = 0;
  }
  if (revPlateSWU && upperSW) {
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, HIGH);
    sr.writePin(REV_ROOM_LED, LOW);
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    revGLTCSWU = 0;
    revHallSWU = 0;
    revRoomSWU = 0;
    revOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && revPlateSWU) {
      midiCCOut(MIDIreverbU, 85);
    }
    if (lowerSW && revPlateSWL) {
      midiCCOut(MIDIreverbL, 85);
    }
  }
}

void updaterevRoomSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb Room", "On");
  }
  if (revRoomSWL && lowerSW) {
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, HIGH);
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    revGLTCSWL = 0;
    revHallSWL = 0;
    revPlateSWL = 0;
    revOffSWL = 0;
  }
  if (revRoomSWU && upperSW) {
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, HIGH);
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    revGLTCSWU = 0;
    revHallSWU = 0;
    revPlateSWU = 0;
    revOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && revRoomSWU) {
      midiCCOut(MIDIreverbU, 97);
    }
    if (lowerSW && revRoomSWL) {
      midiCCOut(MIDIreverbL, 97);
    }
  }
}

void updaterevOffSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Reverb", "Off");
  }
  if (revOffSWL && lowerSW) {
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    revGLTCSWL = 0;
    revHallSWL = 0;
    revPlateSWL = 0;
    revRoomSWL = 0;
  }
  if (revOffSWU && upperSW) {
    sr.writePin(REV_GLTC_LED, LOW);
    sr.writePin(REV_HALL_LED, LOW);
    sr.writePin(REV_PLT_LED, LOW);
    sr.writePin(REV_ROOM_LED, LOW);
    green.writePin(GREEN_REV_GLTC_LED, LOW);
    green.writePin(GREEN_REV_HALL_LED, LOW);
    green.writePin(GREEN_REV_PLT_LED, LOW);
    green.writePin(GREEN_REV_ROOM_LED, LOW);
    revGLTCSWU = 0;
    revHallSWU = 0;
    revPlateSWU = 0;
    revRoomSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && revOffSWU) {
      midiCCOut(MIDIreverbU, 120);
    }
    if (lowerSW && revOffSWL) {
      midiCCOut(MIDIreverbL, 120);
    }
  }
}

void updatenoisePinkSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Pink Noise", "On");
  }
  if (noisePinkSWL && lowerSW) {
    green.writePin(GREEN_PINK_LED, HIGH);
    green.writePin(GREEN_WHITE_LED, LOW);
    sr.writePin(PINK_LED, LOW);
    sr.writePin(WHITE_LED, LOW);
    noiseWhiteSWL = 0;
    noiseOffSWL = 0;
  }
  if (noisePinkSWU && upperSW) {
    sr.writePin(PINK_LED, HIGH);
    sr.writePin(WHITE_LED, LOW);
    green.writePin(GREEN_PINK_LED, LOW);
    green.writePin(GREEN_WHITE_LED, LOW);
    noiseWhiteSWU = 0;
    noiseOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && noisePinkSWU) {
      midiCCOut(MIDInoiseU, 85);
    }
    if (lowerSW && noisePinkSWL) {
      midiCCOut(MIDInoiseL, 85);
    }
  }
}

void updatenoiseWhiteSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("White Noise", "On");
  }
  if (noiseWhiteSWL && lowerSW) {
    green.writePin(GREEN_PINK_LED, LOW);
    green.writePin(GREEN_WHITE_LED, HIGH);
    sr.writePin(PINK_LED, LOW);
    sr.writePin(WHITE_LED, LOW);
    noisePinkSWL = 0;
    noiseOffSWL = 0;
  }
  if (noiseWhiteSWU && upperSW) {
    sr.writePin(PINK_LED, LOW);
    sr.writePin(WHITE_LED, HIGH);
    green.writePin(GREEN_PINK_LED, LOW);
    green.writePin(GREEN_WHITE_LED, LOW);
    noisePinkSWU = 0;
    noiseOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && noiseWhiteSWU) {
      midiCCOut(MIDInoiseU, 75);
    }
    if (lowerSW && noiseWhiteSWL) {
      midiCCOut(MIDInoiseL, 75);
    }
  }
}

void updatenoiseOffSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Noise", "Off");
  }
  if (noiseOffSWL && lowerSW) {
    green.writePin(GREEN_PINK_LED, LOW);
    green.writePin(GREEN_WHITE_LED, LOW);
    sr.writePin(PINK_LED, LOW);
    sr.writePin(WHITE_LED, LOW);
    noisePinkSWL = 0;
    noiseWhiteSWL = 0;
  }
  if (noiseOffSWU && upperSW) {
    sr.writePin(PINK_LED, LOW);
    sr.writePin(WHITE_LED, LOW);
    green.writePin(GREEN_PINK_LED, LOW);
    green.writePin(GREEN_WHITE_LED, LOW);
    noisePinkSWU = 0;
    noiseWhiteSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && noiseOffSWU) {
      midiCCOut(MIDInoiseU, 65);
    }
    if (lowerSW && noiseOffSWL) {
      midiCCOut(MIDInoiseL, 65);
    }
  }
}

void updateechoSyncSW() {

  if (echoSyncSWL && lowerSW) {
    green.writePin(GREEN_ECHO_SYNC_LED, HIGH);
    sr.writePin(ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIechoSyncL, 127);
    }
    echoSyncSW = 1;
  }
  if (!echoSyncSWL && lowerSW) {
    green.writePin(GREEN_ECHO_SYNC_LED, LOW);
    sr.writePin(ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIechoSyncL, 127);
      }
    }
    echoSyncSW = 0;
  }
  if (echoSyncSWU && upperSW) {
    sr.writePin(ECHO_SYNC_LED, HIGH);
    green.writePin(GREEN_ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIechoSyncU, 127);
    }
    echoSyncSW = 1;
  }
  if (!echoSyncSWU && upperSW) {
    sr.writePin(ECHO_SYNC_LED, LOW);
    green.writePin(GREEN_ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIechoSyncU, 127);
      }
    }
    echoSyncSW = 0;
  }
}

void updateosc1ringModSW() {

  if (osc1ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC1_RINGMOD_LED, HIGH);
    sr.writePin(OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc1ringL, 127);
    }
    osc1ringModSW = 1;
  }
  if (!osc1ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC1_RINGMOD_LED, LOW);
    sr.writePin(OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc1ringL, 127);
      }
    }
    osc1ringModSW = 0;
  }
  if (osc1ringModSWU && upperSW) {
    sr.writePin(OSC1_RINGMOD_LED, HIGH);
    green.writePin(GREEN_OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc1ringU, 127);
    }
    osc1ringModSW = 1;
  }
  if (!osc1ringModSWU && upperSW) {
    sr.writePin(OSC1_RINGMOD_LED, LOW);
    green.writePin(GREEN_OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc1ringU, 127);
      }
    }
    osc1ringModSW = 0;
  }
}

void updateosc2ringModSW() {

  if (osc2ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC2_RINGMOD_LED, HIGH);
    sr.writePin(OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc2ringL, 127);
    }
    osc2ringModSW = 1;
  }
  if (!osc2ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC2_RINGMOD_LED, LOW);
    sr.writePin(OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc2ringL, 127);
      }
    }
    osc2ringModSW = 0;
  }
  if (osc2ringModSWU && upperSW) {
    sr.writePin(OSC2_RINGMOD_LED, HIGH);
    green.writePin(GREEN_OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc2ringU, 127);
    }
    osc2ringModSW = 1;
  }
  if (!osc2ringModSWU && upperSW) {
    sr.writePin(OSC2_RINGMOD_LED, LOW);
    green.writePin(GREEN_OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc2ringU, 127);
      }
    }
    osc2ringModSW = 0;
  }
}

void updateosc1_osc2PWMSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 PWM", "On");
  }
  if (osc1_osc2PWMSWL && lowerSW) {
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, HIGH);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    osc1pulseSWL = 0;
    osc1squareSWL = 0;
    osc1sawSWL = 0;
    osc1triangleSWL = 0;
  }
  if (osc1_osc2PWMSWU && upperSW) {
    sr.writePin(OSC1_OSC2_PWM_LED, HIGH);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    osc1pulseSWU = 0;
    osc1squareSWU = 0;
    osc1sawSWU = 0;
    osc1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1_osc2PWMSWU) {
      midiCCOut(MIDIosc1WaveU, 120);
    }
    if (lowerSW && osc1_osc2PWMSWL) {
      midiCCOut(MIDIosc1WaveL, 65);
    }
  }
}

void updateosc1pulseSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 Pulse", "On");
  }
  if (osc1pulseSWL && lowerSW) {
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, HIGH);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWL = 0;
    osc1squareSWL = 0;
    osc1sawSWL = 0;
    osc1triangleSWL = 0;
  }
  if (osc1pulseSWU && upperSW) {
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, HIGH);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWU = 0;
    osc1squareSWU = 0;
    osc1sawSWU = 0;
    osc1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1pulseSWU) {
      midiCCOut(MIDIosc1WaveU, 97);
    }
    if (lowerSW && osc1pulseSWL) {
      midiCCOut(MIDIosc1WaveL, 75);
    }
  }
}

void updateosc1squareSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 Square", "On");
  }
  if (osc1squareSWL && lowerSW) {
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, HIGH);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWL = 0;
    osc1pulseSWL = 0;
    osc1sawSWL = 0;
    osc1triangleSWL = 0;
  }
  if (osc1squareSWU && upperSW) {
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, HIGH);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWU = 0;
    osc1pulseSWU = 0;
    osc1sawSWU = 0;
    osc1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1squareSWU) {
      midiCCOut(MIDIosc1WaveU, 85);
    }
    if (lowerSW && osc1squareSWL) {
      midiCCOut(MIDIosc1WaveL, 85);
    }
  }
}

void updateosc1sawSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 Saw", "On");
  }
  if (osc1sawSWL && lowerSW) {
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, HIGH);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWL = 0;
    osc1pulseSWL = 0;
    osc1squareSWL = 0;
    osc1triangleSWL = 0;
  }
  if (osc1sawSWU && upperSW) {
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, HIGH);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWU = 0;
    osc1pulseSWU = 0;
    osc1squareSWU = 0;
    osc1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1sawSWU) {
      midiCCOut(MIDIosc1WaveU, 75);
    }
    if (lowerSW && osc1sawSWL) {
      midiCCOut(MIDIosc1WaveL, 97);
    }
  }
}

void updateosc1triangleSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 Triangle", "On");
  }
  if (osc1triangleSWL && lowerSW) {
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, HIGH);
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWL = 0;
    osc1pulseSWL = 0;
    osc1squareSWL = 0;
    osc1sawSWL = 0;
  }
  if (osc1triangleSWU && upperSW) {
    sr.writePin(OSC1_OSC2_PWM_LED, LOW);
    sr.writePin(OSC1_PULSE_LED, LOW);
    sr.writePin(OSC1_SQUARE_LED, LOW);
    sr.writePin(OSC1_SAW_LED, LOW);
    sr.writePin(OSC1_TRIANGLE_LED, HIGH);
    green.writePin(GREEN_OSC1_OSC2_PWM_LED, LOW);
    green.writePin(GREEN_OSC1_PULSE_LED, LOW);
    green.writePin(GREEN_OSC1_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC1_SAW_LED, LOW);
    green.writePin(GREEN_OSC1_TRIANGLE_LED, LOW);
    osc1_osc2PWMSWU = 0;
    osc1pulseSWU = 0;
    osc1squareSWU = 0;
    osc1sawSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1triangleSWU) {
      midiCCOut(MIDIosc1WaveU, 65);
    }
    if (lowerSW && osc1triangleSWL) {
      midiCCOut(MIDIosc1WaveL, 120);
    }
  }
}

void updateosc2_osc1PWMSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1 PWM", "On");
  }
  if (osc2_osc1PWMSWL && lowerSW) {
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, HIGH);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    osc2pulseSWL = 0;
    osc2squareSWL = 0;
    osc2sawSWL = 0;
    osc2triangleSWL = 0;
  }
  if (osc2_osc1PWMSWU && upperSW) {
    sr.writePin(OSC2_OSC1_PWM_LED, HIGH);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    osc2pulseSWU = 0;
    osc2squareSWU = 0;
    osc2sawSWU = 0;
    osc2triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2_osc1PWMSWU) {
      midiCCOut(MIDIosc2WaveU, 65);
    }
    if (lowerSW && osc2_osc1PWMSWL) {
      midiCCOut(MIDIosc2WaveL, 65);
    }
  }
}

void updateosc2pulseSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 Pulse", "On");
  }
  if (osc2pulseSWL && lowerSW) {
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, HIGH);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWL = 0;
    osc2squareSWL = 0;
    osc2sawSWL = 0;
    osc2triangleSWL = 0;
  }
  if (osc2pulseSWU && upperSW) {
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, HIGH);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWU = 0;
    osc2squareSWU = 0;
    osc2sawSWU = 0;
    osc2triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2pulseSWU) {
      midiCCOut(MIDIosc2WaveU, 75);
    }
    if (lowerSW && osc2pulseSWL) {
      midiCCOut(MIDIosc2WaveL, 75);
    }
  }
}

void updateosc2squareSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 Square", "On");
  }
  if (osc2squareSWL && lowerSW) {
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, HIGH);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWL = 0;
    osc2pulseSWL = 0;
    osc2sawSWL = 0;
    osc2triangleSWL = 0;
  }
  if (osc2squareSWU && upperSW) {
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, HIGH);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWU = 0;
    osc2pulseSWU = 0;
    osc2sawSWU = 0;
    osc2triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2squareSWU) {
      midiCCOut(MIDIosc2WaveU, 85);
    }
    if (lowerSW && osc2squareSWL) {
      midiCCOut(MIDIosc2WaveL, 85);
    }
  }
}

void updateosc2sawSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 Saw", "On");
  }
  if (osc2sawSWL && lowerSW) {
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, HIGH);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWL = 0;
    osc2pulseSWL = 0;
    osc2squareSWL = 0;
    osc2triangleSWL = 0;
  }
  if (osc2sawSWU && upperSW) {
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, HIGH);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWU = 0;
    osc2pulseSWU = 0;
    osc2squareSWU = 0;
    osc2triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2sawSWU) {
      midiCCOut(MIDIosc2WaveU, 97);
    }
    if (lowerSW && osc2sawSWL) {
      midiCCOut(MIDIosc2WaveL, 97);
    }
  }
}

void updateosc2triangleSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2 Triangle", "On");
  }
  if (osc2triangleSWL && lowerSW) {
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, HIGH);
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWL = 0;
    osc2pulseSWL = 0;
    osc2squareSWL = 0;
    osc2sawSWL = 0;
  }
  if (osc2triangleSWU && upperSW) {
    sr.writePin(OSC2_OSC1_PWM_LED, LOW);
    sr.writePin(OSC2_PULSE_LED, LOW);
    sr.writePin(OSC2_SQUARE_LED, LOW);
    sr.writePin(OSC2_SAW_LED, LOW);
    sr.writePin(OSC2_TRIANGLE_LED, HIGH);
    green.writePin(GREEN_OSC2_OSC1_PWM_LED, LOW);
    green.writePin(GREEN_OSC2_PULSE_LED, LOW);
    green.writePin(GREEN_OSC2_SQUARE_LED, LOW);
    green.writePin(GREEN_OSC2_SAW_LED, LOW);
    green.writePin(GREEN_OSC2_TRIANGLE_LED, LOW);
    osc2_osc1PWMSWU = 0;
    osc2pulseSWU = 0;
    osc2squareSWU = 0;
    osc2sawSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2triangleSWU) {
      midiCCOut(MIDIosc2WaveU, 120);
    }
    if (lowerSW && osc2triangleSWL) {
      midiCCOut(MIDIosc2WaveL, 120);
    }
  }
}

void updateechoPingPongSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo PingPong", "On");
  }
  if (echoPingPongSWL && lowerSW) {
    green.writePin(GREEN_ECHO_PINGPONG_LED, HIGH);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, LOW);
    echoTapeSWL = 0;
    echoSTDSWL = 0;
    echoOffSWL = 0;
  }
  if (echoPingPongSWU && upperSW) {
    sr.writePin(ECHO_PINGPONG_LED, HIGH);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, LOW);
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    echoTapeSWU = 0;
    echoSTDSWU = 0;
    echoOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && echoPingPongSWU) {
      midiCCOut(MIDIechoU, 65);
    }
    if (lowerSW && echoPingPongSWL) {
      midiCCOut(MIDIechoL, 65);
    }
  }
}

void updateechoTapeSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo Tape", "On");
  }
  if (echoTapeSWL && lowerSW) {
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, HIGH);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, LOW);
    echoPingPongSWL = 0;
    echoSTDSWL = 0;
    echoOffSWL = 0;
  }
  if (echoTapeSWU && upperSW) {
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, HIGH);
    sr.writePin(ECHO_STD_LED, LOW);
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    echoPingPongSWU = 0;
    echoSTDSWU = 0;
    echoOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && echoTapeSWU) {
      midiCCOut(MIDIechoU, 75);
    }
    if (lowerSW && echoTapeSWL) {
      midiCCOut(MIDIechoL, 75);
    }
  }
}

void updateechoSTDSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo STD", "On");
  }
  if (echoSTDSWL && lowerSW) {
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, HIGH);
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, LOW);
    echoPingPongSWL = 0;
    echoTapeSWL = 0;
    echoOffSWL = 0;
  }
  if (echoSTDSWU && upperSW) {
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, HIGH);
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    echoPingPongSWU = 0;
    echoTapeSWU = 0;
    echoOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && echoSTDSWU) {
      midiCCOut(MIDIechoU, 85);
    }
    if (lowerSW && echoSTDSWL) {
      midiCCOut(MIDIechoL, 85);
    }
  }
}

void updateechoOffSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Echo", "Off");
  }
  if (echoOffSWL && lowerSW) {
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, LOW);
    echoPingPongSWL = 0;
    echoTapeSWL = 0;
    echoSTDSWL = 0;
  }
  if (echoOffSWU && upperSW) {
    sr.writePin(ECHO_PINGPONG_LED, LOW);
    sr.writePin(ECHO_TAPE_LED, LOW);
    sr.writePin(ECHO_STD_LED, LOW);
    green.writePin(GREEN_ECHO_PINGPONG_LED, LOW);
    green.writePin(GREEN_ECHO_TAPE_LED, LOW);
    green.writePin(GREEN_ECHO_STD_LED, LOW);
    echoPingPongSWU = 0;
    echoTapeSWU = 0;
    echoSTDSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && echoOffSWU) {
      midiCCOut(MIDIechoU, 97);
    }
    if (lowerSW && echoOffSWL) {
      midiCCOut(MIDIechoL, 97);
    }
  }
}

void updatechorus3SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Chorus 3", "On");
  }
  if (chorus3SWL && lowerSW) {
    green.writePin(GREEN_CHORUS_3_LED, HIGH);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, LOW);
    chorus2SWL = 0;
    chorus1SWL = 0;
    chorusOffSWL = 0;
  }
  if (chorus3SWU && upperSW) {
    sr.writePin(CHORUS_3_LED, HIGH);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, LOW);
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    chorus2SWU = 0;
    chorus1SWU = 0;
    chorusOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && chorus3SWU) {
      midiCCOut(MIDIchorusU, 65);
    }
    if (lowerSW && chorus3SWL) {
      midiCCOut(MIDIchorusL, 65);
    }
  }
}

void updatechorus2SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Chorus 2", "On");
  }
  if (chorus2SWL && lowerSW) {
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, HIGH);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, LOW);
    chorus3SWL = 0;
    chorus1SWL = 0;
    chorusOffSWL = 0;
  }
  if (chorus2SWU && upperSW) {
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, HIGH);
    sr.writePin(CHORUS_1_LED, LOW);
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    chorus3SWU = 0;
    chorus1SWU = 0;
    chorusOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && chorus2SWU) {
      midiCCOut(MIDIchorusU, 75);
    }
    if (lowerSW && chorus2SWL) {
      midiCCOut(MIDIchorusL, 75);
    }
  }
}

void updatechorus1SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Chorus 1", "On");
  }
  if (chorus1SWL && lowerSW) {
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, HIGH);
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, LOW);
    chorus3SWL = 0;
    chorus2SWL = 0;
    chorusOffSWL = 0;
  }
  if (chorus1SWU && upperSW) {
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, HIGH);
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    chorus3SWU = 0;
    chorus2SWU = 0;
    chorusOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && chorus1SWU) {
      midiCCOut(MIDIchorusU, 85);
    }
    if (lowerSW && chorus1SWL) {
      midiCCOut(MIDIchorusL, 85);
    }
  }
}

void updatechorusOffSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Chorus", "Off");
  }
  if (chorusOffSWL && lowerSW) {
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, LOW);
    chorus3SWL = 0;
    chorus2SWL = 0;
    chorus1SWL = 0;
  }
  if (chorusOffSWU && upperSW) {
    sr.writePin(CHORUS_3_LED, LOW);
    sr.writePin(CHORUS_2_LED, LOW);
    sr.writePin(CHORUS_1_LED, LOW);
    green.writePin(GREEN_CHORUS_3_LED, LOW);
    green.writePin(GREEN_CHORUS_2_LED, LOW);
    green.writePin(GREEN_CHORUS_1_LED, LOW);
    chorus3SWU = 0;
    chorus2SWU = 0;
    chorus1SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && chorusOffSWU) {
      midiCCOut(MIDIchorusU, 97);
    }
    if (lowerSW && chorusOffSWL) {
      midiCCOut(MIDIchorusL, 97);
    }
  }
}

void updateosc1_1SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1", "1 Foot");
  }
  if (osc1_1SWL && lowerSW) {
    green.writePin(GREEN_OSC1_1_LED, HIGH);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_2SWL = 0;
    osc1_4SWL = 0;
    osc1_8SWL = 0;
    osc1_16SWL = 0;
  }
  if (osc1_1SWU && upperSW) {
    sr.writePin(OSC1_1_LED, HIGH);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    osc1_2SWU = 0;
    osc1_4SWU = 0;
    osc1_8SWU = 0;
    osc1_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1_1SWU) {
      midiCCOut(MIDIosc1FootU, 65);
    }
    if (lowerSW && osc1_1SWL) {
      midiCCOut(MIDIosc1FootL, 65);
    }
  }
}

void updateosc1_2SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1", "2 Foot");
  }
  if (osc1_2SWL && lowerSW) {
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, HIGH);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_1SWL = 0;
    osc1_4SWL = 0;
    osc1_8SWL = 0;
    osc1_16SWL = 0;
  }
  if (osc1_2SWU && upperSW) {
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, HIGH);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    osc1_1SWU = 0;
    osc1_4SWU = 0;
    osc1_8SWU = 0;
    osc1_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1_2SWU) {
      midiCCOut(MIDIosc1FootU, 75);
    }
    if (lowerSW && osc1_2SWL) {
      midiCCOut(MIDIosc1FootL, 75);
    }
  }
}

void updateosc1_4SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1", "4 Foot");
  }
  if (osc1_4SWL && lowerSW) {
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, HIGH);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_1SWL = 0;
    osc1_2SWL = 0;
    osc1_8SWL = 0;
    osc1_16SWL = 0;
  }
  if (osc1_4SWU && upperSW) {
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, HIGH);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    osc1_1SWU = 0;
    osc1_2SWU = 0;
    osc1_8SWU = 0;
    osc1_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1_4SWU) {
      midiCCOut(MIDIosc1FootU, 85);
    }
    if (lowerSW && osc1_4SWL) {
      midiCCOut(MIDIosc1FootL, 85);
    }
  }
}

void updateosc1_8SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1", "8 Foot");
  }
  if (osc1_8SWL && lowerSW) {
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, HIGH);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_1SWL = 0;
    osc1_2SWL = 0;
    osc1_4SWL = 0;
    osc1_16SWL = 0;
  }
  if (osc1_8SWU && upperSW) {
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, HIGH);
    sr.writePin(OSC1_16_LED, LOW);
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    osc1_1SWU = 0;
    osc1_2SWU = 0;
    osc1_4SWU = 0;
    osc1_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1_8SWU) {
      midiCCOut(MIDIosc1FootU, 97);
    }
    if (lowerSW && osc1_8SWL) {
      midiCCOut(MIDIosc1FootL, 97);
    }
  }
}

void updateosc1_16SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC1", "16 Foot");
  }
  if (osc1_16SWL && lowerSW) {
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, HIGH);
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, LOW);
    osc1_1SWL = 0;
    osc1_2SWL = 0;
    osc1_4SWL = 0;
    osc1_8SWL = 0;
  }
  if (osc1_16SWU && upperSW) {
    sr.writePin(OSC1_1_LED, LOW);
    sr.writePin(OSC1_2_LED, LOW);
    sr.writePin(OSC1_4_LED, LOW);
    sr.writePin(OSC1_8_LED, LOW);
    sr.writePin(OSC1_16_LED, HIGH);
    green.writePin(GREEN_OSC1_1_LED, LOW);
    green.writePin(GREEN_OSC1_2_LED, LOW);
    green.writePin(GREEN_OSC1_4_LED, LOW);
    green.writePin(GREEN_OSC1_8_LED, LOW);
    green.writePin(GREEN_OSC1_16_LED, LOW);
    osc1_1SWU = 0;
    osc1_2SWU = 0;
    osc1_4SWU = 0;
    osc1_8SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc1_16SWU) {
      midiCCOut(MIDIosc1FootU, 120);
    }
    if (lowerSW && osc1_16SWL) {
      midiCCOut(MIDIosc1FootL, 120);
    }
  }
}

void updateosc2_1SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2", "1 Foot");
  }
  if (osc2_1SWL && lowerSW) {
    green.writePin(GREEN_OSC2_1_LED, HIGH);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_2SWL = 0;
    osc2_4SWL = 0;
    osc2_8SWL = 0;
    osc2_16SWL = 0;
  }
  if (osc2_1SWU && upperSW) {
    sr.writePin(OSC2_1_LED, HIGH);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    osc2_2SWU = 0;
    osc2_4SWU = 0;
    osc2_8SWU = 0;
    osc2_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2_1SWU) {
      midiCCOut(MIDIosc2FootU, 65);
    }
    if (lowerSW && osc2_1SWL) {
      midiCCOut(MIDIosc2FootL, 65);
    }
  }
}

void updateosc2_2SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2", "2 Foot");
  }
  if (osc2_2SWL && lowerSW) {
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, HIGH);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_1SWL = 0;
    osc2_4SWL = 0;
    osc2_8SWL = 0;
    osc2_16SWL = 0;
  }
  if (osc2_2SWU && upperSW) {
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, HIGH);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    osc2_1SWU = 0;
    osc2_4SWU = 0;
    osc2_8SWU = 0;
    osc2_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2_2SWU) {
      midiCCOut(MIDIosc2FootU, 75);
    }
    if (lowerSW && osc2_2SWL) {
      midiCCOut(MIDIosc2FootL, 75);
    }
  }
}

void updateosc2_4SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2", "4 Foot");
  }
  if (osc2_4SWL && lowerSW) {
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, HIGH);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_1SWL = 0;
    osc2_2SWL = 0;
    osc2_8SWL = 0;
    osc2_16SWL = 0;
  }
  if (osc2_4SWU && upperSW) {
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, HIGH);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    osc2_1SWU = 0;
    osc2_2SWU = 0;
    osc2_8SWU = 0;
    osc2_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2_4SWU) {
      midiCCOut(MIDIosc2FootU, 85);
    }
    if (lowerSW && osc2_4SWL) {
      midiCCOut(MIDIosc2FootL, 85);
    }
  }
}

void updateosc2_8SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2", "8 Foot");
  }
  if (osc2_8SWL && lowerSW) {
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, HIGH);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_1SWL = 0;
    osc2_2SWL = 0;
    osc2_4SWL = 0;
    osc2_16SWL = 0;
  }
  if (osc2_8SWU && upperSW) {
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, HIGH);
    sr.writePin(OSC2_16_LED, LOW);
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    osc2_1SWU = 0;
    osc2_2SWU = 0;
    osc2_4SWU = 0;
    osc2_16SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2_8SWU) {
      midiCCOut(MIDIosc2FootU, 97);
    }
    if (lowerSW && osc2_8SWL) {
      midiCCOut(MIDIosc2FootL, 97);
    }
  }
}

void updateosc2_16SW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("OSC2", "16 Foot");
  }
  if (osc2_16SWL && lowerSW) {
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, HIGH);
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, LOW);
    osc2_1SWL = 0;
    osc2_2SWL = 0;
    osc2_4SWL = 0;
    osc2_8SWL = 0;
  }
  if (osc2_16SWU && upperSW) {
    sr.writePin(OSC2_1_LED, LOW);
    sr.writePin(OSC2_2_LED, LOW);
    sr.writePin(OSC2_4_LED, LOW);
    sr.writePin(OSC2_8_LED, LOW);
    sr.writePin(OSC2_16_LED, HIGH);
    green.writePin(GREEN_OSC2_1_LED, LOW);
    green.writePin(GREEN_OSC2_2_LED, LOW);
    green.writePin(GREEN_OSC2_4_LED, LOW);
    green.writePin(GREEN_OSC2_8_LED, LOW);
    green.writePin(GREEN_OSC2_16_LED, LOW);
    osc2_1SWU = 0;
    osc2_2SWU = 0;
    osc2_4SWU = 0;
    osc2_8SWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && osc2_16SWU) {
      midiCCOut(MIDIosc2FootU, 120);
    }
    if (lowerSW && osc2_16SWL) {
      midiCCOut(MIDIosc2FootL, 120);
    }
  }
}

void updateosc1glideSW() {

  if (osc1glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC1_LED, HIGH);
    sr.writePin(GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc1glideL, 127);
    }
    osc1glideSW = 1;
  }
  if (!osc1glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC1_LED, LOW);
    sr.writePin(GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc1glideL, 127);
      }
    }
    osc1glideSW = 0;
  }
  if (osc1glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC1_LED, HIGH);
    green.writePin(GREEN_GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc1glideU, 127);
    }
    osc1glideSW = 1;
  }
  if (!osc1glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC1_LED, LOW);
    green.writePin(GREEN_GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc1glideU, 127);
      }
    }
    osc1glideSW = 0;
  }
}

void updateosc2glideSW() {

  if (osc2glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC2_LED, HIGH);
    sr.writePin(GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc2glideL, 127);
    }
    osc2glideSW = 1;
  }
  if (!osc2glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC2_LED, LOW);
    sr.writePin(GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc2glideL, 127);
      }
    }
    osc2glideSW = 0;
  }
  if (osc2glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC2_LED, HIGH);
    green.writePin(GREEN_GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc2glideU, 127);
    }
    osc2glideSW = 1;
  }
  if (!osc2glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC2_LED, LOW);
    green.writePin(GREEN_GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc2glideU, 127);
      }
    }
    osc2glideSW = 0;
  }
}

void updateportSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Portamento", "On");
  }
  if (portSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_PORTA_LED, HIGH);
    green.writePin(GREEN_GLIDE_GLIDE_LED, LOW);
    sr.writePin(GLIDE_PORTA_LED, LOW);
    sr.writePin(GLIDE_GLIDE_LED, LOW);
    glideSWL = 0;
    glideOffSWL = 0;
  }
  if (portSWU && upperSW) {
    sr.writePin(GLIDE_PORTA_LED, HIGH);
    sr.writePin(GLIDE_GLIDE_LED, LOW);
    green.writePin(GREEN_GLIDE_PORTA_LED, LOW);
    green.writePin(GREEN_GLIDE_GLIDE_LED, LOW);
    glideSWU = 0;
    glideOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && portSWU) {
      midiCCOut(MIDIglideU, 85);
    }
    if (lowerSW && portSWL) {
      midiCCOut(MIDIglideL, 65);
    }
  }
}

void updateglideSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Glide", "On");
  }
  if (glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_PORTA_LED, LOW);
    green.writePin(GREEN_GLIDE_GLIDE_LED, HIGH);
    sr.writePin(GLIDE_PORTA_LED, LOW);
    sr.writePin(GLIDE_GLIDE_LED, LOW);
    portSWL = 0;
    glideOffSWL = 0;
  }
  if (glideSWU && upperSW) {
    sr.writePin(GLIDE_PORTA_LED, LOW);
    sr.writePin(GLIDE_GLIDE_LED, HIGH);
    green.writePin(GREEN_GLIDE_PORTA_LED, LOW);
    green.writePin(GREEN_GLIDE_GLIDE_LED, LOW);
    portSWU = 0;
    glideOffSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && glideSWU) {
      midiCCOut(MIDIglideU, 75);
    }
    if (lowerSW && glideSWL) {
      midiCCOut(MIDIglideL, 75);
    }
  }
}

void updateglideOffSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Glide/Port", "Off");
  }
  if (glideOffSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_PORTA_LED, LOW);
    green.writePin(GREEN_GLIDE_GLIDE_LED, LOW);
    sr.writePin(GLIDE_PORTA_LED, LOW);
    sr.writePin(GLIDE_GLIDE_LED, LOW);
    portSWL = 0;
    glideSWL = 0;
  }
  if (glideOffSWU && upperSW) {
    sr.writePin(GLIDE_PORTA_LED, LOW);
    sr.writePin(GLIDE_GLIDE_LED, LOW);
    green.writePin(GREEN_GLIDE_PORTA_LED, LOW);
    green.writePin(GREEN_GLIDE_GLIDE_LED, LOW);
    portSWU = 0;
    glideSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && glideOffSWU) {
      midiCCOut(MIDIglideU, 65);
    }
    if (lowerSW && glideOffSWL) {
      midiCCOut(MIDIglideL, 85);
    }
  }
}

void updateosc2SyncSW() {

  if (osc2SyncSWL && lowerSW) {
    green.writePin(GREEN_OSC2_SYNC_LED, HIGH);
    sr.writePin(OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc2SyncL, 127);
    }
    osc2SyncSW = 1;
  }
  if (!osc2SyncSWL && lowerSW) {
    green.writePin(GREEN_OSC2_SYNC_LED, LOW);
    sr.writePin(OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc2SyncL, 127);
      }
    }
    osc2SyncSW = 0;
  }
  if (osc2SyncSWU && upperSW) {
    sr.writePin(OSC2_SYNC_LED, HIGH);
    green.writePin(GREEN_OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIosc2SyncU, 127);
    }
    osc2SyncSW = 1;
  }
  if (!osc2SyncSWU && upperSW) {
    sr.writePin(OSC2_SYNC_LED, LOW);
    green.writePin(GREEN_OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIosc2SyncU, 127);
      }
    }
    osc2SyncSW = 0;
  }
}

void updatemultiTriggerSW() {

  if (multiTriggerSWL && lowerSW) {
    green.writePin(GREEN_MULTI_LED, HIGH);
    sr.writePin(MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDImultiTriggerL, 127);
    }
    multiTriggerSW = 1;
  }
  if (!multiTriggerSWL && lowerSW) {
    green.writePin(GREEN_MULTI_LED, LOW);
    sr.writePin(MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDImultiTriggerL, 127);
      }
    }
    multiTriggerSW = 0;
  }
  if (multiTriggerSWU && upperSW) {
    sr.writePin(MULTI_LED, HIGH);
    green.writePin(GREEN_MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDImultiTriggerU, 127);
    }
    multiTriggerSW = 1;
  }
  if (!multiTriggerSWU && upperSW) {
    sr.writePin(MULTI_LED, LOW);
    green.writePin(GREEN_MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDImultiTriggerU, 127);
      }
    }
    multiTriggerSW = 0;
  }
}

void updatechordMemorySW() {

  if (chordMemorySWL && lowerSW) {
    green.writePin(GREEN_CHORD_MEMORY_LED, HIGH);
    sr.writePin(CHORD_MEMORY_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Chord Memory", "Waiting");
      chordMemoryWaitL = true;
      chord_timerL = millis();
    }
    midiCCOut(MIDIchordMemoryL, 127);
    chordMemorySW = 1;
  }
  if (!chordMemorySWL && lowerSW) {
    green.writePin(GREEN_CHORD_MEMORY_LED, LOW);
    sr.writePin(CHORD_MEMORY_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Chord Memory", "Off");
      midiCCOut(MIDIchordMemoryL, 127);
      chordMemoryWaitL = false;
    }
    chordMemorySW = 0;
  }
  if (chordMemorySWU && upperSW) {
    sr.writePin(CHORD_MEMORY_LED, HIGH);
    green.writePin(GREEN_CHORD_MEMORY_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Chord Memory", "Waiting");
      chordMemoryWaitU = true;
      chord_timerU = millis();
    }
    midiCCOut(MIDIchordMemoryU, 127);
    chordMemorySW = 1;
  }
  if (!chordMemorySWU && upperSW) {
    sr.writePin(CHORD_MEMORY_LED, LOW);
    green.writePin(GREEN_CHORD_MEMORY_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Chord Memory", "Off");
      midiCCOut(MIDIchordMemoryU, 127);
      chordMemoryWaitU = false;
    }
    chordMemorySW = 0;
  }
}

void updatesingleSW() {
  if (singleSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Single", "On");
    }
    sr.writePin(SINGLE_LED, HIGH);
    sr.writePin(DOUBLE_LED, LOW);
    sr.writePin(SPLIT_LED, LOW);
    layerSoloSW = 0;
    sr.writePin(LAYER_SOLO_LED, LOW);
    green.writePin(GREEN_LAYER_SOLO_LED, LOW);
    // Blank the split display
    setLEDDisplay2();
    display2.setBacklight(0);
    doubleSW = 0;
    splitSW = 0;
    upperSW = 1;
    lowerSW = 0;
    recallPatchFlag = true;
    learning = false;
    updateUpperSW();
    recallPatchFlag = false;
    midiCCOut(MIDIsingleSW, 65);
  }
}

void updatedoubleSW() {
  if (doubleSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Double", "On");
    }
    sr.writePin(SINGLE_LED, LOW);
    sr.writePin(DOUBLE_LED, HIGH);
    sr.writePin(SPLIT_LED, LOW);
    // Blank the split display
    setLEDDisplay2();
    display2.setBacklight(0);
    singleSW = 0;
    splitSW = 0;
    learning = false;
    midiCCOut(MIDIdoubleSW, 75);
  }
}

void updatesplitSW() {
  if (splitSW && !learning) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Split", "On");
    }
    sr.writePin(SINGLE_LED, LOW);
    sr.writePin(DOUBLE_LED, LOW);
    sr.writePin(SPLIT_LED, HIGH);
    setLEDDisplay2();
    display2.setBacklight(LEDintensity);
    displayLEDNumber(2, splitNote);
    singleSW = 0;
    doubleSW = 0;
    learning = false;
    midiCCOut(MIDIsplitSW, 85);
  }
}

void updatesplitLearn() {
  if (splitSW && learning) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Split", "Learn");
    }
    split_timer = millis();
    midiCCOut(MIDIkeyboard, 85);
  }
}

void updatepolySW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Poly Mode", "On");
  }
  if (polySWL && lowerSW) {
    green.writePin(GREEN_POLY_LED, HIGH);
    green.writePin(GREEN_SINGLE_MONO_LED, LOW);
    green.writePin(GREEN_UNI_MONO_LED, LOW);
    sr.writePin(POLY_LED, LOW);
    sr.writePin(SINGLE_MONO_LED, LOW);
    sr.writePin(UNI_MONO_LED, LOW);
    singleMonoSWL = 0;
    unisonMonoSWL = 0;
  }
  if (polySWU && upperSW) {
    sr.writePin(POLY_LED, HIGH);
    sr.writePin(SINGLE_MONO_LED, LOW);
    sr.writePin(UNI_MONO_LED, LOW);
    green.writePin(GREEN_POLY_LED, LOW);
    green.writePin(GREEN_SINGLE_MONO_LED, LOW);
    green.writePin(GREEN_UNI_MONO_LED, LOW);
    singleMonoSWU = 0;
    unisonMonoSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && polySWU) {
      midiCCOut(MIDIpolyMonoU, 85);
    }
    if (lowerSW && polySWL) {
      midiCCOut(MIDIpolyMonoL, 85);
    }
  }
}

void updatesingleMonoSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Single Mode", "On");
  }
  if (singleMonoSWL && lowerSW) {
    green.writePin(GREEN_POLY_LED, LOW);
    green.writePin(GREEN_SINGLE_MONO_LED, HIGH);
    green.writePin(GREEN_UNI_MONO_LED, LOW);
    sr.writePin(POLY_LED, LOW);
    sr.writePin(SINGLE_MONO_LED, LOW);
    sr.writePin(UNI_MONO_LED, LOW);
    polySWL = 0;
    unisonMonoSWL = 0;
  }
  if (singleMonoSWU && upperSW) {
    sr.writePin(POLY_LED, LOW);
    sr.writePin(SINGLE_MONO_LED, HIGH);
    sr.writePin(UNI_MONO_LED, LOW);
    green.writePin(GREEN_POLY_LED, LOW);
    green.writePin(GREEN_SINGLE_MONO_LED, LOW);
    green.writePin(GREEN_UNI_MONO_LED, LOW);
    polySWU = 0;
    unisonMonoSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && singleMonoSWU) {
      midiCCOut(MIDIpolyMonoU, 75);
    }
    if (lowerSW && singleMonoSWL) {
      midiCCOut(MIDIpolyMonoL, 75);
    }
  }
}

void updateunisonMonoSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Unison Mode", "On");
  }
  if (unisonMonoSWL && lowerSW) {
    green.writePin(GREEN_POLY_LED, LOW);
    green.writePin(GREEN_SINGLE_MONO_LED, LOW);
    green.writePin(GREEN_UNI_MONO_LED, HIGH);
    sr.writePin(POLY_LED, LOW);
    sr.writePin(SINGLE_MONO_LED, LOW);
    sr.writePin(UNI_MONO_LED, LOW);
    polySWL = 0;
    singleMonoSWL = 0;
  }
  if (unisonMonoSWU && upperSW) {
    sr.writePin(POLY_LED, LOW);
    sr.writePin(SINGLE_MONO_LED, LOW);
    sr.writePin(UNI_MONO_LED, HIGH);
    green.writePin(GREEN_POLY_LED, LOW);
    green.writePin(GREEN_SINGLE_MONO_LED, LOW);
    green.writePin(GREEN_UNI_MONO_LED, LOW);
    polySWU = 0;
    singleMonoSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && unisonMonoSWU) {
      midiCCOut(MIDIpolyMonoU, 65);
    }
    if (lowerSW && unisonMonoSWL) {
      midiCCOut(MIDIpolyMonoL, 65);
    }
  }
}

void updatelfo1SyncSW() {

  if (lfo1SyncSWL && lowerSW) {
    green.writePin(GREEN_LFO1_SYNC_LED, HIGH);
    sr.writePin(LFO1_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Sync", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1SyncL, 127);
    }
    lfo1SyncSW = 1;
  }
  if (!lfo1SyncSWL && lowerSW) {
    green.writePin(GREEN_LFO1_SYNC_LED, LOW);
    sr.writePin(LFO1_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Sync", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1SyncL, 127);
      }
    }
    lfo1SyncSW = 0;
  }
  if (lfo1SyncSWU && upperSW) {
    sr.writePin(LFO1_SYNC_LED, HIGH);
    green.writePin(GREEN_LFO1_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Sync", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1SyncU, 127);
    }
    lfo1SyncSW = 1;
  }
  if (!lfo1SyncSWU && upperSW) {
    sr.writePin(LFO1_SYNC_LED, LOW);
    green.writePin(GREEN_LFO1_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Sync", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1SyncU, 127);
      }
    }
    lfo1SyncSW = 0;
  }
}

void updatelfo1modWheelSW() {

  if (lfo1modWheelSWL && lowerSW) {
    green.writePin(GREEN_LFO1_WHEEL_LED, HIGH);
    sr.writePin(LFO1_WHEEL_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Wheel", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1modWheelL, 127);
    }
    lfo1modWheelSW = 1;
  }
  if (!lfo1modWheelSWL && lowerSW) {
    green.writePin(GREEN_LFO1_WHEEL_LED, LOW);
    sr.writePin(LFO1_WHEEL_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Wheel", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1modWheelL, 127);
      }
    }
    lfo1modWheelSW = 0;
  }
  if (lfo1modWheelSWU && upperSW) {
    sr.writePin(LFO1_WHEEL_LED, HIGH);
    green.writePin(GREEN_LFO1_WHEEL_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Wheel", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1modWheelU, 127);
    }
    lfo1modWheelSW = 1;
  }
  if (!lfo1modWheelSWU && upperSW) {
    sr.writePin(LFO1_WHEEL_LED, LOW);
    green.writePin(GREEN_LFO1_WHEEL_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Wheel", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1modWheelU, 127);
      }
    }
    lfo1modWheelSW = 0;
  }
}

void updatelfo2SyncSW() {
  if (lfo2SyncSW) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO2 Sync", "On");
    }
    sr.writePin(LFO2_SYNC_LED, HIGH);
    midiCCOut(MIDIlfo2SyncSW, 127);
    midiCCOut(MIDIlfo2SyncSW, 0);
    lfo2SyncSW = 1;
  }
  if (!lfo2SyncSW) {
    sr.writePin(LFO2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO2 Sync", "Off");
      midiCCOut(MIDIlfo2SyncSW, 127);
      midiCCOut(MIDIlfo2SyncSW, 0);
    }
    lfo2SyncSW = 0;
  }
}


void updatelfo1randSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Wave", "Random");
  }
  if (lfo1randSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RANDOM_LED, HIGH);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    lfo1squareUniSWL = 0;
    lfo1squareBipSWL = 0;
    lfo1sawUpSWL = 0;
    lfo1sawDnSWL = 0;
    lfo1triangleSWL = 0;
  }
  if (lfo1randSWU && upperSW) {
    sr.writePin(LFO1_RANDOM_LED, HIGH);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    lfo1squareUniSWU = 0;
    lfo1squareBipSWU = 0;
    lfo1sawUpSWU = 0;
    lfo1sawDnSWU = 0;
    lfo1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && lfo1randSWU) {
      midiCCOut(MIDIlfo1WaveU, 120);
    }
    if (lowerSW && lfo1randSWL) {
      midiCCOut(MIDIlfo1WaveL, 65);
    }
  }
}

void updatelfo1squareUniSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Wave", "Square Uni");
  }
  if (lfo1squareUniSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, HIGH);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    lfo1randSWL = 0;
    lfo1squareBipSWL = 0;
    lfo1sawUpSWL = 0;
    lfo1sawDnSWL = 0;
    lfo1triangleSWL = 0;
  }
  if (lfo1squareUniSWU && upperSW) {
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, HIGH);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    lfo1randSWU = 0;
    lfo1squareBipSWU = 0;
    lfo1sawUpSWU = 0;
    lfo1sawDnSWU = 0;
    lfo1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && lfo1squareUniSWU) {
      midiCCOut(MIDIlfo1WaveU, 100);
    }
    if (lowerSW && lfo1squareUniSWL) {
      midiCCOut(MIDIlfo1WaveL, 72);
    }
  }
}

void updatelfo1squareBipSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Wave", "Square Bip");
  }
  if (lfo1squareBipSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, HIGH);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    lfo1randSWL = 0;
    lfo1squareUniSWL = 0;
    lfo1sawUpSWL = 0;
    lfo1sawDnSWL = 0;
    lfo1triangleSWL = 0;
  }
  if (lfo1squareBipSWU && upperSW) {
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, HIGH);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    lfo1randSWU = 0;
    lfo1squareUniSWU = 0;
    lfo1sawUpSWU = 0;
    lfo1sawDnSWU = 0;
    lfo1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && lfo1squareBipSWU) {
      midiCCOut(MIDIlfo1WaveU, 88);
    }
    if (lowerSW && lfo1squareBipSWL) {
      midiCCOut(MIDIlfo1WaveL, 80);
    }
  }
}

void updatelfo1sawUpSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Wave", "Saw Up");
  }
  if (lfo1sawUpSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, HIGH);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    lfo1randSWL = 0;
    lfo1squareUniSWL = 0;
    lfo1squareBipSWL = 0;
    lfo1sawDnSWL = 0;
    lfo1triangleSWL = 0;
  }
  if (lfo1sawUpSWU && upperSW) {
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, HIGH);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    lfo1randSWU = 0;
    lfo1squareUniSWU = 0;
    lfo1squareBipSWU = 0;
    lfo1sawDnSWU = 0;
    lfo1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && lfo1sawUpSWU) {
      midiCCOut(MIDIlfo1WaveU, 80);
    }
    if (lowerSW && lfo1sawUpSWL) {
      midiCCOut(MIDIlfo1WaveL, 88);
    }
  }
}

void updatelfo1sawDnSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Wave", "Saw Down");
  }
  if (lfo1sawDnSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, HIGH);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    lfo1randSWL = 0;
    lfo1squareUniSWL = 0;
    lfo1squareBipSWL = 0;
    lfo1sawUpSWL = 0;
    lfo1triangleSWL = 0;
  }
  if (lfo1sawDnSWU && upperSW) {
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, HIGH);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    lfo1randSWU = 0;
    lfo1squareUniSWU = 0;
    lfo1squareBipSWU = 0;
    lfo1sawUpSWU = 0;
    lfo1triangleSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && lfo1sawDnSWU) {
      midiCCOut(MIDIlfo1WaveU, 72);
    }
    if (lowerSW && lfo1sawDnSWL) {
      midiCCOut(MIDIlfo1WaveL, 100);
    }
  }
}

void updatelfo1triangleSW() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO1 Wave", "Triangle");
  }
  if (lfo1triangleSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, HIGH);
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, LOW);
    lfo1randSWL = 0;
    lfo1squareUniSWL = 0;
    lfo1squareBipSWL = 0;
    lfo1sawUpSWL = 0;
    lfo1sawDnSWL = 0;
  }
  if (lfo1triangleSWU && upperSW) {
    sr.writePin(LFO1_RANDOM_LED, LOW);
    sr.writePin(LFO1_SQ_UNIPOLAR_LED, LOW);
    sr.writePin(LFO1_SQ_BIPOLAR_LED, LOW);
    sr.writePin(LFO1_SAW_UP_LED, LOW);
    sr.writePin(LFO1_SAW_DOWN_LED, LOW);
    sr.writePin(LFO1_TRIANGLE_LED, HIGH);
    green.writePin(GREEN_LFO1_RANDOM_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_UNIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SQ_BIPOLAR_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_UP_LED, LOW);
    green.writePin(GREEN_LFO1_SAW_DOWN_LED, LOW);
    green.writePin(GREEN_LFO1_TRIANGLE_LED, LOW);
    lfo1randSWU = 0;
    lfo1squareUniSWU = 0;
    lfo1squareBipSWU = 0;
    lfo1sawUpSWU = 0;
    lfo1sawDnSWU = 0;
  }

  if (!layerPatchFlag) {
    if (upperSW && lfo1triangleSWU) {
      midiCCOut(MIDIlfo1WaveU, 65);
    }
    if (lowerSW && lfo1triangleSWL) {
      midiCCOut(MIDIlfo1WaveL, 120);
    }
  }
}

void updatelfo1resetSW() {

  if (lfo1resetSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RESET_LED, HIGH);
    sr.writePin(LFO1_RESET_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Reset", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1resetL, 127);
    }
    lfo1resetSW = 1;
  }
  if (!lfo1resetSWL && lowerSW) {
    green.writePin(GREEN_LFO1_RESET_LED, LOW);
    sr.writePin(LFO1_RESET_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Reset", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1osc1L, 127);
      }
    }
    lfo1resetSW = 0;
  }
  if (lfo1resetSWU && upperSW) {
    sr.writePin(LFO1_RESET_LED, HIGH);
    green.writePin(GREEN_LFO1_RESET_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Reset", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1resetU, 127);
    }
    lfo1resetSW = 1;
  }
  if (!lfo1resetSWU && upperSW) {
    sr.writePin(LFO1_RESET_LED, LOW);
    green.writePin(GREEN_LFO1_RESET_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Reset", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1resetU, 127);
      }
    }
    lfo1resetSW = 0;
  }
}

void updatelfo1osc1SW() {

  if (lfo1osc1SWL && lowerSW) {
    green.writePin(GREEN_LFO1_OSC1_LED, HIGH);
    sr.writePin(LFO1_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC1", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1osc1L, 127);
    }
    lfo1osc1SW = 1;
  }
  if (!lfo1osc1SWL && lowerSW) {
    green.writePin(GREEN_LFO1_OSC1_LED, LOW);
    sr.writePin(LFO1_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC1", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1osc1L, 127);
      }
    }
    lfo1osc1SW = 0;
  }
  if (lfo1osc1SWU && upperSW) {
    sr.writePin(LFO1_OSC1_LED, HIGH);
    green.writePin(GREEN_LFO1_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC1", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1osc1U, 127);
    }
    lfo1osc1SW = 1;
  }
  if (!lfo1osc1SWU && upperSW) {
    sr.writePin(LFO1_OSC1_LED, LOW);
    green.writePin(GREEN_LFO1_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC1", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1osc1U, 127);
      }
    }
    lfo1osc1SW = 0;
  }
}

void updatelfo1osc2SW() {

  if (lfo1osc2SWL && lowerSW) {
    green.writePin(GREEN_LFO1_OSC2_LED, HIGH);
    sr.writePin(LFO1_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC2", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1osc2L, 127);
    }
    lfo1osc2SW = 1;
  }
  if (!lfo1osc2SWL && lowerSW) {
    green.writePin(GREEN_LFO1_OSC2_LED, LOW);
    sr.writePin(LFO1_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC2", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1osc2L, 127);
      }
    }
    lfo1osc2SW = 0;
  }
  if (lfo1osc2SWU && upperSW) {
    sr.writePin(LFO1_OSC2_LED, HIGH);
    green.writePin(GREEN_LFO1_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC2", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1osc2U, 127);
    }
    lfo1osc2SW = 1;
  }
  if (!lfo1osc2SWU && upperSW) {
    sr.writePin(LFO1_OSC2_LED, LOW);
    green.writePin(GREEN_LFO1_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 OSC2", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1osc2U, 127);
      }
    }
    lfo1osc2SW = 0;
  }
}

void updatelfo1pw1SW() {

  if (lfo1pw1SWL && lowerSW) {
    green.writePin(GREEN_LFO1_PW1_LED, HIGH);
    sr.writePin(LFO1_PW1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW1", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1pw1L, 127);
    }
    lfo1pw1SW = 1;
  }
  if (!lfo1pw1SWL && lowerSW) {
    green.writePin(GREEN_LFO1_PW1_LED, LOW);
    sr.writePin(LFO1_PW1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW1", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1pw1L, 127);
      }
    }
    lfo1pw1SW = 0;
  }
  if (lfo1pw1SWU && upperSW) {
    sr.writePin(LFO1_PW1_LED, HIGH);
    green.writePin(GREEN_LFO1_PW1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW1", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1pw1U, 127);
    }
    lfo1pw1SW = 1;
  }
  if (!lfo1pw1SWU && upperSW) {
    sr.writePin(LFO1_PW1_LED, LOW);
    green.writePin(GREEN_LFO1_PW1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW1", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1pw1U, 127);
      }
    }
    lfo1pw1SW = 0;
  }
}

void updatelfo1pw2SW() {

  if (lfo1pw2SWL && lowerSW) {
    green.writePin(GREEN_LFO1_PW2_LED, HIGH);
    sr.writePin(LFO1_PW2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW2", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1pw2L, 127);
    }
    lfo1pw2SW = 1;
  }
  if (!lfo1pw2SWL && lowerSW) {
    green.writePin(GREEN_LFO1_PW2_LED, LOW);
    sr.writePin(LFO1_PW2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW2", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1pw2L, 127);
      }
    }
    lfo1pw2SW = 0;
  }
  if (lfo1pw2SWU && upperSW) {
    sr.writePin(LFO1_PW2_LED, HIGH);
    green.writePin(GREEN_LFO1_PW2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW2", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1pw2U, 127);
    }
    lfo1pw2SW = 1;
  }
  if (!lfo1pw2SWU && upperSW) {
    sr.writePin(LFO1_PW2_LED, LOW);
    green.writePin(GREEN_LFO1_PW2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 PW2", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1pw2U, 127);
      }
    }
    lfo1pw2SW = 0;
  }
}

void updatelfo1filtSW() {

  if (lfo1filtSWL && lowerSW) {
    green.writePin(GREEN_LFO1_FILT_LED, HIGH);
    sr.writePin(LFO1_FILT_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Filter", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1filtL, 127);
    }
    lfo1filtSW = 1;
  }
  if (!lfo1filtSWL && lowerSW) {
    green.writePin(GREEN_LFO1_FILT_LED, LOW);
    sr.writePin(LFO1_FILT_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Filter", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1filtL, 127);
      }
    }
    lfo1filtSW = 0;
  }
  if (lfo1filtSWU && upperSW) {
    sr.writePin(LFO1_FILT_LED, HIGH);
    green.writePin(GREEN_LFO1_FILT_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Filter", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1filtU, 127);
    }
    lfo1filtSW = 1;
  }
  if (!lfo1filtSWU && upperSW) {
    sr.writePin(LFO1_FILT_LED, LOW);
    green.writePin(GREEN_LFO1_FILT_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 Filter", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1filtU, 127);
      }
    }
    lfo1filtSW = 0;
  }
}

void updatelfo1ampSW() {

  if (lfo1ampSWL && lowerSW) {
    green.writePin(GREEN_LFO1_AMP_LED, HIGH);
    sr.writePin(LFO1_AMP_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 AMP", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1ampL, 127);
    }
    lfo1ampSW = 1;
  }
  if (!lfo1ampSWL && lowerSW) {
    green.writePin(GREEN_LFO1_AMP_LED, LOW);
    sr.writePin(LFO1_AMP_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 AMP", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1ampL, 127);
      }
    }
    lfo1ampSW = 0;
  }
  if (lfo1ampSWU && upperSW) {
    sr.writePin(LFO1_AMP_LED, HIGH);
    green.writePin(GREEN_LFO1_AMP_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 AMP", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1ampU, 127);
    }
    lfo1ampSW = 1;
  }
  if (!lfo1ampSWU && upperSW) {
    sr.writePin(LFO1_AMP_LED, LOW);
    green.writePin(GREEN_LFO1_AMP_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 AMP", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1ampU, 127);
      }
    }
    lfo1ampSW = 0;
  }
}

void updatelfo1seqRateSW() {

  if (lfo1seqRateSWL && lowerSW) {
    green.writePin(GREEN_LFO1_SEQ_RATE_LED, HIGH);
    sr.writePin(LFO1_SEQ_RATE_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 SeqRate", "Lower On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1seqRateL, 127);
    }
    lfo1seqRateSW = 1;
  }
  if (!lfo1seqRateSWL && lowerSW) {
    green.writePin(GREEN_LFO1_SEQ_RATE_LED, LOW);
    sr.writePin(LFO1_SEQ_RATE_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 SeqRate", "Lower Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1seqRateL, 127);
      }
    }
    lfo1seqRateSW = 0;
  }
  if (lfo1seqRateSWU && upperSW) {
    sr.writePin(LFO1_SEQ_RATE_LED, HIGH);
    green.writePin(GREEN_LFO1_SEQ_RATE_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 SeqRate", "Upper On");
    }
    if (!layerPatchFlag) {
      midiCCOut(MIDIlfo1seqRateU, 127);
    }
    lfo1seqRateSW = 1;
  }
  if (!lfo1seqRateSWU && upperSW) {
    sr.writePin(LFO1_SEQ_RATE_LED, LOW);
    green.writePin(GREEN_LFO1_SEQ_RATE_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("LFO1 SeqRate", "Upper Off");
      if (!layerPatchFlag) {
        midiCCOut(MIDIlfo1seqRateU, 127);
      }
    }
    lfo1seqRateSW = 0;
  }
}

void switchLEDs() {
  layerPatchFlag = true;
  updatearpRange1SW();
  updatearpRange2SW();
  updatearpRange3SW();
  updatearpRange4SW();
  updatearpSyncSW();
  updatearpHoldSW();
  updatelayerSoloSW();
  updatearpRandSW();
  updatearpUpDownSW();
  updatearpDownSW();
  updatearpUpSW();
  updatearpOffSW();
  updateenvInvSW();
  updatefilterHPSW();
  updatefilterBP2SW();
  updatefilterBP1SW();
  updatefilterLP2SW();
  updatefilterLP1SW();
  updaterevGLTCSW();
  updaterevHallSW();
  updaterevPlateSW();
  updaterevRoomSW();
  updaterevOffSW();
  updatenoisePinkSW();
  updatenoiseWhiteSW();
  updatenoiseOffSW();
  updateechoSyncSW();
  updateosc1ringModSW();
  updateosc2ringModSW();
  updateosc1_osc2PWMSW();
  updateosc1pulseSW();
  updateosc1squareSW();
  updateosc1sawSW();
  updateosc1triangleSW();
  updateosc2_osc1PWMSW();
  updateosc2pulseSW();
  updateosc2squareSW();
  updateosc2sawSW();
  updateosc2triangleSW();
  updateechoPingPongSW();
  updateechoTapeSW();
  updateechoSTDSW();
  updateechoOffSW();
  updatechorus3SW();
  updatechorus2SW();
  updatechorus1SW();
  updatechorusOffSW();
  updateosc1_1SW();
  updateosc1_2SW();
  updateosc1_4SW();
  updateosc1_8SW();
  updateosc1_16SW();
  updateosc2_1SW();
  updateosc2_2SW();
  updateosc2_4SW();
  updateosc2_8SW();
  updateosc2_16SW();
  updateosc1glideSW();
  updateosc2glideSW();
  updateportSW();
  updateglideSW();
  updateglideOffSW();
  updateosc2SyncSW();
  updatemultiTriggerSW();
  updatepolySW();
  updatesingleMonoSW();
  updateunisonMonoSW();
  updatelfo1SyncSW();
  updatelfo1modWheelSW();
  updatelfo1randSW();
  updatelfo1squareUniSW();
  updatelfo1squareBipSW();
  updatelfo1sawUpSW();
  updatelfo1sawDnSW();
  updatelfo1triangleSW();
  updatelfo1resetSW();
  updatelfo1osc1SW();
  updatelfo1osc2SW();
  updatelfo1pw1SW();
  updatelfo1pw2SW();
  updatelfo1filtSW();
  updatelfo1ampSW();
  updatelfo1seqRateSW();

  layerPatchFlag = false;
}

void updatePatchname() {
  showPatchPage(String(patchNo), patchName);
}

void displayLEDNumber(int displayNumber, int value) {
  if (value > 0 && value <= 9) {
    setCursorPos = 3;
  }
  if (value > 9 && value <= 99) {
    setCursorPos = 2;
  }
  if (value > 99 && value <= 999) {
    setCursorPos = 1;
  }
  if (value < 0) {
    if (value < 0 && value >= -9) {
      setCursorPos = 2;
    }
    if (value < -9 && value >= -99) {
      setCursorPos = 1;
    }
  }

  switch (displayNumber) {
    case 0:
      setLEDDisplay0();
      display0.clear();
      display0.setCursor(0, setCursorPos);
      display0.print(value);
      break;

    case 1:
      setLEDDisplay1();
      display1.clear();
      display1.setCursor(0, setCursorPos);
      display1.print(value);
      break;

    case 2:
      setLEDDisplay2();
      display2.clear();
      display2.setCursor(0, setCursorPos);
      display2.print(value);
      break;

    case 8:
      trilldisplay.clear();
      trilldisplay.setCursor(0, setCursorPos);
      trilldisplay.print(value);
      break;
  }
}

void setLEDDisplay0() {
  digitalWrite(LED_MUX_0, LOW);
  digitalWrite(LED_MUX_1, LOW);
}

void setLEDDisplay1() {
  digitalWrite(LED_MUX_0, HIGH);
  digitalWrite(LED_MUX_1, LOW);
}

void setLEDDisplay2() {
  digitalWrite(LED_MUX_0, LOW);
  digitalWrite(LED_MUX_1, HIGH);
}

void myControlChange(byte channel, byte control, int value) {
  switch (control) {

    case CCmodWheelinput:
      MIDI.sendControlChange(control, value, channel);
      if (sendNotes) {
        usbMIDI.sendControlChange(control, value, channel);
      }
      break;

    case CCmasterVolume:
      masterVolume = value;
      masterVolumestr = SYNTHEXAMOUNT[value];
      updatemasterVolume();
      break;

    case CCmasterTune:
      masterTune = value;
      masterTunestr = SYNTHEXTUNE[value];
      updatemasterTune();
      break;

    case CClayerPan:
      if (lowerSW) {
        layerPanL = value;
      }
      if (upperSW) {
        layerPanU = value;
      }
      layerPanstr = SYNTHEXPAN[value];
      updatelayerPan();
      break;

    case CClayerVolume:
      if (lowerSW) {
        layerVolumeL = value;
      }
      if (upperSW) {
        layerVolumeU = value;
      }
      layerVolumestr = SYNTHEXLAYERVOL[value];
      updatelayerVolume();
      break;

    case CCreverbLevel:
      if (lowerSW) {
        reverbLevelL = value;
      }
      if (upperSW) {
        reverbLevelU = value;
      }
      reverbLevelstr = SYNTHEXAMOUNT[value];
      updatereverbLevel();
      break;

    case CCreverbDecay:
      if (lowerSW) {
        reverbDecayL = value;
      }
      if (upperSW) {
        reverbDecayU = value;
      }
      reverbDecaystr = SYNTHEXAMOUNT[value];
      updatereverbDecay();
      break;

    case CCreverbEQ:
      if (lowerSW) {
        reverbEQL = value;
      }
      if (upperSW) {
        reverbEQU = value;
      }
      reverbEQstr = SYNTHEXEQ[value];
      updatereverbEQ();
      break;

    case CCarpFrequency:
      if (lowerSW) {
        arpFrequencyL = value;
        if (arpSyncSWL) {
          arpFrequencymapL = map(arpFrequencyL, 0, 127, 0, 19);
          arpFrequencystring = SYNTHEXSYNC[arpFrequencymapL];
        } else {
          arpFrequencystr = SYNTHEXARPSPEED[value];
        }
      }
      if (upperSW) {
        arpFrequencyU = value;
        if (arpSyncSWU) {
          arpFrequencymapU = map(arpFrequencyU, 0, 127, 0, 19);
          arpFrequencystring = SYNTHEXSYNC[arpFrequencymapU];
        } else {
          arpFrequencystr = SYNTHEXARPSPEED[value];
        }
      }
      updatearpFrequency();
      break;

    case CCampVelocity:
      if (lowerSW) {
        ampVelocityL = value;
      }
      if (upperSW) {
        ampVelocityU = value;
      }
      ampVelocitystr = SYNTHEXAMOUNT[value];
      updateampVelocity();
      break;

    case CCfilterVelocity:
      if (lowerSW) {
        filterVelocityL = value;
      }
      if (upperSW) {
        filterVelocityU = value;
      }
      filterVelocitystr = SYNTHEXAMOUNT[value];
      updatefilterVelocity();
      break;

    case CCampRelease:
      if (lowerSW) {
        ampReleaseL = value;
      }
      if (upperSW) {
        ampReleaseU = value;
      }
      ampReleasestr = SYNTHEXRELEASE[value];
      updateampRelease();
      break;

    case CCampSustain:
      if (lowerSW) {
        ampSustainL = value;
      }
      if (upperSW) {
        ampSustainU = value;
      }
      ampSustainstr = SYNTHEXAMOUNT[value];
      updateampSustain();
      break;

    case CCampDecay:
      if (lowerSW) {
        ampDecayL = value;
      }
      if (upperSW) {
        ampDecayU = value;
      }
      ampDecaystr = SYNTHEXRELEASE[value];
      updateampDecay();
      break;

    case CCampAttack:
      if (lowerSW) {
        ampAttackL = value;
      }
      if (upperSW) {
        ampAttackU = value;
      }
      ampAttackstr = SYNTHEXATTACK[value];
      updateampAttack();
      break;

    case CCfilterKeyboard:
      if (lowerSW) {
        filterKeyboardL = value;
      }
      if (upperSW) {
        filterKeyboardU = value;
      }
      filterKeyboardstr = SYNTHEXAMOUNT[value];
      updatefilterKeyboard();
      break;

    case CCfilterResonance:
      if (lowerSW) {
        filterResonanceL = value;
      }
      if (upperSW) {
        filterResonanceU = value;
      }
      filterResonancestr = SYNTHEXRESONANCE[value];
      updatefilterResonance();
      break;

    case CCosc2Volume:
      if (lowerSW) {
        osc2VolumeL = value;
      }
      if (upperSW) {
        osc2VolumeU = value;
      }
      osc2Volumestr = SYNTHEXAMOUNT[value];
      updateosc2Volume();
      break;

    case CCosc2PW:
      if (lowerSW) {
        osc2PWL = value;
      }
      if (upperSW) {
        osc2PWU = value;
      }
      osc2PWstr = SYNTHEXAMOUNT[value];
      updateosc2PW();
      break;

    case CCosc1PW:
      if (lowerSW) {
        osc1PWL = value;
      }
      if (upperSW) {
        osc1PWU = value;
      }
      osc1PWstr = SYNTHEXAMOUNT[value];
      updateosc1PW();
      break;

    case CCosc1Volume:
      if (lowerSW) {
        osc1VolumeL = value;
      }
      if (upperSW) {
        osc1VolumeU = value;
      }
      osc1Volumestr = SYNTHEXAMOUNT[value];
      updateosc1Volume();
      break;

    case CCfilterCutoff:
      if (lowerSW) {
        filterCutoffL = value;
      }
      if (upperSW) {
        filterCutoffU = value;
      }
      filterCutoffstr = SYNTHEXCUTOFF[value];
      updatefilterCutoff();
      break;

    case CCfilterEnvAmount:
      if (lowerSW) {
        filterEnvAmountL = value;
      }
      if (upperSW) {
        filterEnvAmountU = value;
      }
      filterEnvAmountstr = SYNTHEXAMOUNT[value];
      updatefilterEnvAmount();
      break;

    case CCfilterAttack:
      if (lowerSW) {
        filterAttackL = value;
      }
      if (upperSW) {
        filterAttackU = value;
      }
      filterAttackstr = SYNTHEXATTACK[value];
      updatefilterAttack();
      break;

    case CCfilterDecay:
      if (lowerSW) {
        filterDecayL = value;
      }
      if (upperSW) {
        filterDecayU = value;
      }
      filterDecaystr = SYNTHEXRELEASE[value];
      updatefilterDecay();
      break;

    case CCfilterSustain:
      if (lowerSW) {
        filterSustainL = value;
      }
      if (upperSW) {
        filterSustainU = value;
      }
      filterSustainstr = SYNTHEXAMOUNT[value];
      updatefilterSustain();
      break;

    case CCfilterRelease:
      if (lowerSW) {
        filterReleaseL = value;
      }
      if (upperSW) {
        filterReleaseU = value;
      }
      filterReleasestr = SYNTHEXRELEASE[value];
      updatefilterRelease();
      break;

    case CCechoEQ:
      if (lowerSW) {
        echoEQL = value;
      }
      if (upperSW) {
        echoEQU = value;
      }
      echoEQstr = SYNTHEXEQ[value];
      updateechoEQ();
      break;

    case CCechoLevel:
      if (lowerSW) {
        echoLevelL = value;
      }
      if (upperSW) {
        echoLevelU = value;
      }
      echoLevelstr = SYNTHEXAMOUNT[value];
      updateechoLevel();
      break;

    case CCechoFeedback:
      if (lowerSW) {
        echoFeedbackL = value;
      }
      if (upperSW) {
        echoFeedbackU = value;
      }
      echoFeedbackstr = SYNTHEXAMOUNT[value];
      updateechoFeedback();
      break;

    case CCechoSpread:
      if (lowerSW) {
        echoSpreadL = value;
      }
      if (upperSW) {
        echoSpreadU = value;
      }
      echoSpreadstr = SYNTHEXAMOUNT[value];
      updateechoSpread();
      break;

    case CCechoTime:
      if (lowerSW) {
        echoTimeL = value;
        if (echoSyncSWL) {
          echoTimemapL = map(echoTimeL, 0, 127, 0, 19);
          echoTimestring = SYNTHEXECHOSYNC[echoTimemapL];
        } else {
          echoTimestr = SYNTHEXECHOTIME[value];
        }
      }
      if (upperSW) {
        echoTimeU = value;
        if (echoSyncSWU) {
          echoTimemapU = map(echoTimeU, 0, 127, 0, 19);
          echoTimestring = SYNTHEXECHOSYNC[echoTimemapU];
        } else {
          echoTimestr = SYNTHEXECHOTIME[value];
        }
      }
      updateechoTime();
      break;

    case CClfo2UpperLower:
      lfo2Destination = value;
      lfo2Destinationstr = map(lfo2Destination, 0, 127, 0, 2);
      updatelfo2Destination();
      break;

    case CCunisonDetune:
      if (lowerSW) {
        unisonDetuneL = value;
      }
      if (upperSW) {
        unisonDetuneU = value;
      }
      unisonDetunestr = SYNTHEXAMOUNT[value];
      updateunisonDetune();
      break;

    case CCglideSpeed:
      if (lowerSW) {
        glideSpeedL = value;
      }
      if (upperSW) {
        glideSpeedU = value;
      }
      glideSpeedstr = SYNTHEXGLIDE[value];
      updateglideSpeed();
      break;

    case CCosc1Transpose:
      if (lowerSW) {
        osc1TransposeL = value;
      }
      if (upperSW) {
        osc1TransposeU = value;
      }
      osc1Transposestr = SYNTHEXTRANSPOSE[value];
      updateosc1Transpose();
      break;

    case CCosc2Transpose:
      if (lowerSW) {
        osc2TransposeL = value;
      }
      if (upperSW) {
        osc2TransposeU = value;
      }
      osc2Transposestr = SYNTHEXTRANSPOSE[value];
      updateosc2Transpose();
      break;

    case CCnoiseLevel:
      if (lowerSW) {
        noiseLevelL = value;
      }
      if (upperSW) {
        noiseLevelU = value;
      }
      noiseLevelstr = SYNTHEXAMOUNT[value];
      updatenoiseLevel();
      break;

    case CCglideAmount:
      if (lowerSW) {
        glideAmountL = value;
      }
      if (upperSW) {
        glideAmountU = value;
      }
      glideAmountstr = SYNTHEXGLIDEAMOUNT[value];
      updateglideAmount();
      break;

    case CCosc1Tune:
      if (lowerSW) {
        osc1TuneL = value;
      }
      if (upperSW) {
        osc1TuneU = value;
      }
      osc1Tunestr = SYNTHEXOSCTUNE[value];
      updateosc1Tune();
      break;

    case CCosc2Tune:
      if (lowerSW) {
        osc2TuneL = value;
      }
      if (upperSW) {
        osc2TuneU = value;
      }
      osc2Tunestr = SYNTHEXOSCTUNE[value];
      updateosc2Tune();
      break;

    case CCbendToFilter:
      bendToFilter = value;
      bendToFilterstr = SYNTHEXAMOUNT[value];
      updatebendToFilter();
      break;

    case CClfo2ToFilter:
      lfo2ToFilter = value;
      lfo2ToFilterstr = SYNTHEXAMOUNT[value];
      updatelfo2ToFilter();
      break;

    case CCbendToOsc:
      bendToOsc = value;
      bendToOscstr = SYNTHEXBENDTOOSC[value];
      updatebendToOsc();
      break;

    case CClfo2ToOsc:
      lfo2ToOsc = value;
      lfo2ToOscstr = SYNTHEXAMOUNT[value];
      updatelfo2ToOsc();
      break;

    case CClfo2FreqAcc:
      lfo2FreqAcc = value;
      lfo2FreqAccstr = SYNTHEXLFO2ACCEL[value];
      updatelfo2FreqAcc();
      break;

    case CClfo2InitFrequency:
      lfo2InitFrequency = value;
      if (lfo2SyncSW) {
        lfo2InitFrequencymap = map(lfo2InitFrequency, 0, 127, 0, 19);
        lfo2InitFrequencystring = SYNTHEXSYNC[lfo2InitFrequencymap];
      } else {
        lfo2InitFrequencystr = SYNTHEXLFO2[value];
      }
      updatelfo2InitFrequency();
      break;

    case CClfo2InitAmount:
      lfo2InitAmount = value;
      lfo2InitAmountstr = SYNTHEXAMOUNT[value];
      updatelfo2InitAmount();
      break;

    case CCseqAssign:
      seqAssign = value;
      seqAssignstr = map(seqAssign, 0, 127, 0, 1);
      updateseqAssign();
      break;

    case CCseqRate:
      seqRate = value;
      seqRatestr = value;
      updateseqRate();
      break;

    case CCseqGate:
      seqGate = value;
      seqGatestr = value;
      updateseqGate();
      break;

    case CClfo1Frequency:
      if (lowerSW) {
        lfo1FrequencyL = value;
        if (lfo1SyncSWL) {
          lfo1FrequencymapL = map(lfo1FrequencyL, 0, 127, 0, 19);
          lfo1Frequencystring = SYNTHEXSYNC[lfo1FrequencymapL];
        } else {
          lfo1Frequencystr = SYNTHEXLFO1[value];
        }
      }
      if (upperSW) {
        lfo1FrequencyU = value;
        if (lfo1SyncSWU) {
          lfo1FrequencymapU = map(lfo1FrequencyU, 0, 127, 0, 19);
          lfo1Frequencystring = SYNTHEXSYNC[lfo1FrequencymapU];
        } else {
          lfo1Frequencystr = SYNTHEXLFO1[value];
        }
      }
      updatelfo1Frequency();
      break;

    case CClfo1DepthA:
      if (lowerSW) {
        lfo1DepthAL = value;
      }
      if (upperSW) {
        lfo1DepthAU = value;
      }
      lfo1DepthAstr = SYNTHEX100[value];
      updatelfo1DepthA();
      break;

    case CClfo1Delay:
      if (lowerSW && !lfo1modWheelSWL) {
        lfo1DelayL = value;
        lfo1Delaystr = SYNTHEXLFO1DELAY[value];
        updatelfo1Delay();
      }
      if (upperSW && !lfo1modWheelSWU) {
        lfo1DelayU = value;
        lfo1Delaystr = SYNTHEXLFO1DELAY[value];
        updatelfo1Delay();
      }
      break;

    case CClfo1DepthB:
      if (lowerSW) {
        lfo1DepthBL = value;
      }
      if (upperSW) {
        lfo1DepthBU = value;
      }
      lfo1DepthBstr = SYNTHEX100[value];
      updatelfo1DepthB();
      break;

    case CCarpRange4SW:
      if (lowerSW) {
        arpRange4SWL = 1;
      }
      if (upperSW) {
        arpRange4SWU = 1;
      }
      updatearpRange4SW();
      break;

    case CCarpRange3SW:
      if (lowerSW) {
        arpRange3SWL = 1;
      }
      if (upperSW) {
        arpRange3SWU = 1;
      }
      updatearpRange3SW();
      break;

    case CCarpRange2SW:
      if (lowerSW) {
        arpRange2SWL = 1;
      }
      if (upperSW) {
        arpRange2SWU = 1;
      }
      updatearpRange2SW();
      break;

    case CCarpRange1SW:
      if (lowerSW) {
        arpRange1SWL = 1;
      }
      if (upperSW) {
        arpRange1SWU = 1;
      }
      updatearpRange1SW();
      break;

    case CCarpSyncSW:
      if (lowerSW) {
        value > 0 ? arpSyncSWL = 1 : arpSyncSWL = 0;
      }
      if (upperSW) {
        value > 0 ? arpSyncSWU = 1 : arpSyncSWU = 0;
      }
      updatearpSyncSW();
      break;

    case CCarpHoldSW:
      if (lowerSW) {
        value > 0 ? arpHoldSWL = 1 : arpHoldSWL = 0;
      }
      if (upperSW) {
        value > 0 ? arpHoldSWU = 1 : arpHoldSWU = 0;
      }
      updatearpHoldSW();
      break;

    case CClayerSoloSW:
      value > 0 ? layerSoloSW = 1 : layerSoloSW = 0;
      updatelayerSoloSW();
      break;

    case CCarpRandSW:
      if (lowerSW) {
        arpRandSWL = 1;
      }
      if (upperSW) {
        arpRandSWU = 1;
      }
      updatearpRandSW();
      break;

    case CCarpUpDownSW:
      if (lowerSW) {
        arpUpDownSWL = 1;
      }
      if (upperSW) {
        arpUpDownSWU = 1;
      }
      updatearpUpDownSW();
      break;

    case CCarpDownSW:
      if (lowerSW) {
        arpDownSWL = 1;
      }
      if (upperSW) {
        arpDownSWU = 1;
      }
      updatearpDownSW();
      break;

    case CCarpUpSW:
      if (lowerSW) {
        arpUpSWL = 1;
      }
      if (upperSW) {
        arpUpSWU = 1;
      }
      updatearpUpSW();
      break;

    case CCarpOffSW:
      if (lowerSW) {
        arpOffSWL = 1;
      }
      if (upperSW) {
        arpOffSWU = 1;
      }
      updatearpOffSW();
      break;

    case CCenvInvSW:
      if (lowerSW) {
        value > 0 ? envInvSWL = 1 : envInvSWL = 0;
      }
      if (upperSW) {
        value > 0 ? envInvSWU = 1 : envInvSWU = 0;
      }
      updateenvInvSW();
      break;

    case CCfilterHPSW:
      if (lowerSW) {
        filterHPSWL = 1;
      }
      if (upperSW) {
        filterHPSWU = 1;
      }
      updatefilterHPSW();
      break;

    case CCfilterBP2SW:
      if (lowerSW) {
        filterBP2SWL = 1;
      }
      if (upperSW) {
        filterBP2SWU = 1;
      }
      updatefilterBP2SW();
      break;

    case CCfilterBP1SW:
      if (lowerSW) {
        filterBP1SWL = 1;
      }
      if (upperSW) {
        filterBP1SWU = 1;
      }
      updatefilterBP1SW();
      break;

    case CCfilterLP2SW:
      if (lowerSW) {
        filterLP2SWL = 1;
      }
      if (upperSW) {
        filterLP2SWU = 1;
      }
      updatefilterLP2SW();
      break;

    case CCfilterLP1SW:
      if (lowerSW) {
        filterLP1SWL = 1;
      }
      if (upperSW) {
        filterLP1SWU = 1;
      }
      updatefilterLP1SW();
      break;

    case CCrevGLTCSW:
      if (lowerSW) {
        revGLTCSWL = 1;
      }
      if (upperSW) {
        revGLTCSWU = 1;
      }
      updaterevGLTCSW();
      break;

    case CCrevHallSW:
      if (lowerSW) {
        revHallSWL = 1;
      }
      if (upperSW) {
        revHallSWU = 1;
      }
      updaterevHallSW();
      break;

    case CCrevPlateSW:
      if (lowerSW) {
        revPlateSWL = 1;
      }
      if (upperSW) {
        revPlateSWU = 1;
      }
      updaterevPlateSW();
      break;

    case CCrevRoomSW:
      if (lowerSW) {
        revRoomSWL = 1;
      }
      if (upperSW) {
        revRoomSWU = 1;
      }
      updaterevRoomSW();
      break;

    case CCrevOffSW:
      if (lowerSW) {
        revOffSWL = 1;
      }
      if (upperSW) {
        revOffSWU = 1;
      }
      updaterevOffSW();
      break;

    case CCnoisePinkSW:
      if (lowerSW) {
        noisePinkSWL = 1;
      }
      if (upperSW) {
        noisePinkSWU = 1;
      }
      updatenoisePinkSW();
      break;

    case CCnoiseWhiteSW:
      if (lowerSW) {
        noiseWhiteSWL = 1;
      }
      if (upperSW) {
        noiseWhiteSWU = 1;
      }
      updatenoiseWhiteSW();
      break;

    case CCnoiseOffSW:
      if (lowerSW) {
        noiseOffSWL = 1;
      }
      if (upperSW) {
        noiseOffSWU = 1;
      }
      updatenoiseOffSW();
      break;

    case CCechoSyncSW:
      if (lowerSW) {
        value > 0 ? echoSyncSWL = 1 : echoSyncSWL = 0;
      }
      if (upperSW) {
        value > 0 ? echoSyncSWU = 1 : echoSyncSWU = 0;
      }
      updateechoSyncSW();
      break;

    case CCosc1ringModSW:
      if (lowerSW) {
        value > 0 ? osc1ringModSWL = 1 : osc1ringModSWL = 0;
      }
      if (upperSW) {
        value > 0 ? osc1ringModSWU = 1 : osc1ringModSWU = 0;
      }
      updateosc1ringModSW();
      break;

    case CCosc2ringModSW:
      if (lowerSW) {
        value > 0 ? osc2ringModSWL = 1 : osc2ringModSWL = 0;
      }
      if (upperSW) {
        value > 0 ? osc2ringModSWU = 1 : osc2ringModSWU = 0;
      }
      updateosc2ringModSW();
      break;

    case CCosc1_osc2PWMSW:
      if (lowerSW) {
        osc1_osc2PWMSWL = 1;
      }
      if (upperSW) {
        osc1_osc2PWMSWU = 1;
      }
      updateosc1_osc2PWMSW();
      break;

    case CCosc1pulseSW:
      if (lowerSW) {
        osc1pulseSWL = 1;
      }
      if (upperSW) {
        osc1pulseSWU = 1;
      }
      updateosc1pulseSW();
      break;

    case CCosc1squareSW:
      if (lowerSW) {
        osc1squareSWL = 1;
      }
      if (upperSW) {
        osc1squareSWU = 1;
      }
      updateosc1squareSW();
      break;

    case CCosc1sawSW:
      if (lowerSW) {
        osc1sawSWL = 1;
      }
      if (upperSW) {
        osc1sawSWU = 1;
      }
      updateosc1sawSW();
      break;

    case CCosc1triangleSW:
      if (lowerSW) {
        osc1triangleSWL = 1;
      }
      if (upperSW) {
        osc1triangleSWU = 1;
      }
      updateosc1triangleSW();
      break;

    case CCosc2_osc1PWMSW:
      if (lowerSW) {
        osc2_osc1PWMSWL = 1;
      }
      if (upperSW) {
        osc2_osc1PWMSWU = 1;
      }
      updateosc2_osc1PWMSW();
      break;

    case CCosc2pulseSW:
      if (lowerSW) {
        osc2pulseSWL = 1;
      }
      if (upperSW) {
        osc2pulseSWU = 1;
      }
      updateosc2pulseSW();
      break;

    case CCosc2squareSW:
      if (lowerSW) {
        osc2squareSWL = 1;
      }
      if (upperSW) {
        osc2squareSWU = 1;
      }
      updateosc2squareSW();
      break;

    case CCosc2sawSW:
      if (lowerSW) {
        osc2sawSWL = 1;
      }
      if (upperSW) {
        osc2sawSWU = 1;
      }
      updateosc2sawSW();
      break;

    case CCosc2triangleSW:
      if (lowerSW) {
        osc2triangleSWL = 1;
      }
      if (upperSW) {
        osc2triangleSWU = 1;
      }
      updateosc2triangleSW();
      break;

    case CCechoPingPongSW:
      if (lowerSW) {
        echoPingPongSWL = 1;
      }
      if (upperSW) {
        echoPingPongSWU = 1;
      }
      updateechoPingPongSW();
      break;

    case CCechoTapeSW:
      if (lowerSW) {
        echoTapeSWL = 1;
      }
      if (upperSW) {
        echoTapeSWU = 1;
      }
      updateechoTapeSW();
      break;

    case CCechoSTDSW:
      if (lowerSW) {
        echoSTDSWL = 1;
      }
      if (upperSW) {
        echoSTDSWU = 1;
      }
      updateechoSTDSW();
      break;

    case CCechoOffSW:
      if (lowerSW) {
        echoOffSWL = 1;
      }
      if (upperSW) {
        echoOffSWU = 1;
      }
      updateechoOffSW();
      break;

    case CCchorus3SW:
      if (lowerSW) {
        chorus3SWL = 1;
      }
      if (upperSW) {
        chorus3SWU = 1;
      }
      updatechorus3SW();
      break;

    case CCchorus2SW:
      if (lowerSW) {
        chorus2SWL = 1;
      }
      if (upperSW) {
        chorus2SWU = 1;
      }
      updatechorus2SW();
      break;

    case CCchorus1SW:
      if (lowerSW) {
        chorus1SWL = 1;
      }
      if (upperSW) {
        chorus1SWU = 1;
      }
      updatechorus1SW();
      break;

    case CCchorusOffSW:
      if (lowerSW) {
        chorusOffSWL = 1;
      }
      if (upperSW) {
        chorusOffSWU = 1;
      }
      updatechorusOffSW();
      break;

    case CCosc1_1SW:
      if (lowerSW) {
        osc1_1SWL = 1;
      }
      if (upperSW) {
        osc1_1SWU = 1;
      }
      updateosc1_1SW();
      break;

    case CCosc1_2SW:
      if (lowerSW) {
        osc1_2SWL = 1;
      }
      if (upperSW) {
        osc1_2SWU = 1;
      }
      updateosc1_2SW();
      break;

    case CCosc1_4SW:
      if (lowerSW) {
        osc1_4SWL = 1;
      }
      if (upperSW) {
        osc1_4SWU = 1;
      }
      updateosc1_4SW();
      break;

    case CCosc1_8SW:
      if (lowerSW) {
        osc1_8SWL = 1;
      }
      if (upperSW) {
        osc1_8SWU = 1;
      }
      updateosc1_8SW();
      break;

    case CCosc1_16SW:
      if (lowerSW) {
        osc1_16SWL = 1;
      }
      if (upperSW) {
        osc1_16SWU = 1;
      }
      updateosc1_16SW();
      break;

    case CCosc2_1SW:
      if (lowerSW) {
        osc2_1SWL = 1;
      }
      if (upperSW) {
        osc2_1SWU = 1;
      }
      updateosc2_1SW();
      break;

    case CCosc2_2SW:
      if (lowerSW) {
        osc2_2SWL = 1;
      }
      if (upperSW) {
        osc2_2SWU = 1;
      }
      updateosc2_2SW();
      break;

    case CCosc2_4SW:
      if (lowerSW) {
        osc2_4SWL = 1;
      }
      if (upperSW) {
        osc2_4SWU = 1;
      }
      updateosc2_4SW();
      break;

    case CCosc2_8SW:
      if (lowerSW) {
        osc2_8SWL = 1;
      }
      if (upperSW) {
        osc2_8SWU = 1;
      }
      updateosc2_8SW();
      break;

    case CCosc2_16SW:
      if (lowerSW) {
        osc2_16SWL = 1;
      }
      if (upperSW) {
        osc2_16SWU = 1;
      }
      updateosc2_16SW();
      break;

    case CCosc1glideSW:
      if (lowerSW) {
        value > 0 ? osc1glideSWL = 1 : osc1glideSWL = 0;
      }
      if (upperSW) {
        value > 0 ? osc1glideSWU = 1 : osc1glideSWU = 0;
      }
      updateosc1glideSW();
      break;

    case CCosc2glideSW:
      if (lowerSW) {
        value > 0 ? osc2glideSWL = 1 : osc2glideSWL = 0;
      }
      if (upperSW) {
        value > 0 ? osc2glideSWU = 1 : osc2glideSWU = 0;
      }
      updateosc2glideSW();
      break;

    case CCportSW:
      if (lowerSW) {
        portSWL = 1;
      }
      if (upperSW) {
        portSWU = 1;
      }
      updateportSW();
      break;

    case CCglideSW:
      if (lowerSW) {
        glideSWL = 1;
      }
      if (upperSW) {
        glideSWU = 1;
      }
      updateglideSW();
      break;

    case CCglideOffSW:
      if (lowerSW) {
        glideOffSWL = 1;
      }
      if (upperSW) {
        glideOffSWU = 1;
      }
      updateglideOffSW();
      break;

    case CCosc2SyncSW:
      if (lowerSW) {
        value > 0 ? osc2SyncSWL = 1 : osc2SyncSWL = 0;
      }
      if (upperSW) {
        value > 0 ? osc2SyncSWU = 1 : osc2SyncSWU = 0;
      }
      updateosc2SyncSW();
      break;

    case CCmultiTriggerSW:
      if (lowerSW) {
        value > 0 ? multiTriggerSWL = 1 : multiTriggerSWL = 0;
      }
      if (upperSW) {
        value > 0 ? multiTriggerSWU = 1 : multiTriggerSWU = 0;
      }
      updatemultiTriggerSW();
      break;

    case CCchordMemorySW:
      if (lowerSW) {
        value > 0 ? chordMemorySWL = 1 : chordMemorySWL = 0;
      }
      if (upperSW) {
        value > 0 ? chordMemorySWU = 1 : chordMemorySWU = 0;
      }
      updatechordMemorySW();
      break;

    case CCsingleSW:
      value > 0 ? singleSW = 1 : singleSW = 0;
      updatesingleSW();
      break;

    case CCdoubleSW:
      value > 0 ? doubleSW = 1 : doubleSW = 0;
      updatedoubleSW();
      break;

    case CCsplitSW:
      value > 0 ? splitSW = 1 : splitSW = 0;
      updatesplitSW();
      break;

    case CCsplitLearning:
      value > 0 ? splitSW = 1 : splitSW = 0;
      updatesplitLearn();
      break;

    case CCpolySW:
      if (lowerSW) {
        polySWL = 1;
      }
      if (upperSW) {
        polySWU = 1;
      }
      updatepolySW();
      break;

    case CCsingleMonoSW:
      if (lowerSW) {
        singleMonoSWL = 1;
      }
      if (upperSW) {
        singleMonoSWU = 1;
      }
      updatesingleMonoSW();
      break;

    case CCunisonMonoSW:
      if (lowerSW) {
        unisonMonoSWL = 1;
      }
      if (upperSW) {
        unisonMonoSWU = 1;
      }
      updateunisonMonoSW();
      break;

    case CClfo1SyncSW:
      if (lowerSW) {
        value > 0 ? lfo1SyncSWL = 1 : lfo1SyncSWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1SyncSWU = 1 : lfo1SyncSWU = 0;
      }
      updatelfo1SyncSW();
      break;

    case CClfo1modWheelSW:
      if (lowerSW) {
        value > 0 ? lfo1modWheelSWL = 1 : lfo1modWheelSWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1modWheelSWU = 1 : lfo1modWheelSWU = 0;
      }
      updatelfo1modWheelSW();
      break;

    case CClfo2SyncSW:
      value > 0 ? lfo2SyncSW = 1 : lfo2SyncSW = 0;
      updatelfo2SyncSW();
      break;

    case CClfo1randSW:
      if (lowerSW) {
        lfo1randSWL = 1;
      }
      if (upperSW) {
        lfo1randSWU = 1;
      }
      updatelfo1randSW();
      break;

    case CClfo1squareUniSW:
      if (lowerSW) {
        lfo1squareUniSWL = 1;
      }
      if (upperSW) {
        lfo1squareUniSWU = 1;
      }
      updatelfo1squareUniSW();
      break;

    case CClfo1squareBipSW:
      if (lowerSW) {
        lfo1squareBipSWL = 1;
      }
      if (upperSW) {
        lfo1squareBipSWU = 1;
      }
      updatelfo1squareBipSW();
      break;

    case CClfo1sawUpSW:
      if (lowerSW) {
        lfo1sawUpSWL = 1;
      }
      if (upperSW) {
        lfo1sawUpSWU = 1;
      }
      updatelfo1sawUpSW();
      break;

    case CClfo1sawDnSW:
      if (lowerSW) {
        lfo1sawDnSWL = 1;
      }
      if (upperSW) {
        lfo1sawDnSWU = 1;
      }
      updatelfo1sawDnSW();
      break;

    case CClfo1triangleSW:
      if (lowerSW) {
        lfo1triangleSWL = 1;
      }
      if (upperSW) {
        lfo1triangleSWU = 1;
      }
      updatelfo1triangleSW();
      break;

    case CClfo1resetSW:
      if (lowerSW) {
        value > 0 ? lfo1resetSWL = 1 : lfo1resetSWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1resetSWU = 1 : lfo1resetSWU = 0;
      }
      updatelfo1resetSW();
      break;

    case CClfo1osc1SW:
      if (lowerSW) {
        value > 0 ? lfo1osc1SWL = 1 : lfo1osc1SWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1osc1SWU = 1 : lfo1osc1SWU = 0;
      }
      updatelfo1osc1SW();
      break;

    case CClfo1osc2SW:
      if (lowerSW) {
        value > 0 ? lfo1osc2SWL = 1 : lfo1osc2SWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1osc2SWU = 1 : lfo1osc2SWU = 0;
      }
      updatelfo1osc2SW();
      break;

    case CClfo1pw1SW:
      if (lowerSW) {
        value > 0 ? lfo1pw1SWL = 1 : lfo1pw1SWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1pw1SWU = 1 : lfo1pw1SWU = 0;
      }
      updatelfo1pw1SW();
      break;

    case CClfo1pw2SW:
      if (lowerSW) {
        value > 0 ? lfo1pw2SWL = 1 : lfo1pw2SWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1pw2SWU = 1 : lfo1pw2SWU = 0;
      }
      updatelfo1pw2SW();
      break;

    case CClfo1filtSW:
      if (lowerSW) {
        value > 0 ? lfo1filtSWL = 1 : lfo1filtSWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1filtSWU = 1 : lfo1filtSWU = 0;
      }
      updatelfo1filtSW();
      break;

    case CClfo1ampSW:
      if (lowerSW) {
        value > 0 ? lfo1ampSWL = 1 : lfo1ampSWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1ampSWU = 1 : lfo1ampSWU = 0;
      }
      updatelfo1ampSW();
      break;

    case CClfo1seqRateSW:
      if (lowerSW) {
        value > 0 ? lfo1seqRateSWL = 1 : lfo1seqRateSWL = 0;
      }
      if (upperSW) {
        value > 0 ? lfo1seqRateSWU = 1 : lfo1seqRateSWU = 0;
      }
      updatelfo1seqRateSW();
      break;

    case CCseqPlaySW:
      value > 0 ? seqPlaySW = 1 : seqPlaySW = 0;
      updateseqPlaySW();
      break;

    case CCseqStopSW:
      value > 0 ? seqStopSW = 1 : seqStopSW = 0;
      updateseqStopSW();
      break;

    case CCseqKeySW:
      value > 0 ? seqKeySW = 1 : seqKeySW = 0;
      updateseqKeySW();
      break;

    case CCseqTransSW:
      value > 0 ? seqTransSW = 1 : seqTransSW = 0;
      updateseqTransSW();
      break;

    case CCseqLoopSW:
      value > 0 ? seqLoopSW = 1 : seqLoopSW = 0;
      updateseqLoopSW();
      break;

    case CCseqFwSW:
      value > 0 ? seqFwSW = 1 : seqFwSW = 0;
      updateseqFwSW();
      break;

    case CCseqBwSW:
      value > 0 ? seqBwSW = 1 : seqBwSW = 0;
      updateseqBwSW();
      break;

    case CCseqEnable1SW:
      value > 0 ? seqEnable1SW = 1 : seqEnable1SW = 0;
      updateseqEnable1SW();
      break;

    case CCseqEnable2SW:
      value > 0 ? seqEnable2SW = 1 : seqEnable2SW = 0;
      updateseqEnable2SW();
      break;

    case CCseqEnable3SW:
      value > 0 ? seqEnable3SW = 1 : seqEnable3SW = 0;
      updateseqEnable3SW();
      break;

    case CCseqEnable4SW:
      value > 0 ? seqEnable4SW = 1 : seqEnable4SW = 0;
      updateseqEnable4SW();
      break;

    case CCseqSyncSW:
      value > 0 ? seqSyncSW = 1 : seqSyncSW = 0;
      updateseqSyncSW();
      break;

    case CCseqrecEditSW:
      value > 0 ? seqrecEditSW = 1 : seqrecEditSW = 0;
      updateseqrecEditSW();
      break;

    case CCseqinsStepSW:
      value > 0 ? seqinsStepSW = 1 : seqinsStepSW = 0;
      updateseqinsStepSW();
      break;

    case CCseqdelStepSW:
      value > 0 ? seqdelStepSW = 1 : seqdelStepSW = 0;
      updateseqdelStepSW();
      break;

    case CCseqaddStepSW:
      value > 0 ? seqaddStepSW = 1 : seqaddStepSW = 0;
      updateseqaddStepSW();
      break;

    case CCseqRestSW:
      value > 0 ? seqRestSW = 1 : seqRestSW = 0;
      updateseqRestSW();
      break;

    case CCseqUtilSW:
      value > 0 ? seqUtilSW = 1 : seqUtilSW = 0;
      updateseqUtilSW();
      break;

    case CCmaxVoicesSW:
      value > 0 ? maxVoicesSW = 1 : maxVoicesSW = 0;
      updatemaxVoicesSW();
      break;

    case CCmaxVoicesExitSW:
      value > 0 ? maxVoicesExitSW = 1 : maxVoicesExitSW = 0;
      updatemaxVoicesExitSW();
      break;

    case CClimiterSW:
      value > 0 ? limiterSW = 1 : limiterSW = 0;
      updatelimiterSW();
      break;

    case CClowerSW:
      value > 0 ? lowerSW = 1 : lowerSW = 0;
      updateLowerSW();
      break;

    case CCupperSW:
      value > 0 ? upperSW = 1 : upperSW = 0;
      updateUpperSW();
      break;

    case CCutilitySW:
      updateUtilitySW();
      break;

    case CCutilityAction:
      updateUtilityAction();
      break;


    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program) {
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  Serial.print("MIDI Pgm Change:");
  Serial.println(patchNo);
  state = PARAMETER;
}

void recallPatch(int patchNo) {
  allNotesOff();
  MIDI.sendProgramChange(0, midiOutCh);
  delay(50);
  recallPatchFlag = true;
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile) {
    Serial.println("File not found");
  } else {
    String data[NO_OF_PARAMS];  //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
  }
}

void setCurrentPatchData(String data[]) {
  patchName = data[0];

  // Pots

  masterVolume = data[1].toInt();
  masterTune = data[2].toInt();
  layerPanU = data[3].toInt();
  layerVolumeL = data[4].toInt();
  layerVolumeU = data[5].toInt();
  reverbLevelL = data[6].toInt();
  reverbLevelU = data[7].toInt();
  reverbDecayL = data[8].toInt();
  reverbDecayU = data[9].toInt();
  reverbEQL = data[10].toInt();
  reverbEQU = data[11].toInt();
  arpFrequencyL = data[12].toInt();
  arpFrequencyU = data[13].toInt();
  ampVelocityL = data[14].toInt();
  ampVelocityU = data[15].toInt();
  filterVelocityL = data[16].toInt();
  filterVelocityU = data[17].toInt();
  ampReleaseL = data[18].toInt();
  ampReleaseU = data[19].toInt();
  ampSustainL = data[20].toInt();
  ampSustainU = data[21].toInt();
  ampDecayL = data[22].toInt();
  ampDecayU = data[23].toInt();
  ampAttackL = data[24].toInt();
  ampAttackU = data[25].toInt();
  filterKeyboardL = data[26].toInt();
  filterKeyboardU = data[27].toInt();
  filterResonanceL = data[28].toInt();
  filterResonanceU = data[29].toInt();
  osc2VolumeL = data[30].toInt();
  osc2VolumeU = data[31].toInt();
  osc2PWL = data[32].toInt();
  osc2PWU = data[33].toInt();
  osc1PWL = data[34].toInt();
  osc1PWU = data[35].toFloat();
  osc1VolumeL = data[36].toInt();
  osc1VolumeU = data[37].toInt();
  filterCutoffL = data[38].toInt();
  filterCutoffU = data[39].toFloat();
  filterEnvAmountL = data[40].toInt();
  filterEnvAmountU = data[41].toInt();
  filterAttackL = data[42].toInt();
  filterAttackU = data[43].toInt();
  filterDecayL = data[44].toInt();
  filterDecayU = data[45].toInt();
  filterSustainL = data[46].toInt();
  filterSustainU = data[47].toInt();
  filterReleaseL = data[48].toInt();
  filterReleaseU = data[49].toInt();
  echoEQL = data[50].toInt();
  echoEQU = data[51].toInt();
  echoLevelL = data[52].toInt();
  echoLevelU = data[53].toInt();
  echoFeedbackL = data[54].toInt();
  echoFeedbackU = data[55].toInt();
  echoSpreadL = data[56].toInt();
  echoSpreadU = data[57].toInt();
  echoTimeL = data[58].toInt();
  echoTimeU = data[59].toInt();
  lfo2Destination = data[60].toInt();
  unisonDetuneL = data[61].toInt();
  unisonDetuneU = data[62].toInt();
  glideSpeedL = data[63].toInt();
  glideSpeedU = data[64].toInt();
  osc1TransposeL = data[65].toInt();
  osc1TransposeU = data[66].toInt();
  osc2TransposeL = data[67].toInt();
  osc2TransposeU = data[68].toInt();
  noiseLevelL = data[69].toInt();
  noiseLevelU = data[70].toInt();
  glideAmountL = data[71].toInt();
  glideAmountU = data[72].toInt();
  osc1TuneL = data[73].toInt();
  osc1TuneU = data[74].toInt();
  osc2TuneL = data[75].toInt();
  osc2TuneU = data[76].toInt();
  bendToFilter = data[77].toInt();
  lfo2ToFilter = data[78].toInt();
  bendToOsc = data[79].toInt();
  lfo2ToOsc = data[80].toInt();
  lfo2FreqAcc = data[81].toInt();
  lfo2InitFrequency = data[82].toInt();
  lfo2InitAmount = data[83].toInt();
  seqAssign = data[84].toInt();
  seqRate = data[85].toInt();
  seqGate = data[86].toInt();
  lfo1FrequencyL = data[87].toInt();
  lfo1FrequencyU = data[88].toInt();
  lfo1DepthAL = data[89].toInt();
  lfo1DepthAU = data[90].toInt();
  lfo1DelayL = data[91].toInt();
  lfo1DelayU = data[92].toInt();
  lfo1DepthBL = data[93].toInt();
  lfo1DepthBU = data[94].toInt();

  //Switches
  arpRange4SWL = data[95].toInt();
  arpRange4SWU = data[96].toInt();
  arpRange3SWL = data[97].toInt();
  arpRange3SWU = data[98].toInt();
  arpRange2SWL = data[99].toInt();
  arpRange2SWU = data[100].toInt();
  arpRange1SWL = data[101].toInt();
  arpRange1SWU = data[102].toInt();
  arpSyncSWL = data[103].toInt();
  arpSyncSWU = data[104].toInt();
  arpHoldSWL = data[105].toInt();
  arpHoldSWU = data[106].toInt();
  arpRandSWL = data[107].toInt();
  arpRandSWU = data[108].toInt();
  arpUpDownSWL = data[109].toInt();
  arpUpDownSWU = data[110].toInt();
  arpDownSWL = data[111].toInt();
  arpDownSWU = data[112].toInt();
  arpUpSWL = data[113].toInt();
  arpUpSWU = data[114].toInt();
  arpOffSWL = data[115].toInt();
  arpOffSWU = data[116].toInt();
  envInvSWL = data[117].toInt();
  envInvSWU = data[118].toInt();
  filterHPSWL = data[119].toInt();
  filterHPSWU = data[120].toInt();
  filterBP2SWL = data[121].toInt();
  filterBP2SWU = data[122].toInt();
  filterBP1SWL = data[123].toInt();
  filterBP1SWU = data[124].toInt();
  filterLP2SWL = data[125].toInt();
  filterLP2SWU = data[126].toInt();
  filterLP1SWL = data[127].toInt();
  filterLP1SWU = data[128].toInt();
  revGLTCSWL = data[129].toInt();
  revGLTCSWU = data[130].toInt();
  revHallSWL = data[131].toInt();
  revHallSWU = data[132].toInt();
  revPlateSWL = data[133].toInt();
  revPlateSWU = data[134].toInt();
  revRoomSWL = data[135].toInt();
  revRoomSWU = data[136].toInt();
  revOffSWL = data[137].toInt();
  revOffSWU = data[138].toInt();
  noisePinkSWL = data[139].toInt();
  noisePinkSWU = data[140].toInt();
  noiseWhiteSWL = data[141].toInt();
  noiseWhiteSWU = data[142].toInt();
  noiseOffSWL = data[143].toInt();
  noiseOffSWU = data[144].toInt();
  echoSyncSWL = data[145].toInt();
  echoSyncSWU = data[146].toInt();
  osc1ringModSWL = data[147].toInt();
  osc1ringModSWU = data[148].toInt();
  osc2ringModSWL = data[149].toInt();
  osc2ringModSWU = data[150].toInt();
  osc1_osc2PWMSWL = data[151].toInt();
  osc1_osc2PWMSWU = data[152].toInt();
  osc1pulseSWL = data[153].toInt();
  osc1pulseSWU = data[154].toInt();
  osc1squareSWL = data[155].toInt();
  osc1squareSWU = data[156].toInt();
  osc1sawSWL = data[157].toInt();
  osc1sawSWU = data[158].toInt();
  osc1triangleSWL = data[159].toInt();
  osc1triangleSWU = data[160].toInt();
  osc2_osc1PWMSWL = data[161].toInt();
  osc2_osc1PWMSWU = data[162].toInt();
  osc2pulseSWL = data[163].toInt();
  osc2pulseSWU = data[164].toInt();
  osc2squareSWL = data[165].toInt();
  osc2squareSWU = data[166].toInt();
  osc2sawSWL = data[167].toInt();
  osc2sawSWU = data[168].toInt();
  osc2triangleSWL = data[169].toInt();
  osc2triangleSWU = data[170].toInt();
  echoPingPongSWL = data[171].toInt();
  echoPingPongSWU = data[172].toInt();
  echoTapeSWL = data[173].toInt();
  echoTapeSWU = data[174].toInt();
  echoSTDSWL = data[175].toInt();
  echoSTDSWU = data[176].toInt();
  echoOffSWL = data[177].toInt();
  echoOffSWU = data[178].toInt();
  chorus3SWL = data[179].toInt();
  chorus3SWU = data[180].toInt();
  chorus2SWL = data[181].toInt();
  chorus2SWU = data[182].toInt();
  chorus1SWL = data[183].toInt();
  chorus1SWU = data[184].toInt();
  chorusOffSWL = data[185].toInt();
  chorusOffSWU = data[186].toInt();
  osc1_1SWL = data[187].toInt();
  osc1_1SWU = data[188].toInt();
  osc1_2SWL = data[189].toInt();
  osc1_2SWU = data[190].toInt();
  osc1_4SWL = data[191].toInt();
  osc1_4SWU = data[192].toInt();
  osc1_8SWL = data[193].toInt();
  osc1_8SWU = data[194].toInt();
  osc1_16SWL = data[195].toInt();
  osc1_16SWU = data[196].toInt();
  osc2_1SWL = data[197].toInt();
  osc2_1SWU = data[198].toInt();
  osc2_2SWL = data[199].toInt();
  osc2_2SWU = data[200].toInt();
  osc2_4SWL = data[201].toInt();
  osc2_4SWU = data[202].toInt();
  osc2_8SWL = data[203].toInt();
  osc2_8SWU = data[204].toInt();
  osc2_16SWL = data[205].toInt();
  osc2_16SWU = data[206].toInt();
  osc1glideSWL = data[207].toInt();
  osc1glideSWU = data[208].toInt();
  osc2glideSWL = data[209].toInt();
  osc2glideSWU = data[210].toInt();
  portSWL = data[211].toInt();
  portSWU = data[212].toInt();
  glideSWL = data[213].toInt();
  glideSWU = data[214].toInt();
  glideOffSWL = data[215].toInt();
  glideOffSWU = data[216].toInt();
  osc2SyncSWL = data[217].toInt();
  osc2SyncSWU = data[218].toInt();
  multiTriggerSWL = data[219].toInt();
  multiTriggerSWU = data[220].toInt();
  singleSW = data[221].toInt();
  doubleSW = data[222].toInt();
  splitSW = data[223].toInt();
  polySWL = data[224].toInt();
  polySWU = data[225].toInt();
  singleMonoSWL = data[226].toInt();
  singleMonoSWU = data[227].toInt();
  unisonMonoSWL = data[228].toInt();
  unisonMonoSWU = data[229].toInt();
  lfo1SyncSWL = data[230].toInt();
  lfo1SyncSWU = data[231].toInt();
  lfo1modWheelSWL = data[232].toInt();
  lfo1modWheelSWU = data[233].toInt();
  lfo2SyncSW = data[234].toInt();
  lfo1randSWL = data[235].toInt();
  lfo1randSWU = data[236].toInt();
  lfo1resetSWL = data[237].toInt();
  lfo1resetSWU = data[238].toInt();
  lfo1osc1SWL = data[239].toInt();
  lfo1osc1SWU = data[240].toInt();
  lfo1osc2SWL = data[241].toInt();
  lfo1osc2SWU = data[242].toInt();
  lfo1pw1SWL = data[243].toInt();
  lfo1pw1SWU = data[244].toInt();
  lfo1pw2SWL = data[245].toInt();
  lfo1pw2SWU = data[246].toInt();
  lfo1filtSWL = data[247].toInt();
  lfo1filtSWU = data[248].toInt();
  lfo1ampSWL = data[249].toInt();
  lfo1ampSWU = data[250].toInt();
  lfo1seqRateSWL = data[251].toInt();
  lfo1seqRateSWU = data[252].toInt();
  lfo1squareUniSWL = data[253].toInt();
  lfo1squareUniSWU = data[254].toInt();
  lfo1squareBipSWL = data[255].toInt();
  lfo1squareBipSWU = data[256].toInt();
  lfo1sawUpSWL = data[257].toInt();
  lfo1sawUpSWU = data[258].toInt();
  lfo1sawDnSWL = data[259].toInt();
  lfo1sawDnSWU = data[260].toInt();
  lfo1triangleSWL = data[261].toInt();
  lfo1triangleSWU = data[262].toInt();
  layerPanL = data[263].toInt();
  limiterSW = data[264].toInt();
  splitNote = data[265].toInt();
  maxVoices = data[266].toInt();
  seqPlaySW = data[267].toInt();
  seqKeySW = data[268].toInt();
  seqTransSW = data[269].toInt();
  seqLoopSW = data[270].toInt();
  seqSyncSW = data[271].toInt();
  seqEnable1SW = data[272].toInt();
  seqEnable2SW = data[273].toInt();
  seqEnable3SW = data[274].toInt();
  seqEnable4SW = data[275].toInt();

  updateEverything();
  recallPatchFlag = false;

  //Patchname
  updatePatchname();

  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

String getCurrentPatchData() {
  return patchName + "," + String(masterVolume) + "," + String(masterTune) + "," + String(layerPanU) + "," + String(layerVolumeL) + "," + String(layerVolumeU) + "," + String(reverbLevelL)
         + "," + String(reverbLevelU) + "," + String(reverbDecayL) + "," + String(reverbDecayU) + "," + String(reverbEQL) + "," + String(reverbEQU) + "," + String(arpFrequencyL)
         + "," + String(arpFrequencyU) + "," + String(ampVelocityL) + "," + String(ampVelocityU) + "," + String(filterVelocityL) + "," + String(filterVelocityU) + "," + String(ampReleaseL)
         + "," + String(ampReleaseU) + "," + String(ampSustainL) + "," + String(ampSustainU) + "," + String(ampDecayL) + "," + String(ampDecayU) + "," + String(ampAttackL)
         + "," + String(ampAttackU) + "," + String(filterKeyboardL) + "," + String(filterKeyboardU) + "," + String(filterResonanceL) + "," + String(filterResonanceU) + "," + String(osc2VolumeL)
         + "," + String(osc2VolumeU) + "," + String(osc2PWL) + "," + String(osc2PWU) + "," + String(osc1PWL) + "," + String(osc1PWU) + "," + String(osc1VolumeL)
         + "," + String(osc1VolumeU) + "," + String(filterCutoffL) + "," + String(filterCutoffU) + "," + String(filterEnvAmountL) + "," + String(filterEnvAmountU) + "," + String(filterAttackL)
         + "," + String(filterAttackU) + "," + String(filterDecayL) + "," + String(filterDecayU) + "," + String(filterSustainL) + "," + String(filterSustainU) + "," + String(filterReleaseL)
         + "," + String(filterReleaseU) + "," + String(echoEQL) + "," + String(echoEQU) + "," + String(echoLevelL) + "," + String(echoLevelU) + "," + String(echoFeedbackL)
         + "," + String(echoFeedbackU) + "," + String(echoSpreadL) + "," + String(echoSpreadU) + "," + String(echoTimeL) + "," + String(echoTimeU) + "," + String(lfo2Destination)
         + "," + String(unisonDetuneL) + "," + String(unisonDetuneU) + "," + String(glideSpeedL) + "," + String(glideSpeedU) + "," + String(osc1TransposeL) + "," + String(osc1TransposeU)
         + "," + String(osc2TransposeL) + "," + String(osc2TransposeU) + "," + String(noiseLevelL) + "," + String(noiseLevelU) + "," + String(glideAmountL) + "," + String(glideAmountU)
         + "," + String(osc1TuneL) + "," + String(osc1TuneU) + "," + String(osc2TuneL) + "," + String(osc2TuneU) + "," + String(bendToFilter) + "," + String(lfo2ToFilter)
         + "," + String(bendToOsc) + "," + String(lfo2ToOsc) + "," + String(lfo2FreqAcc) + "," + String(lfo2InitFrequency) + "," + String(lfo2InitAmount) + "," + String(seqAssign)
         + "," + String(seqRate) + "," + String(seqGate) + "," + String(lfo1FrequencyL) + "," + String(lfo1FrequencyU) + "," + String(lfo1DepthAL) + "," + String(lfo1DepthAU)
         + "," + String(lfo1DelayL) + "," + String(lfo1DelayU) + "," + String(lfo1DepthBL) + "," + String(lfo1DepthBU) + "," + String(arpRange4SWL) + "," + String(arpRange4SWU)
         + "," + String(arpRange3SWL) + "," + String(arpRange3SWU) + "," + String(arpRange2SWL) + "," + String(arpRange2SWU) + "," + String(arpRange1SWL) + "," + String(arpRange1SWU)
         + "," + String(arpSyncSWL) + "," + String(arpSyncSWU) + "," + String(arpHoldSWL) + "," + String(arpHoldSWU) + "," + String(arpRandSWL) + "," + String(arpRandSWU)
         + "," + String(arpUpDownSWL) + "," + String(arpUpDownSWU) + "," + String(arpDownSWL) + "," + String(arpDownSWU) + "," + String(arpUpSWL) + "," + String(arpUpSWU)
         + "," + String(arpOffSWL) + "," + String(arpOffSWU) + "," + String(envInvSWL) + "," + String(envInvSWU) + "," + String(filterHPSWL) + "," + String(filterHPSWU)
         + "," + String(filterBP2SWL) + "," + String(filterBP2SWU) + "," + String(filterBP1SWL) + "," + String(filterBP1SWU) + "," + String(filterLP2SWL) + "," + String(filterLP2SWU)
         + "," + String(filterLP1SWL) + "," + String(filterLP1SWU) + "," + String(revGLTCSWL) + "," + String(revGLTCSWU) + "," + String(revHallSWL) + "," + String(revHallSWU)
         + "," + String(revPlateSWL) + "," + String(revPlateSWU) + "," + String(revRoomSWL) + "," + String(revRoomSWU) + "," + String(revOffSWL) + "," + String(revOffSWU)
         + "," + String(noisePinkSWL) + "," + String(noisePinkSWU) + "," + String(noiseWhiteSWL) + "," + String(noiseWhiteSWU) + "," + String(noiseOffSWL) + "," + String(noiseOffSWU)
         + "," + String(echoSyncSWL) + "," + String(echoSyncSWU) + "," + String(osc1ringModSWL) + "," + String(osc1ringModSWU) + "," + String(osc2ringModSWL) + "," + String(osc2ringModSWU)
         + "," + String(osc1_osc2PWMSWL) + "," + String(osc1_osc2PWMSWU) + "," + String(osc1pulseSWL) + "," + String(osc1pulseSWU) + "," + String(osc1squareSWL) + "," + String(osc1squareSWU)
         + "," + String(osc1sawSWL) + "," + String(osc1sawSWU) + "," + String(osc1triangleSWL) + "," + String(osc1triangleSWU) + "," + String(osc2_osc1PWMSWL) + "," + String(osc2_osc1PWMSWU)
         + "," + String(osc2pulseSWL) + "," + String(osc2pulseSWU) + "," + String(osc2squareSWL) + "," + String(osc2squareSWU) + "," + String(osc2sawSWL) + "," + String(osc2sawSWU)
         + "," + String(osc2triangleSWL) + "," + String(osc2triangleSWU) + "," + String(echoPingPongSWL) + "," + String(echoPingPongSWU) + "," + String(echoTapeSWL) + "," + String(echoTapeSWU)
         + "," + String(echoSTDSWL) + "," + String(echoSTDSWU) + "," + String(echoOffSWL) + "," + String(echoOffSWU) + "," + String(chorus3SWL) + "," + String(chorus3SWU)
         + "," + String(chorus2SWL) + "," + String(chorus2SWU) + "," + String(chorus1SWL) + "," + String(chorus1SWU) + "," + String(chorusOffSWL) + "," + String(chorusOffSWU)
         + "," + String(osc1_1SWL) + "," + String(osc1_1SWU) + "," + String(osc1_2SWL) + "," + String(osc1_2SWU) + "," + String(osc1_4SWL) + "," + String(osc1_4SWU)
         + "," + String(osc1_8SWL) + "," + String(osc1_8SWU) + "," + String(osc1_16SWL) + "," + String(osc1_16SWU) + "," + String(osc2_1SWL) + "," + String(osc2_1SWU)
         + "," + String(osc2_2SWL) + "," + String(osc2_2SWU) + "," + String(osc2_4SWL) + "," + String(osc2_4SWU) + "," + String(osc2_8SWL) + "," + String(osc2_8SWU)
         + "," + String(osc2_16SWL) + "," + String(osc2_16SWU) + "," + String(osc1glideSWL) + "," + String(osc1glideSWU) + "," + String(osc2glideSWL) + "," + String(osc2glideSWU)
         + "," + String(portSWL) + "," + String(portSWU) + "," + String(glideSWL) + "," + String(glideSWU) + "," + String(glideOffSWL) + "," + String(glideOffSWU)
         + "," + String(osc2SyncSWL) + "," + String(osc2SyncSWU) + "," + String(multiTriggerSWL) + "," + String(multiTriggerSWU) + "," + String(singleSW) + "," + String(doubleSW)
         + "," + String(splitSW) + "," + String(polySWL) + "," + String(polySWU) + "," + String(singleMonoSWL) + "," + String(singleMonoSWU) + "," + String(unisonMonoSWL)
         + "," + String(unisonMonoSWU) + "," + String(lfo1SyncSWL) + "," + String(lfo1SyncSWU) + "," + String(lfo1modWheelSWL) + "," + String(lfo1modWheelSWU) + "," + String(lfo2SyncSW)
         + "," + String(lfo1randSWL) + "," + String(lfo1randSWU) + "," + String(lfo1resetSWL) + "," + String(lfo1resetSWU) + "," + String(lfo1osc1SWL) + "," + String(lfo1osc1SWU)
         + "," + String(lfo1osc2SWL) + "," + String(lfo1osc2SWU) + "," + String(lfo1pw1SWL) + "," + String(lfo1pw1SWU) + "," + String(lfo1pw2SWL) + "," + String(lfo1pw2SWU)
         + "," + String(lfo1filtSWL) + "," + String(lfo1filtSWU) + "," + String(lfo1ampSWL) + "," + String(lfo1ampSWU) + "," + String(lfo1seqRateSWL) + "," + String(lfo1seqRateSWU)
         + "," + String(lfo1squareUniSWL) + "," + String(lfo1squareUniSWU) + "," + String(lfo1squareBipSWL) + "," + String(lfo1squareBipSWU) + "," + String(lfo1sawUpSWL) + "," + String(lfo1sawUpSWU)
         + "," + String(lfo1sawDnSWL) + "," + String(lfo1sawDnSWU) + "," + String(lfo1triangleSWL) + "," + String(lfo1triangleSWU) + "," + String(layerPanL) + "," + String(limiterSW)
         + "," + String(splitNote) + "," + String(maxVoices) + "," + String(seqPlaySW) + "," + String(seqKeySW) + "," + String(seqTransSW) + "," + String(seqLoopSW)
         + "," + String(seqSyncSW) + "," + String(seqEnable1SW) + "," + String(seqEnable2SW) + "," + String(seqEnable3SW) + "," + String(seqEnable4SW);
}

void updateEverything() {
  //Pots
  recallPatchFlag = true;
  lowerSW = true;
  upperSW = false;

  updatelayerPan();
  updatelayerVolume();
  updatefilterCutoff();
  updatefilterResonance();
  updatefilterEnvAmount();
  updatefilterKeyboard();
  updatelfo1Frequency();
  updatelfo1Delay();
  updatelfo1DepthA();
  updatelfo1DepthB();
  updateunisonDetune();
  updateosc1Transpose();
  updateosc1Tune();
  updateosc1PW();
  updateosc1Volume();
  updateosc2Transpose();
  updateosc2Tune();
  updateosc2PW();
  updateosc2Volume();
  updatefilterAttack();
  updatefilterDecay();
  updatefilterSustain();
  updatefilterRelease();
  updatefilterVelocity();
  updateampAttack();
  updateampDecay();
  updateampSustain();
  updateampRelease();
  updateampVelocity();
  updatenoiseLevel();
  updateglideSpeed();
  updateglideAmount();
  updatearpFrequency();
  updateechoTime();
  updateechoFeedback();
  updateechoEQ();
  updateechoSpread();
  updateechoLevel();
  updatereverbDecay();
  updatereverbEQ();
  updatereverbLevel();

  //Switches

  updatearpRange4SW();
  updatearpRange3SW();
  updatearpRange2SW();
  updatearpRange1SW();
  updatearpSyncSW();
  updatearpHoldSW();
  updatelayerSoloSW();
  updatearpRandSW();
  updatearpUpDownSW();
  updatearpDownSW();
  updatearpUpSW();
  updatearpOffSW();
  updateenvInvSW();
  updatefilterHPSW();
  updatefilterBP2SW();
  updatefilterBP1SW();
  updatefilterLP2SW();
  updatefilterLP1SW();
  updatenoisePinkSW();
  updatenoiseWhiteSW();
  updatenoiseOffSW();
  updateosc1ringModSW();
  updateosc2ringModSW();
  updateosc1_1SW();
  updateosc1_2SW();
  updateosc1_4SW();
  updateosc1_8SW();
  updateosc1_16SW();
  updateosc2_1SW();
  updateosc2_2SW();
  updateosc2_4SW();
  updateosc2_8SW();
  updateosc2_16SW();
  updateosc1glideSW();
  updateosc2glideSW();
  updateportSW();
  updateglideSW();
  updateglideOffSW();
  updateosc2SyncSW();
  updatemultiTriggerSW();
  updatepolySW();
  updatesingleMonoSW();
  updateunisonMonoSW();
  updatelfo1SyncSW();
  updatelfo1modWheelSW();
  updatelfo1randSW();
  updatelfo1squareUniSW();
  updatelfo1squareBipSW();
  updatelfo1sawUpSW();
  updatelfo1sawDnSW();
  updatelfo1triangleSW();
  updatelfo1resetSW();
  updatelfo1osc1SW();
  updatelfo1osc2SW();
  updatelfo1pw1SW();
  updatelfo1pw2SW();
  updatelfo1filtSW();
  updatelfo1ampSW();
  updatelfo1seqRateSW();
  updateosc1_osc2PWMSW();
  updateosc1pulseSW();
  updateosc1squareSW();
  updateosc1sawSW();
  updateosc1triangleSW();
  updateosc2_osc1PWMSW();
  updateosc2pulseSW();
  updateosc2squareSW();
  updateosc2sawSW();
  updateosc2triangleSW();
  updateechoSyncSW();
  updateechoPingPongSW();
  updateechoTapeSW();
  updateechoSTDSW();
  updateechoOffSW();
  updatechorus3SW();
  updatechorus2SW();
  updatechorus1SW();
  updatechorusOffSW();
  updaterevGLTCSW();
  updaterevHallSW();
  updaterevPlateSW();
  updaterevRoomSW();
  updaterevOffSW();

  lowerSW = false;
  upperSW = true;
  delay(10);

  updatelayerPan();
  updatelayerVolume();
  updatefilterCutoff();
  updatefilterResonance();
  updatefilterEnvAmount();
  updatefilterKeyboard();
  updatelfo1Frequency();
  updatelfo1Delay();
  updatelfo1DepthA();
  updatelfo1DepthB();
  updateunisonDetune();
  updateosc1Transpose();
  updateosc1Tune();
  updateosc1PW();
  updateosc1Volume();
  updateosc2Transpose();
  updateosc2Tune();
  updateosc2PW();
  updateosc2Volume();
  updatefilterAttack();
  updatefilterDecay();
  updatefilterSustain();
  updatefilterRelease();
  updatefilterVelocity();
  updateampAttack();
  updateampDecay();
  updateampSustain();
  updateampRelease();
  updateampVelocity();
  updatenoiseLevel();
  updateglideSpeed();
  updateglideAmount();
  updatearpFrequency();
  updateechoTime();
  updateechoFeedback();
  updateechoEQ();
  updateechoSpread();
  updateechoLevel();
  updatereverbDecay();
  updatereverbEQ();
  updatereverbLevel();

  delay(10);
  //Switches

  updatearpRange4SW();
  updatearpRange3SW();
  updatearpRange2SW();
  updatearpRange1SW();
  updatearpSyncSW();
  updatearpHoldSW();
  updatelayerSoloSW();
  updatearpRandSW();
  updatearpUpDownSW();
  updatearpDownSW();
  updatearpUpSW();
  updatearpOffSW();
  updateenvInvSW();
  updatefilterHPSW();
  updatefilterBP2SW();
  updatefilterBP1SW();
  updatefilterLP2SW();
  updatefilterLP1SW();
  updatenoisePinkSW();
  updatenoiseWhiteSW();
  updatenoiseOffSW();
  updateosc1ringModSW();
  updateosc2ringModSW();
  updateosc1_1SW();
  updateosc1_2SW();
  updateosc1_4SW();
  updateosc1_8SW();
  updateosc1_16SW();
  updateosc2_1SW();
  updateosc2_2SW();
  updateosc2_4SW();
  updateosc2_8SW();
  updateosc2_16SW();
  updateosc1glideSW();
  updateosc2glideSW();
  updateportSW();
  updateglideSW();
  updateglideOffSW();
  updateosc2SyncSW();
  updatemultiTriggerSW();
  updatepolySW();
  updatesingleMonoSW();
  updateunisonMonoSW();
  updatelfo1SyncSW();
  updatelfo1modWheelSW();
  updatelfo1randSW();
  updatelfo1squareUniSW();
  updatelfo1squareBipSW();
  updatelfo1sawUpSW();
  updatelfo1sawDnSW();
  updatelfo1triangleSW();
  updatelfo1resetSW();
  updatelfo1osc1SW();
  updatelfo1osc2SW();
  updatelfo1pw1SW();
  updatelfo1pw2SW();
  updatelfo1filtSW();
  updatelfo1ampSW();
  updatelfo1seqRateSW();
  updateosc1_osc2PWMSW();
  updateosc1pulseSW();
  updateosc1squareSW();
  updateosc1sawSW();
  updateosc1triangleSW();
  updateosc2_osc1PWMSW();
  updateosc2pulseSW();
  updateosc2squareSW();
  updateosc2sawSW();
  updateosc2triangleSW();
  updateechoSyncSW();
  updateechoPingPongSW();
  updateechoTapeSW();
  updateechoSTDSW();
  updateechoOffSW();
  updatechorus3SW();
  updatechorus2SW();
  updatechorus1SW();
  updatechorusOffSW();
  updaterevGLTCSW();
  updaterevHallSW();
  updaterevPlateSW();
  updaterevRoomSW();
  updaterevOffSW();

  // common controls
  delay(10);

  updatelfo2SyncSW();
  updatelfo2InitAmount();
  updatelfo2InitFrequency();
  updatelfo2FreqAcc();
  updatelfo2ToOsc();
  updatebendToOsc();
  updatelfo2ToFilter();
  updatebendToFilter();
  updatemasterTune();
  updatelimiterSW();
  updatemasterVolume();
  updatemasterTune();
  updateLowerSW();
  updateUpperSW();
  updatesingleSW();
  updatedoubleSW();
  updatesplitSW();
  updatemaxVoicesSW();
  updateseqSyncSW();
  updateseqLoopSW();
  updateseqPlaySW();
  updateseqKeySW();
  updateseqTransSW();
  updateseqEnable1SW();
  updateseqEnable2SW();
  updateseqEnable3SW();
  updateseqEnable4SW();

  recallPatchFlag = false;
}

void checkMux() {

  mux1Read = adc->adc1->analogRead(MUX1_S);
  mux2Read = adc->adc1->analogRead(MUX2_S);
  mux3Read = adc->adc1->analogRead(MUX3_S);
  mux4Read = adc->adc0->analogRead(MUX4_S);

  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;
    mux1Read = (mux1Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX1_masterVolume:
        myControlChange(midiChannel, CCmasterVolume, mux1Read);
        break;
      case MUX1_masterTune:
        myControlChange(midiChannel, CCmasterTune, mux1Read);
        break;
      case MUX1_layerPan:
        myControlChange(midiChannel, CClayerPan, mux1Read);
        break;
      case MUX1_layerVolume:
        myControlChange(midiChannel, CClayerVolume, mux1Read);
        break;
      case MUX1_reverbLevel:
        myControlChange(midiChannel, CCreverbLevel, mux1Read);
        break;
      case MUX1_reverbDecay:
        myControlChange(midiChannel, CCreverbDecay, mux1Read);
        break;
      case MUX1_reverbEQ:
        myControlChange(midiChannel, CCreverbEQ, mux1Read);
        break;
      // case MUX1_spare7:
      //   myControlChange(midiChannel, CClfoSync, mux1Read);
      //   break;
      case MUX1_arpFrequency:
        myControlChange(midiChannel, CCarpFrequency, mux1Read);
        break;
      case MUX1_ampVelocity:
        myControlChange(midiChannel, CCampVelocity, mux1Read);
        break;
      case MUX1_filterVelocity:
        myControlChange(midiChannel, CCfilterVelocity, mux1Read);
        break;
        // case MUX1_spare11:
        //   myControlChange(midiChannel, CCbassOctave, mux1Read);
        //   break;
        // case MUX1_spare12:
        //   myControlChange(midiChannel, CClfoSpeed, mux1Read);
        //   break;
        // case MUX1_spare13:
        //   myControlChange(midiChannel, CCleadPW, mux1Read);
        //   break;
        // case MUX1_spare14:
        //   myControlChange(midiChannel, CCleadPWM, mux1Read);
        //   break;
        // case MUX1_spare15:
        //   myControlChange(midiChannel, CCleadPWM, mux1Read);
        //   break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux2ValuesPrev[muxInput] = mux2Read;
    mux2Read = (mux2Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX2_ampRelease:
        myControlChange(midiChannel, CCampRelease, mux2Read);
        break;
      case MUX2_ampSustain:
        myControlChange(midiChannel, CCampSustain, mux2Read);
        break;
      case MUX2_ampDecay:
        myControlChange(midiChannel, CCampDecay, mux2Read);
        break;
      case MUX2_ampAttack:
        myControlChange(midiChannel, CCampAttack, mux2Read);
        break;
      case MUX2_filterKeyboard:
        myControlChange(midiChannel, CCfilterKeyboard, mux2Read);
        break;
      case MUX2_filterResonance:
        myControlChange(midiChannel, CCfilterResonance, mux2Read);
        break;
      case MUX2_osc2Volume:
        myControlChange(midiChannel, CCosc2Volume, mux2Read);
        break;
      case MUX2_osc2PW:
        myControlChange(midiChannel, CCosc2PW, mux2Read);
        break;
      case MUX2_osc1PW:
        myControlChange(midiChannel, CCosc1PW, mux2Read);
        break;
      case MUX2_osc1Volume:
        myControlChange(midiChannel, CCosc1Volume, mux2Read);
        break;
      case MUX2_filterCutoff:
        myControlChange(midiChannel, CCfilterCutoff, mux2Read);
        break;
      case MUX2_filterEnvAmount:
        myControlChange(midiChannel, CCfilterEnvAmount, mux2Read);
        break;
      case MUX2_filterAttack:
        myControlChange(midiChannel, CCfilterAttack, mux2Read);
        break;
      case MUX2_filterDecay:
        myControlChange(midiChannel, CCfilterDecay, mux2Read);
        break;
      case MUX2_filterSustain:
        myControlChange(midiChannel, CCfilterSustain, mux2Read);
        break;
      case MUX2_filterRelease:
        myControlChange(midiChannel, CCfilterRelease, mux2Read);
        break;
    }
  }

  if (mux3Read > (mux3ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux3Read < (mux3ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux3ValuesPrev[muxInput] = mux3Read;
    mux3Read = (mux3Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX3_echoEQ:
        myControlChange(midiChannel, CCechoEQ, mux3Read);
        break;
      case MUX3_echoLevel:
        myControlChange(midiChannel, CCechoLevel, mux3Read);
        break;
      case MUX3_echoFeedback:
        myControlChange(midiChannel, CCechoFeedback, mux3Read);
        break;
      case MUX3_echoSpread:
        myControlChange(midiChannel, CCechoSpread, mux3Read);
        break;
      case MUX3_echoTime:
        myControlChange(midiChannel, CCechoTime, mux3Read);
        break;
      case MUX3_lfo2UpperLower:
        myControlChange(midiChannel, CClfo2UpperLower, mux3Read);
        break;
      // case MUX3_spare6:
      //   myControlChange(midiChannel, CCspare6, mux3Read);
      //   break;
      // case MUX3_spare7:
      //   myControlChange(midiChannel, CCspare7, mux3Read);
      //   break;
      case MUX3_unisonDetune:
        myControlChange(midiChannel, CCunisonDetune, mux3Read);
        break;
      case MUX3_glideSpeed:
        myControlChange(midiChannel, CCglideSpeed, mux3Read);
        break;
      case MUX3_osc1Transpose:
        myControlChange(midiChannel, CCosc1Transpose, mux3Read);
        break;
      case MUX3_osc2Transpose:
        myControlChange(midiChannel, CCosc2Transpose, mux3Read);
        break;
      case MUX3_noiseLevel:
        myControlChange(midiChannel, CCnoiseLevel, mux3Read);
        break;
      case MUX3_glideAmount:
        myControlChange(midiChannel, CCglideAmount, mux3Read);
        break;
      case MUX3_osc1Tune:
        myControlChange(midiChannel, CCosc1Tune, mux3Read);
        break;
      case MUX3_osc2Tune:
        myControlChange(midiChannel, CCosc2Tune, mux3Read);
        break;
    }
  }

  if (mux4Read > (mux4ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux4Read < (mux4ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux4ValuesPrev[muxInput] = mux4Read;
    mux4Read = (mux4Read >> resolutionFrig);  // Change range to 0-127

    switch (muxInput) {
      case MUX4_bendToFilter:
        myControlChange(midiChannel, CCbendToFilter, mux4Read);
        break;
      case MUX4_lfo2ToFilter:
        myControlChange(midiChannel, CClfo2ToFilter, mux4Read);
        break;
      case MUX4_bendToOsc:
        myControlChange(midiChannel, CCbendToOsc, mux4Read);
        break;
      case MUX4_lfo2ToOsc:
        myControlChange(midiChannel, CClfo2ToOsc, mux4Read);
        break;
      case MUX4_lfo2FreqAcc:
        myControlChange(midiChannel, CClfo2FreqAcc, mux4Read);
        break;
      case MUX4_lfo2InitFrequency:
        myControlChange(midiChannel, CClfo2InitFrequency, mux4Read);
        break;
      case MUX4_lfo2InitAmount:
        myControlChange(midiChannel, CClfo2InitAmount, mux4Read);
        break;
      // case MUX4_spare7:
      //   myControlChange(midiChannel, CCspare7, mux4Read);
      //   break;
      case MUX4_seqAssign:
        myControlChange(midiChannel, CCseqAssign, mux4Read);
        break;
      case MUX4_seqRate:
        myControlChange(midiChannel, CCseqRate, mux4Read);
        break;
      case MUX4_seqGate:
        myControlChange(midiChannel, CCseqGate, mux4Read);
        break;
      case MUX4_lfo1Frequency:
        myControlChange(midiChannel, CClfo1Frequency, mux4Read);
        break;
      case MUX4_lfo1DepthA:
        myControlChange(midiChannel, CClfo1DepthA, mux4Read);
        break;
      case MUX4_lfo1Delay:
        myControlChange(midiChannel, CClfo1Delay, mux4Read);
        break;
      case MUX4_lfo1DepthB:
        myControlChange(midiChannel, CClfo1DepthB, mux4Read);
        break;
        // case MUX4_spare15:
        //   myControlChange(midiChannel, CCspare15, mux4Read);
        //   break;
    }
  }

  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWriteFast(MUX_0, muxInput & B0001);
  digitalWriteFast(MUX_1, muxInput & B0010);
  digitalWriteFast(MUX_2, muxInput & B0100);
  digitalWriteFast(MUX_3, muxInput & B1000);
  delayMicroseconds(75);
}

void onButtonPress(uint16_t btnIndex, uint8_t btnType) {

  // to check if a specific button was pressed

  if (btnIndex == LIMITER_SW && btnType == ROX_PRESSED) {
    limiterSW = !limiterSW;
    myControlChange(midiChannel, CClimiterSW, limiterSW);
  }

  if (btnIndex == LOWER_SW && btnType == ROX_PRESSED) {
    if (!singleSW) {
      lowerSW = !lowerSW;
      myControlChange(midiChannel, CClowerSW, lowerSW);
    }
  }

  if (btnIndex == UPPER_SW && btnType == ROX_PRESSED) {
    upperSW = !upperSW;
    myControlChange(midiChannel, CCupperSW, upperSW);
  }

  if (btnIndex == UTILITY_SW && btnType == ROX_RELEASED) {
    utilitySW = oldutilitySW + 1;
    myControlChange(midiChannel, CCutilitySW, utilitySW);
  } else {
    if (btnIndex == UTILITY_SW && btnType == ROX_HELD) {
      myControlChange(midiChannel, CCutilityAction, utilitySW);
    }
  }

  if (btnIndex == ARP_RANGE_4_SW && btnType == ROX_PRESSED) {
    arpRange4SW = !arpRange4SW;
    myControlChange(midiChannel, CCarpRange4SW, arpRange4SW);
  }

  if (btnIndex == ARP_RANGE_3_SW && btnType == ROX_PRESSED) {
    arpRange3SW = !arpRange3SW;
    myControlChange(midiChannel, CCarpRange3SW, arpRange3SW);
  }

  if (btnIndex == ARP_RANGE_2_SW && btnType == ROX_PRESSED) {
    arpRange2SW = !arpRange2SW;
    myControlChange(midiChannel, CCarpRange2SW, arpRange2SW);
  }

  if (btnIndex == ARP_RANGE_1_SW && btnType == ROX_PRESSED) {
    arpRange1SW = !arpRange1SW;
    myControlChange(midiChannel, CCarpRange1SW, arpRange1SW);
  }

  if (btnIndex == ARP_SYNC_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      arpSyncSWU = !arpSyncSWU;
      myControlChange(midiChannel, CCarpSyncSW, arpSyncSWU);
    }
    if (lowerSW) {
      arpSyncSWL = !arpSyncSWL;
      myControlChange(midiChannel, CCarpSyncSW, arpSyncSWL);
    }
  }

  if (btnIndex == ARP_HOLD_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      arpHoldSWU = !arpHoldSWU;
      myControlChange(midiChannel, CCarpHoldSW, arpHoldSWU);
    }
    if (lowerSW) {
      arpHoldSWL = !arpHoldSWL;
      myControlChange(midiChannel, CCarpHoldSW, arpHoldSWL);
    }
  }

  if (btnIndex == LAYER_SOLO_SW && btnType == ROX_PRESSED) {
    if (!singleSW) {
      layerSoloSW = !layerSoloSW;
      myControlChange(midiChannel, CClayerSoloSW, layerSoloSW);
    }
  }

  if (btnIndex == ARP_RAND_SW && btnType == ROX_PRESSED) {
    arpRandSW = !arpRandSW;
    myControlChange(midiChannel, CCarpRandSW, arpRandSW);
  }

  if (btnIndex == ARP_UP_DOWN_SW && btnType == ROX_PRESSED) {
    arpUpDownSW = !arpUpDownSW;
    myControlChange(midiChannel, CCarpUpDownSW, arpUpDownSW);
  }

  if (btnIndex == ARP_DOWN_SW && btnType == ROX_PRESSED) {
    arpDownSW = !arpDownSW;
    myControlChange(midiChannel, CCarpDownSW, arpDownSW);
  }

  if (btnIndex == ARP_UP_SW && btnType == ROX_PRESSED) {
    arpUpSW = !arpUpSW;
    myControlChange(midiChannel, CCarpUpSW, arpUpSW);
  }

  if (btnIndex == ARP_OFF_SW && btnType == ROX_PRESSED) {
    arpOffSW = !arpOffSW;
    myControlChange(midiChannel, CCarpOffSW, arpOffSW);
  }

  if (btnIndex == FILT_ENV_INV_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      envInvSWU = !envInvSWU;
      myControlChange(midiChannel, CCenvInvSW, envInvSWU);
    }
    if (lowerSW) {
      envInvSWL = !envInvSWL;
      myControlChange(midiChannel, CCenvInvSW, envInvSWL);
    }
  }

  if (btnIndex == FILT_HP_SW && btnType == ROX_PRESSED) {
    filterHPSW = !filterHPSW;
    myControlChange(midiChannel, CCfilterHPSW, filterHPSW);
  }

  if (btnIndex == FILT_BP2_SW && btnType == ROX_PRESSED) {
    filterBP2SW = !filterBP2SW;
    myControlChange(midiChannel, CCfilterBP2SW, filterBP2SW);
  }

  if (btnIndex == FILT_BP1_SW && btnType == ROX_PRESSED) {
    filterBP1SW = !filterBP1SW;
    myControlChange(midiChannel, CCfilterBP1SW, filterBP1SW);
  }

  if (btnIndex == FILT_LP2_SW && btnType == ROX_PRESSED) {
    filterLP2SW = !filterLP2SW;
    myControlChange(midiChannel, CCfilterLP2SW, filterLP2SW);
  }

  if (btnIndex == FILT_LP1_SW && btnType == ROX_PRESSED) {
    filterLP1SW = !filterLP1SW;
    myControlChange(midiChannel, CCfilterLP1SW, filterLP1SW);
  }

  if (btnIndex == REV_GLTC_SW && btnType == ROX_PRESSED) {
    revGLTCSW = !revGLTCSW;
    myControlChange(midiChannel, CCrevGLTCSW, revGLTCSW);
  }

  if (btnIndex == REV_HALL_SW && btnType == ROX_PRESSED) {
    revHallSW = !revHallSW;
    myControlChange(midiChannel, CCrevHallSW, revHallSW);
  }

  if (btnIndex == REV_PLT_SW && btnType == ROX_PRESSED) {
    revPlateSW = !revPlateSW;
    myControlChange(midiChannel, CCrevPlateSW, revPlateSW);
  }

  if (btnIndex == REV_ROOM_SW && btnType == ROX_PRESSED) {
    revRoomSW = !revRoomSW;
    myControlChange(midiChannel, CCrevRoomSW, revRoomSW);
  }

  if (btnIndex == REV_OFF_SW && btnType == ROX_PRESSED) {
    revOffSW = !revOffSW;
    myControlChange(midiChannel, CCrevOffSW, revOffSW);
  }

  if (btnIndex == PINK_SW && btnType == ROX_PRESSED) {
    noisePinkSW = !noisePinkSW;
    myControlChange(midiChannel, CCnoisePinkSW, noisePinkSW);
  }

  if (btnIndex == WHITE_SW && btnType == ROX_PRESSED) {
    noiseWhiteSW = !noiseWhiteSW;
    myControlChange(midiChannel, CCnoiseWhiteSW, noiseWhiteSW);
  }

  if (btnIndex == NOISE_OFF_SW && btnType == ROX_PRESSED) {
    noiseOffSW = !noiseOffSW;
    myControlChange(midiChannel, CCnoiseOffSW, noiseOffSW);
  }

  if (btnIndex == ECHO_SYNC_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      echoSyncSWU = !echoSyncSWU;
      myControlChange(midiChannel, CCechoSyncSW, echoSyncSWU);
    }
    if (lowerSW) {
      echoSyncSWL = !echoSyncSWL;
      myControlChange(midiChannel, CCechoSyncSW, echoSyncSWL);
    }
  }

  if (btnIndex == OSC2_RINGMOD_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      osc2ringModSWU = !osc2ringModSWU;
      myControlChange(midiChannel, CCosc2ringModSW, osc2ringModSWU);
    }
    if (lowerSW) {
      osc2ringModSWL = !osc2ringModSWL;
      myControlChange(midiChannel, CCosc2ringModSW, osc2ringModSWL);
    }
  }

  if (btnIndex == OSC1_RINGMOD_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      osc1ringModSWU = !osc1ringModSWU;
      myControlChange(midiChannel, CCosc1ringModSW, osc1ringModSWU);
    }
    if (lowerSW) {
      osc1ringModSWL = !osc1ringModSWL;
      myControlChange(midiChannel, CCosc1ringModSW, osc1ringModSWL);
    }
  }

  if (btnIndex == OSC1_OSC2_PWM_SW && btnType == ROX_PRESSED) {
    osc1_osc2PWMSW = !osc1_osc2PWMSW;
    myControlChange(midiChannel, CCosc1_osc2PWMSW, osc1_osc2PWMSW);
  }

  if (btnIndex == OSC1_PULSE_SW && btnType == ROX_PRESSED) {
    osc1pulseSW = !osc1pulseSW;
    myControlChange(midiChannel, CCosc1pulseSW, osc1pulseSW);
  }

  if (btnIndex == OSC1_SQUARE_SW && btnType == ROX_PRESSED) {
    osc1squareSW = !osc1squareSW;
    myControlChange(midiChannel, CCosc1squareSW, osc1squareSW);
  }

  if (btnIndex == OSC1_SAW_SW && btnType == ROX_PRESSED) {
    osc1sawSW = !osc1sawSW;
    myControlChange(midiChannel, CCosc1sawSW, osc1sawSW);
  }

  if (btnIndex == OSC1_TRIANGLE_SW && btnType == ROX_PRESSED) {
    osc1triangleSW = !osc1triangleSW;
    myControlChange(midiChannel, CCosc1triangleSW, osc1triangleSW);
  }

  if (btnIndex == OSC2_OSC1_PWM_SW && btnType == ROX_PRESSED) {
    osc2_osc1PWMSW = !osc2_osc1PWMSW;
    myControlChange(midiChannel, CCosc2_osc1PWMSW, osc2_osc1PWMSW);
  }

  if (btnIndex == OSC2_PULSE_SW && btnType == ROX_PRESSED) {
    osc2pulseSW = !osc2pulseSW;
    myControlChange(midiChannel, CCosc2pulseSW, osc2pulseSW);
  }

  if (btnIndex == OSC2_SQUARE_SW && btnType == ROX_PRESSED) {
    osc2squareSW = !osc2squareSW;
    myControlChange(midiChannel, CCosc2squareSW, osc2squareSW);
  }

  if (btnIndex == OSC2_SAW_SW && btnType == ROX_PRESSED) {
    osc2sawSW = !osc2sawSW;
    myControlChange(midiChannel, CCosc2sawSW, osc2sawSW);
  }

  if (btnIndex == OSC2_TRIANGLE_SW && btnType == ROX_PRESSED) {
    osc2triangleSW = !osc2triangleSW;
    myControlChange(midiChannel, CCosc2triangleSW, osc2triangleSW);
  }

  if (btnIndex == ECHO_PINGPONG_SW && btnType == ROX_PRESSED) {
    echoPingPongSW = !echoPingPongSW;
    myControlChange(midiChannel, CCechoPingPongSW, echoPingPongSW);
  }

  if (btnIndex == ECHO_TAPE_SW && btnType == ROX_PRESSED) {
    echoTapeSW = !echoTapeSW;
    myControlChange(midiChannel, CCechoTapeSW, echoTapeSW);
  }

  if (btnIndex == ECHO_STD_SW && btnType == ROX_PRESSED) {
    echoSTDSW = !echoSTDSW;
    myControlChange(midiChannel, CCechoSTDSW, echoSTDSW);
  }

  if (btnIndex == ECHO_OFF_SW && btnType == ROX_PRESSED) {
    echoOffSW = !echoOffSW;
    myControlChange(midiChannel, CCechoOffSW, echoOffSW);
  }

  if (btnIndex == CHORUS_3_SW && btnType == ROX_PRESSED) {
    chorus3SW = !chorus3SW;
    myControlChange(midiChannel, CCchorus3SW, chorus3SW);
  }

  if (btnIndex == CHORUS_2_SW && btnType == ROX_PRESSED) {
    chorus2SW = !chorus2SW;
    myControlChange(midiChannel, CCchorus2SW, chorus2SW);
  }

  if (btnIndex == CHORUS_1_SW && btnType == ROX_PRESSED) {
    chorus1SW = !chorus1SW;
    myControlChange(midiChannel, CCchorus1SW, chorus1SW);
  }

  if (btnIndex == CHORUS_OFF_SW && btnType == ROX_PRESSED) {
    chorusOffSW = !chorusOffSW;
    myControlChange(midiChannel, CCchorusOffSW, chorusOffSW);
  }

  if (btnIndex == OSC1_1_SW && btnType == ROX_PRESSED) {
    osc1_1SW = !osc1_1SW;
    myControlChange(midiChannel, CCosc1_1SW, osc1_1SW);
  }

  if (btnIndex == OSC1_2_SW && btnType == ROX_PRESSED) {
    osc1_2SW = !osc1_2SW;
    myControlChange(midiChannel, CCosc1_2SW, osc1_2SW);
  }

  if (btnIndex == OSC1_4_SW && btnType == ROX_PRESSED) {
    osc1_4SW = !osc1_4SW;
    myControlChange(midiChannel, CCosc1_4SW, osc1_4SW);
  }

  if (btnIndex == OSC1_8_SW && btnType == ROX_PRESSED) {
    osc1_8SW = !osc1_8SW;
    myControlChange(midiChannel, CCosc1_8SW, osc1_8SW);
  }

  if (btnIndex == OSC1_16_SW && btnType == ROX_PRESSED) {
    osc1_16SW = !osc1_16SW;
    myControlChange(midiChannel, CCosc1_16SW, osc1_16SW);
  }

  if (btnIndex == OSC2_1_SW && btnType == ROX_PRESSED) {
    osc2_1SW = !osc2_1SW;
    myControlChange(midiChannel, CCosc2_1SW, osc2_1SW);
  }

  if (btnIndex == OSC2_2_SW && btnType == ROX_PRESSED) {
    osc2_2SW = !osc2_2SW;
    myControlChange(midiChannel, CCosc2_2SW, osc2_2SW);
  }

  if (btnIndex == OSC2_4_SW && btnType == ROX_PRESSED) {
    osc2_4SW = !osc2_4SW;
    myControlChange(midiChannel, CCosc2_4SW, osc2_4SW);
  }

  if (btnIndex == OSC2_8_SW && btnType == ROX_PRESSED) {
    osc2_8SW = !osc2_8SW;
    myControlChange(midiChannel, CCosc2_8SW, osc2_8SW);
  }

  if (btnIndex == OSC2_16_SW && btnType == ROX_PRESSED) {
    osc2_16SW = !osc2_16SW;
    myControlChange(midiChannel, CCosc2_16SW, osc2_16SW);
  }

  if (btnIndex == GLIDE_OSC1_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      osc1glideSWU = !osc1glideSWU;
      myControlChange(midiChannel, CCosc1glideSW, osc1glideSWU);
    }
    if (lowerSW) {
      osc1glideSWL = !osc1glideSWL;
      myControlChange(midiChannel, CCosc1glideSW, osc1glideSWL);
    }
  }

  if (btnIndex == GLIDE_OSC2_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      osc2glideSWU = !osc2glideSWU;
      myControlChange(midiChannel, CCosc2glideSW, osc2glideSWU);
    }
    if (lowerSW) {
      osc2glideSWL = !osc2glideSWL;
      myControlChange(midiChannel, CCosc2glideSW, osc2glideSWL);
    }
  }

  if (btnIndex == GLIDE_PORTA_SW && btnType == ROX_PRESSED) {
    portSW = !portSW;
    myControlChange(midiChannel, CCportSW, portSW);
  }

  if (btnIndex == GLIDE_GLIDE_SW && btnType == ROX_PRESSED) {
    glideSW = !glideSW;
    myControlChange(midiChannel, CCglideSW, glideSW);
  }

  if (btnIndex == GLIDE_OFF_SW && btnType == ROX_PRESSED) {
    glideOffSW = !glideOffSW;
    myControlChange(midiChannel, CCglideOffSW, glideOffSW);
  }

  if (btnIndex == OSC2_SYNC_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      osc2SyncSWU = !osc2SyncSWU;
      myControlChange(midiChannel, CCosc2SyncSW, osc2SyncSWU);
    }
    if (lowerSW) {
      osc2SyncSWL = !osc2SyncSWL;
      myControlChange(midiChannel, CCosc2SyncSW, osc2SyncSWL);
    }
  }

  if (btnIndex == MULTI_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      multiTriggerSWU = !multiTriggerSWU;
      myControlChange(midiChannel, CCmultiTriggerSW, multiTriggerSWU);
    }
    if (lowerSW) {
      multiTriggerSWL = !multiTriggerSWL;
      myControlChange(midiChannel, CCmultiTriggerSW, multiTriggerSWL);
    }
  }

  if (btnIndex == CHORD_MEMORY_SW && btnType == ROX_PRESSED) {
    if (upperSW) {
      chordMemorySWU = !chordMemorySWU;
      myControlChange(midiChannel, CCchordMemorySW, chordMemorySWU);
    }
    if (lowerSW) {
      chordMemorySWL = !chordMemorySWL;
      myControlChange(midiChannel, CCchordMemorySW, chordMemorySWL);
    }
  }

  if (btnIndex == SINGLE_SW && btnType == ROX_PRESSED) {
    singleSW = 1;
    myControlChange(midiChannel, CCsingleSW, singleSW);
  }

  if (btnIndex == DOUBLE_SW && btnType == ROX_PRESSED) {
    doubleSW = 1;
    myControlChange(midiChannel, CCdoubleSW, doubleSW);
  }

  if (btnIndex == SPLIT_SW && btnType == ROX_PRESSED) {
    splitSW = 1;
    myControlChange(midiChannel, CCsplitSW, splitSW);
  }

  if (btnIndex == POLY_SW && btnType == ROX_PRESSED) {
    polySW = !polySW;
    myControlChange(midiChannel, CCpolySW, polySW);
  }

  if (btnIndex == SINGLE_MONO_SW && btnType == ROX_PRESSED) {
    singleMonoSW = !singleMonoSW;
    myControlChange(midiChannel, CCsingleMonoSW, singleMonoSW);
  }

  if (btnIndex == UNI_MONO_SW && btnType == ROX_PRESSED) {
    unisonMonoSW = !unisonMonoSW;
    myControlChange(midiChannel, CCunisonMonoSW, unisonMonoSW);
  }

  if (btnIndex == MAX_VOICES_SW && btnType == ROX_RELEASED) {
    maxVoicesSW = 1;
    myControlChange(midiChannel, CCmaxVoicesSW, maxVoicesSW);
  } else {
    if (btnIndex == MAX_VOICES_SW && btnType == ROX_HELD) {
      maxVoicesExitSW = 1;
      myControlChange(midiChannel, CCmaxVoicesExitSW, maxVoicesExitSW);
    }
  }

  if (btnIndex == LFO1_SYNC_SW && btnType == ROX_PRESSED) {
    lfo1SyncSW = !lfo1SyncSW;
    myControlChange(midiChannel, CClfo1SyncSW, lfo1SyncSW);
  }

  if (btnIndex == LFO1_WHEEL_SW && btnType == ROX_PRESSED) {
    lfo1modWheelSW = !lfo1modWheelSW;
    myControlChange(midiChannel, CClfo1modWheelSW, lfo1modWheelSW);
  }

  if (btnIndex == LFO2_SYNC_SW && btnType == ROX_PRESSED) {
    lfo2SyncSW = !lfo2SyncSW;
    myControlChange(midiChannel, CClfo2SyncSW, lfo2SyncSW);
  }

  if (btnIndex == LFO1_RANDOM_SW && btnType == ROX_PRESSED) {
    lfo1randSW = !lfo1randSW;
    myControlChange(midiChannel, CClfo1randSW, lfo1randSW);
  }

  if (btnIndex == LFO1_SQ_UNIPOLAR_SW && btnType == ROX_PRESSED) {
    lfo1squareUniSW = !lfo1squareUniSW;
    myControlChange(midiChannel, CClfo1squareUniSW, lfo1squareUniSW);
  }

  if (btnIndex == LFO1_SQ_BIPOLAR_SW && btnType == ROX_PRESSED) {
    lfo1squareBipSW = !lfo1squareBipSW;
    myControlChange(midiChannel, CClfo1squareBipSW, lfo1squareBipSW);
  }

  if (btnIndex == LFO1_SAW_UP_SW && btnType == ROX_PRESSED) {
    lfo1sawUpSW = !lfo1sawUpSW;
    myControlChange(midiChannel, CClfo1sawUpSW, lfo1sawUpSW);
  }

  if (btnIndex == LFO1_SAW_DOWN_SW && btnType == ROX_PRESSED) {
    lfo1sawDnSW = !lfo1sawDnSW;
    myControlChange(midiChannel, CClfo1sawDnSW, lfo1sawDnSW);
  }

  if (btnIndex == LFO1_TRIANGLE_SW && btnType == ROX_PRESSED) {
    lfo1triangleSW = !lfo1triangleSW;
    myControlChange(midiChannel, CClfo1triangleSW, lfo1triangleSW);
  }

  if (btnIndex == LFO1_RESET_SW && btnType == ROX_PRESSED) {
    lfo1resetSW = !lfo1resetSW;
    myControlChange(midiChannel, CClfo1resetSW, lfo1resetSW);
  }

  if (btnIndex == LFO1_OSC1_SW && btnType == ROX_PRESSED) {
    lfo1osc1SW = !lfo1osc1SW;
    myControlChange(midiChannel, CClfo1osc1SW, lfo1osc1SW);
  }

  if (btnIndex == LFO1_OSC2_SW && btnType == ROX_PRESSED) {
    lfo1osc2SW = !lfo1osc2SW;
    myControlChange(midiChannel, CClfo1osc2SW, lfo1osc2SW);
  }

  if (btnIndex == LFO1_PW1_SW && btnType == ROX_PRESSED) {
    lfo1pw1SW = !lfo1pw1SW;
    myControlChange(midiChannel, CClfo1pw1SW, lfo1pw1SW);
  }

  if (btnIndex == LFO1_PW2_SW && btnType == ROX_PRESSED) {
    lfo1pw2SW = !lfo1pw2SW;
    myControlChange(midiChannel, CClfo1pw2SW, lfo1pw2SW);
  }

  if (btnIndex == LFO1_FILT_SW && btnType == ROX_PRESSED) {
    lfo1filtSW = !lfo1filtSW;
    myControlChange(midiChannel, CClfo1filtSW, lfo1filtSW);
  }

  if (btnIndex == LFO1_AMP_SW && btnType == ROX_PRESSED) {
    lfo1ampSW = !lfo1ampSW;
    myControlChange(midiChannel, CClfo1ampSW, lfo1ampSW);
  }

  if (btnIndex == LFO1_SEQ_RATE_SW && btnType == ROX_PRESSED) {
    lfo1seqRateSW = !lfo1seqRateSW;
    myControlChange(midiChannel, CClfo1seqRateSW, lfo1seqRateSW);
  }

  if (btnIndex == SEQ_PLAY_SW && btnType == ROX_PRESSED) {
    seqPlaySW = 1;
    myControlChange(midiChannel, CCseqPlaySW, seqPlaySW);
  }

  if (btnIndex == SEQ_STOP_SW && btnType == ROX_PRESSED) {
    seqStopSW = 1;
    myControlChange(midiChannel, CCseqStopSW, seqStopSW);
  }

  if (btnIndex == SEQ_KEY_SW && btnType == ROX_PRESSED) {
    seqKeySW = !seqKeySW;
    myControlChange(midiChannel, CCseqKeySW, seqKeySW);
  }

  if (btnIndex == SEQ_TRANS_SW && btnType == ROX_PRESSED) {
    seqTransSW = !seqTransSW;
    myControlChange(seqTransSW, CCseqTransSW, seqTransSW);
  }

  if (btnIndex == SEQ_LOOP_SW && btnType == ROX_PRESSED) {
    seqLoopSW = !seqLoopSW;
    myControlChange(midiChannel, CCseqLoopSW, seqLoopSW);
  }

  if (btnIndex == STEP_FW_SW && btnType == ROX_PRESSED) {
    seqFwSW = 1;
    myControlChange(midiChannel, CCseqFwSW, seqFwSW);
  }

  if (btnIndex == STEP_BW_SW && btnType == ROX_PRESSED) {
    seqBwSW = 1;
    myControlChange(midiChannel, CCseqBwSW, seqBwSW);
  }

  if (btnIndex == SEQ_ENABLE_1_SW && btnType == ROX_PRESSED) {
    seqEnable1SW = !seqEnable1SW;
    myControlChange(midiChannel, CCseqEnable1SW, seqEnable1SW);
  }

  if (btnIndex == SEQ_ENABLE_2_SW && btnType == ROX_PRESSED) {
    seqEnable2SW = !seqEnable2SW;
    myControlChange(midiChannel, CCseqEnable2SW, seqEnable2SW);
  }

  if (btnIndex == SEQ_ENABLE_3_SW && btnType == ROX_PRESSED) {
    seqEnable3SW = !seqEnable3SW;
    myControlChange(midiChannel, CCseqEnable3SW, seqEnable3SW);
  }

  if (btnIndex == SEQ_ENABLE_4_SW && btnType == ROX_PRESSED) {
    seqEnable4SW = !seqEnable4SW;
    myControlChange(midiChannel, CCseqEnable4SW, seqEnable4SW);
  }

  if (btnIndex == SEQ_SYNC_SW && btnType == ROX_PRESSED) {
    seqSyncSW = !seqSyncSW;
    myControlChange(midiChannel, CCseqSyncSW, seqSyncSW);
  }

  if (btnIndex == SEQ_REC_EDIT_SW && btnType == ROX_PRESSED) {
    seqrecEditSW = !seqrecEditSW;
    myControlChange(midiChannel, CCseqrecEditSW, seqrecEditSW);
  }

  if (btnIndex == SEQ_INS_STEP_SW && btnType == ROX_PRESSED) {
    seqinsStepSW = 1;
    myControlChange(midiChannel, CCseqinsStepSW, seqinsStepSW);
  }

  if (btnIndex == SEQ_DEL_STEP_SW && btnType == ROX_PRESSED) {
    seqdelStepSW = 1;
    myControlChange(midiChannel, CCseqdelStepSW, seqdelStepSW);
  }

  if (btnIndex == SEQ_ADD_STEP_SW && btnType == ROX_PRESSED) {
    seqaddStepSW = 1;
    myControlChange(midiChannel, CCseqaddStepSW, seqaddStepSW);
  }

  if (btnIndex == SEQ_UTIL_SW && btnType == ROX_PRESSED) {
    seqUtilSW = 1;
    myControlChange(midiChannel, CCseqUtilSW, seqUtilSW);
  }

  if (btnIndex == SEQ_REST_SW && btnType == ROX_PRESSED) {
    seqRestSW = 1;
    myControlChange(midiChannel, CCseqRestSW, seqRestSW);
  }
}

void showSettingsPage() {
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

void midiCCOut(byte cc, byte value) {
  if (midiOutCh > 0) {
    switch (ccType) {
      case 0:
        {
          switch (cc) {
            case MIDIarpSyncU:
              if (updateParams) {
                usbMIDI.sendNoteOn(0, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(0, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(0, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(0, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIarpSyncL:
              if (updateParams) {
                usbMIDI.sendNoteOn(1, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(1, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(1, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(1, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIarpHoldU:
              if (updateParams) {
                usbMIDI.sendNoteOn(2, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(2, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(2, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(2, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIarpHoldL:
              if (updateParams) {
                usbMIDI.sendNoteOn(3, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(3, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(3, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(3, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIenvInvU:
              if (updateParams) {
                usbMIDI.sendNoteOn(4, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(4, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(4, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(4, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIenvInvL:
              if (updateParams) {
                usbMIDI.sendNoteOn(5, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(5, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(5, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(5, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc2SyncU:
              if (updateParams) {
                usbMIDI.sendNoteOn(6, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(6, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(6, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(6, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc2SyncL:
              if (updateParams) {
                usbMIDI.sendNoteOn(7, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(7, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(7, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(7, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDImultiTriggerU:
              if (updateParams) {
                usbMIDI.sendNoteOn(8, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(8, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(8, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(8, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDImultiTriggerL:
              if (updateParams) {
                usbMIDI.sendNoteOn(9, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(9, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(9, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(9, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIechoSyncU:
              if (updateParams) {
                usbMIDI.sendNoteOn(10, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(10, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(10, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(10, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIechoSyncL:
              if (updateParams) {
                usbMIDI.sendNoteOn(11, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(11, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(11, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(11, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc1ringU:
              if (updateParams) {
                usbMIDI.sendNoteOn(12, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(12, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(12, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(12, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc1ringL:
              if (updateParams) {
                usbMIDI.sendNoteOn(13, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(13, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(13, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(13, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc2ringU:
              if (updateParams) {
                usbMIDI.sendNoteOn(14, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(14, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(14, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(14, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc2ringL:
              if (updateParams) {
                usbMIDI.sendNoteOn(15, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(15, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(15, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(15, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc1glideU:
              if (updateParams) {
                usbMIDI.sendNoteOn(16, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(16, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(16, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(16, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc1glideL:
              if (updateParams) {
                usbMIDI.sendNoteOn(17, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(17, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(17, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(17, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc2glideU:
              if (updateParams) {
                usbMIDI.sendNoteOn(18, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(18, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(18, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(18, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIosc2glideL:
              if (updateParams) {
                usbMIDI.sendNoteOn(19, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(19, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(19, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(19, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlimiter:
              if (updateParams) {
                usbMIDI.sendNoteOn(20, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(20, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(20, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(20, 0, midiOutCh);   //MIDI USB is set to Out
              break;

              //
              // lowest note on a 76 note keybed
              //

            case MIDIlfo1modWheelU:
              if (updateParams) {
                usbMIDI.sendNoteOn(127, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(127, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(127, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(127, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1modWheelL:
              if (updateParams) {
                usbMIDI.sendNoteOn(126, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(126, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(126, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(126, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1resetU:
              if (updateParams) {
                usbMIDI.sendNoteOn(125, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(125, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(125, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(125, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1resetL:
              if (updateParams) {
                usbMIDI.sendNoteOn(124, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(124, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(124, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(124, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1osc1U:
              if (updateParams) {
                usbMIDI.sendNoteOn(123, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(123, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(123, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(123, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1osc1L:
              if (updateParams) {
                usbMIDI.sendNoteOn(122, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(122, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(122, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(122, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1osc2U:
              if (updateParams) {
                usbMIDI.sendNoteOn(121, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(121, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(121, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(121, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1osc2L:
              if (updateParams) {
                usbMIDI.sendNoteOn(120, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(120, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(120, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(120, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1pw1U:
              if (updateParams) {
                usbMIDI.sendNoteOn(119, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(119, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(119, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(119, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1pw1L:
              if (updateParams) {
                usbMIDI.sendNoteOn(118, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(118, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(118, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(118, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1pw2U:
              if (updateParams) {
                usbMIDI.sendNoteOn(117, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(117, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(117, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(117, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1pw2L:
              if (updateParams) {
                usbMIDI.sendNoteOn(116, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(116, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(116, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(116, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1filtU:
              if (updateParams) {
                usbMIDI.sendNoteOn(115, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(115, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(115, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(115, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1filtL:
              if (updateParams) {
                usbMIDI.sendNoteOn(114, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(114, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(114, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(114, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1ampU:
              if (updateParams) {
                usbMIDI.sendNoteOn(113, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(113, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(113, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(113, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1ampL:
              if (updateParams) {
                usbMIDI.sendNoteOn(112, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(112, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(112, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(112, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1seqRateU:
              if (updateParams) {
                usbMIDI.sendNoteOn(111, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(111, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(111, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(111, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1seqRateL:
              if (updateParams) {
                usbMIDI.sendNoteOn(110, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(110, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(110, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(110, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIchordMemoryU:
              if (updateParams) {
                usbMIDI.sendNoteOn(109, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(109, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(109, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(109, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIchordMemoryL:
              if (updateParams) {
                usbMIDI.sendNoteOn(108, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(108, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(108, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(108, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlayerSolo:
              if (updateParams) {
                usbMIDI.sendNoteOn(107, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(107, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(107, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(107, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1SyncU:
              if (updateParams) {
                usbMIDI.sendNoteOn(106, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(106, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(106, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(106, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIlfo1SyncL:
              if (updateParams) {
                usbMIDI.sendNoteOn(105, 127, midiOutCh);  //MIDI USB is set to Out
                usbMIDI.sendNoteOff(105, 0, midiOutCh);   //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(105, 127, midiOutCh);  //MIDI DIN is set to Out
              MIDI.sendNoteOff(105, 0, midiOutCh);   //MIDI USB is set to Out
              break;

            case MIDIsingleSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(104, 127, midiOutCh);  //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(104, 127, midiOutCh);  //MIDI DIN is set to Out
              break;

            case MIDIdoubleSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(103, 127, midiOutCh);  //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(103, 127, midiOutCh);  //MIDI DIN is set to Out
              break;

            case MIDIsplitSW:
              if (updateParams) {
                usbMIDI.sendNoteOn(102, 127, midiOutCh);  //MIDI USB is set to Out
              }
              MIDI.sendNoteOn(102, 127, midiOutCh);  //MIDI DIN is set to Out
              break;

            default:
              if (updateParams) {
                usbMIDI.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
              }
              MIDI.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
              break;
          }
        }
      case 1:
        {
          break;
        }
      case 2:
        {
          break;
        }
    }
  }
}

void midi6CCOut(byte cc, byte value) {
  // if (updateParams) {
  //   usbMIDI.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
  // }
  MIDI6.sendControlChange(cc, value, midiOutCh);  //MIDI DIN is set to Out
}

void checkSwitches() {

  saveButton.update();
  if (saveButton.held()) {
    switch (state) {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        break;
    }
  } else if (saveButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        if (patches.size() < PATCHES_LIMIT) {
          resetPatchesOrdering();  //Reset order of patches from first patch
          patches.push({ patches.size() + 1, INITPATCHNAME });
          state = SAVE;
        }
        break;
      case SAVE:
        //Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
        patchName = patches.last().patchName;
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(patches.last().patchNo, patches.last().patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        break;
      case PATCHNAMING:
        if (renamedPatch.length() > 0) patchName = renamedPatch;  //Prevent empty strings
        state = PATCH;
        savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
        showPatchPage(patches.last().patchNo, patchName);
        patchNo = patches.last().patchNo;
        loadPatches();  //Get rid of pushed patch if it wasn't saved
        setPatchesOrdering(patchNo);
        renamedPatch = "";
        state = PARAMETER;
        break;
    }
  }

  settingsButton.update();
  if (settingsButton.held()) {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
  } else if (settingsButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = SETTINGS;
        showSettingsPage();
        break;
      case SETTINGS:
        showSettingsPage();
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }

  backButton.update();
  if (backButton.held()) {
    //If Back button held, Panic - all notes off
  } else if (backButton.numClicks() == 1) {
    switch (state) {
      case RECALL:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        break;
      case SAVE:
        renamedPatch = "";
        state = PARAMETER;
        loadPatches();  //Remove patch that was to be saved
        setPatchesOrdering(patchNo);
        break;
      case PATCHNAMING:
        charIndex = 0;
        renamedPatch = "";
        state = SAVE;
        break;
      case DELETE:
        setPatchesOrdering(patchNo);
        state = PARAMETER;
        break;
      case SETTINGS:
        state = PARAMETER;
        break;
      case SETTINGSVALUE:
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.held()) {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
  } else if (recallButton.numClicks() == 1) {
    switch (state) {
      case PARAMETER:
        state = RECALL;  //show patch list
        break;
      case RECALL:
        state = PATCH;
        //Recall the current patch
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case SAVE:
        showRenamingPage(patches.last().patchName);
        patchName = patches.last().patchName;
        state = PATCHNAMING;
        break;
      case PATCHNAMING:
        if (renamedPatch.length() < 12)  //actually 12 chars
        {
          renamedPatch.concat(String(currentCharacter));
          charIndex = 0;
          currentCharacter = CHARACTERS[charIndex];
          showRenamingPage(renamedPatch);
        }
        break;
      case DELETE:
        //Don't delete final patch
        if (patches.size() > 1) {
          state = DELETEMSG;
          patchNo = patches.first().patchNo;     //PatchNo to delete from SD card
          patches.shift();                       //Remove patch from circular buffer
          deletePatch(String(patchNo).c_str());  //Delete from SD card
          loadPatches();                         //Repopulate circular buffer to start from lowest Patch No
          renumberPatchesOnSD();
          loadPatches();                      //Repopulate circular buffer again after delete
          patchNo = patches.first().patchNo;  //Go back to 1
          recallPatch(patchNo);               //Load first patch
        }
        state = PARAMETER;
        break;
      case SETTINGS:
        state = SETTINGSVALUE;
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::save_current_value();
        state = SETTINGS;
        showSettingsPage();
        break;
    }
  }
}

void reinitialiseToPanel() {
  //This sets the current patch to be the same as the current hardware panel state - all the pots
  //The four button controls stay the same state
  //This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++) {
    mux1ValuesPrev[i] = RE_READ;
    mux2ValuesPrev[i] = RE_READ;
    mux3ValuesPrev[i] = RE_READ;
    mux4ValuesPrev[i] = RE_READ;
  }
  patchName = INITPATCHNAME;
  showPatchPage("Initial", "Panel Settings");
}

void checkEncoder() {
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.push(patches.shift());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case RECALL:
        patches.push(patches.shift());
        break;
      case SAVE:
        patches.push(patches.shift());
        break;
      case PATCHNAMING:
        if (charIndex == TOTALCHARS) charIndex = 0;  //Wrap around
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.push(patches.shift());
        break;
      case SETTINGS:
        settings::increment_setting();
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::increment_setting_value();
        showSettingsPage();
        break;
    }
    encPrevious = encRead;
  } else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3)) {
    switch (state) {
      case PARAMETER:
        state = PATCH;
        patches.unshift(patches.pop());
        patchNo = patches.first().patchNo;
        recallPatch(patchNo);
        state = PARAMETER;
        break;
      case RECALL:
        patches.unshift(patches.pop());
        break;
      case SAVE:
        patches.unshift(patches.pop());
        break;
      case PATCHNAMING:
        if (charIndex == -1)
          charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.unshift(patches.pop());
        break;
      case SETTINGS:
        settings::decrement_setting();
        showSettingsPage();
        break;
      case SETTINGSVALUE:
        settings::decrement_setting_value();
        showSettingsPage();
        break;
    }
    encPrevious = encRead;
  }
}

void checkEEPROM() {

  if (oldLEDintensity != LEDintensity) {
    LEDintensity = LEDintensity * 10;
    trilldisplay.setBacklight(LEDintensity);
    setLEDDisplay0();
    display0.setBacklight(LEDintensity);
    setLEDDisplay1();
    display1.setBacklight(LEDintensity);
    setLEDDisplay2();
    display2.setBacklight(LEDintensity);
    oldLEDintensity = LEDintensity;
  }
}

void stopLEDs() {
  unsigned long currentMillis = millis();

  if (layerSoloSW && upperSW) {
    if (currentMillis - upper_timer >= interval) {
      upper_timer = currentMillis;
      if (sr.readPin(UPPER_LED) == HIGH) {
        sr.writePin(UPPER_LED, LOW);
      } else {
        sr.writePin(UPPER_LED, HIGH);
      }
    }
  }

  if (layerSoloSW && lowerSW) {
    if (currentMillis - lower_timer >= interval) {
      lower_timer = currentMillis;
      if (sr.readPin(LOWER_LED) == HIGH) {
        sr.writePin(LOWER_LED, LOW);
      } else {
        sr.writePin(LOWER_LED, HIGH);
      }
    }
  }

  if (chordMemoryWaitU) {
    if (currentMillis - chord_timerU >= interval) {
      chord_timerU = currentMillis;
      if (sr.readPin(CHORD_MEMORY_LED) == HIGH) {
        sr.writePin(CHORD_MEMORY_LED, LOW);
      } else {
        sr.writePin(CHORD_MEMORY_LED, HIGH);
      }
    }
  }

  if (chordMemoryWaitL) {
    if (currentMillis - chord_timerL >= interval) {
      chord_timerL = currentMillis;
      if (green.readPin(GREEN_CHORD_MEMORY_LED) == HIGH) {
        green.writePin(GREEN_CHORD_MEMORY_LED, LOW);
      } else {
        green.writePin(GREEN_CHORD_MEMORY_LED, HIGH);
      }
    }
  }

  if (learning) {
    if (currentMillis - split_timer >= interval) {
      split_timer = currentMillis;
      if (sr.readPin(SPLIT_LED) == HIGH) {
        sr.writePin(SPLIT_LED, LOW);
        setLEDDisplay2();
        display2.setBacklight(0);
      } else {
        sr.writePin(SPLIT_LED, HIGH);
        setLEDDisplay2();
        display2.setBacklight(LEDintensity);
      }
    }
  }
}

void sendEscapeKey() {
  if ((maxVoices_timer > 0) && (millis() - maxVoices_timer > 10000)) {
    midi6CCOut(MIDIEscape, 127);
    maxVoices_timer = 0;
  }
}

void loop() {
  myusb.Task();
  midi1.read();  //USB HOST MIDI Class Compliant
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);
  checkMux();           // Read the sliders and switches
  checkSwitches();      // Read the buttons for the program menus etc
  checkEncoder();       // check the encoder status
  octoswitch.update();  // read all the buttons for the Synthex
  sr.update();          // update all the RED LEDs in the buttons
  green.update();       // update all the GREEN LEDs in the buttons
  checkEEPROM();        // check anything that may have changed form the initial startup
  stopLEDs();           // blink the wave LEDs once when pressed
  sendEscapeKey();
  //convertIncomingNote();                 // read a note when in learn mode and use it to set the values
}
