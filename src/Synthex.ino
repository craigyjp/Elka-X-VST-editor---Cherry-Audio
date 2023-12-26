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

byte ccType = 0;  //(EEPROM)

#include "Settings.h"

int count = 0;  //For MIDI Clk Sync
int DelayForSH3 = 12;
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
  sr.begin(LED_DATA, LED_LATCH, LED_CLK, LED_PWM);
  green.begin(GREEN_LED_DATA, GREEN_LED_LATCH, GREEN_LED_CLK, GREEN_LED_PWM);
  setupDisplay();
  setUpSettings();
  setupHardware();
  //setUpLEDS();

  LEDintensity = getLEDintensity();
  LEDintensity = LEDintensity * 10;
  oldLEDintensity = LEDintensity;

  trilldisplay.begin();                     // initializes the display
  trilldisplay.setBacklight(LEDintensity);  // set the brightness to 100 %
  trilldisplay.print("   1");               // display INIT on the display
  delay(10);

  setLEDDisplay0();
  display0.begin();                     // initializes the display
  display0.setBacklight(LEDintensity);  // set the brightness to intensity
  display0.print(" 127");               // display INIT on the display
  delay(10);

  setLEDDisplay1();
  display1.begin();                     // initializes the display
  display1.setBacklight(LEDintensity);  // set the brightness to intensity
  display1.print("   0");               // display INIT on the display
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
  midi1.setHandleAfterTouch(myAfterTouch);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myConvertControlChange);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  usbMIDI.setHandleAfterTouch(myAfterTouch);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleControlChange(myConvertControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("MIDI In DIN Listening");

  //Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  //Read MIDI Out Channel from EEPROM
  midiOutCh = getMIDIOutCh();

  sr.writePin(UPPER_LED, HIGH);
  // sr.writePin(GREEN_ARP_RANGE_4_LED, HIGH);
  // sr.writePin(LEAD_VCO2_WAVE_LED, HIGH);

  recallPatch(patchNo);  //Load first patch
}

void myNoteOn(byte channel, byte note, byte velocity) {
  if (learning) {
    learningNote = note;
    noteArrived = true;
  }
  if (!learning) {
    MIDI.sendNoteOn(note, velocity, channel);
    if (sendNotes) {
      usbMIDI.sendNoteOn(note, velocity, channel);
    }
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

  // if (learning && noteArrived) {
  //   switch (learningDisplayNumber) {

  //     case 1:
  //       leadBottomNote = learningNote;
  //       setLEDDisplay1();
  //       display1.setBacklight(LEDintensity);
  //       displayLEDNumber(1, leadBottomNote);
  //       //updateleadTopNote();
  //       break;

  //     case 0:
  //       leadTopNote = learningNote;
  //       leadLearn = 0;
  //       //updateleadLearn();
  //       //refreshLeadNotes();
  //       learning = false;
  //       break;

  //       // case 3:
  //       //   polyBottomNote = learningNote;
  //       //   setLEDDisplay3();
  //       //   display3.setBacklight(LEDintensity);
  //       //   displayLEDNumber(3, polyBottomNote);
  //       //   //updatepolyTopNote();
  //       //   break;

  //     case 2:
  //       polyTopNote = learningNote;
  //       polyLearn = 0;
  //       //updatepolyLearn();
  //       //refreshPolyNotes();
  //       learning = false;
  //       break;

  //       // case 5:
  //       //   stringsBottomNote = learningNote;
  //       //   setLEDDisplay5();
  //       //   display5.setBacklight(LEDintensity);
  //       //   displayLEDNumber(5, stringsBottomNote);
  //       //   //updatestringsTopNote();
  //       //   break;

  //     case 4:
  //       stringsTopNote = learningNote;
  //       stringsLearn = 0;
  //       //updatestringsLearn();
  //       //refreshStringNotes();
  //       learning = false;
  //       break;

  //       // case 7:
  //       //   bassBottomNote = learningNote;
  //       //   setLEDDisplay7();
  //       //   display7.setBacklight(LEDintensity);
  //       //   displayLEDNumber(7, bassBottomNote);
  //       //   //updatebassTopNote();
  //       //   break;

  //     case 6:
  //       bassTopNote = learningNote;
  //       bassLearn = 0;
  //       //updatebassLearn();
  //       //refreshBassNotes();
  //       learning = false;
  //       break;
  //   }
  //   noteArrived = false;
  // }
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

// void updatetrillUp() {
//   if (trillUp == 1) {
//     trillValue++;
//     if (trillValue > 60) {
//       trillValue = 60;
//     }
//     if (trillValue == 0) {
//       trillValue = 1;
//     }
//     displayLEDNumber(8, trillValue);
//     midiCCOut(CCtrillUp, 127);
//     trillUp = 0;
//   } else {
//     displayLEDNumber(8, trillValue);
//   }
// }

// void updateTrills() {
//   displayLEDNumber(8, trillValue);
//   if (trillValue > 0) {
//     for (int i = 1; i < trillValue; i++) {
//       midiCCOut(CCtrillUp, 127);
//     }
//   }
//   if (trillValue < 0) {
//     int negativetrillValue = abs(trillValue);
//     for (int i = 0; i < negativetrillValue; i++) {
//       midiCCOut(CCtrillDown, 127);
//     }
//   }
// }

// void updatetrillDown() {
//   if (trillDown == 1) {
//     trillValue = trillValue - 1;
//     if (trillValue < -60) {
//       trillValue = -60;
//     }
//     if (trillValue == 0) {
//       trillValue = -1;
//     }
//     displayLEDNumber(8, trillValue);
//     midiCCOut(CCtrillDown, 127);
//     trillDown = 0;
//   } else {
//     displayLEDNumber(8, trillValue);
//   }
// }

// void updatemodWheel() {
//   if (modWheel > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wheel", String("On"));
//     }
//     sr.writePin(MOD_WHEEL_LED, HIGH);
//     midiCCOut(CCmodWheel, CC_ON);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wheel", String("Off"));
//     }
//     sr.writePin(MOD_WHEEL_LED, LOW);
//     midiCCOut(CCmodWheel, 0);
//   }
// }

// void updateleadMix() {
//   if (!recallPatchFlag) {
//     showCurrentParameterPage("Lead Mix", String(leadMixstr) + " dB");
//   }
//   midiCCOut(CCleadMix, leadMix);
// }

// void updatephaserSpeed() {
//   if (modSourcePhaser == 2) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Sweep Speed", String(phaserSpeedstr) + " Hz");
//     }
//     midiCCOut(CCphaserSpeed, phaserSpeed);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Sweep Speed", String("Inactive"));
//     }
//   }
// }

// void updatechorusFlange() {
//   if (chorusFlange > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Mode", String("Flange"));
//     }
//     midiCCOut(CCchorusFlange, CC_ON);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Mode", String("Chorus"));
//     }
//     midiCCOut(CCchorusFlange, 0);
//   }
// }

// void updatechorusSpeed() {
//   if (chorusFlange > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Flanger Speed", String(chorusSpeedstr) + " Hz");
//     }
//     midiCCOut(CCchorusSpeed, chorusSpeed);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Chorus Speed", String(chorusSpeedstr) + " Hz");
//     }
//     midiCCOut(CCchorusSpeed, chorusSpeed);
//   }
// }

// void updatelfoDelay() {
//   if (lfoDelay > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Delay", String("On"));
//     }
//     sr.writePin(LFO_DELAY_LED, HIGH);
//     midiCCOut(CClfoDelay, CC_ON);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Delay", String("Off"));
//     }
//     sr.writePin(LFO_DELAY_LED, LOW);
//     midiCCOut(CClfoDelay, 0);
//   }
// }

// void updatechorusDepth() {
//   if (chorusFlange > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Flanger Depth", String(chorusDepthstr) + " %");
//     }
//     midiCCOut(CCchorusDepth, chorusDepth);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Chorus Depth", String(chorusDepthstr) + " %");
//     }
//     midiCCOut(CCchorusDepth, chorusDepth);
//   }
// }

// void updatechorusRes() {
//   if (chorusFlange > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Flanger Res", String(chorusResstr) + " %");
//     }
//     midiCCOut(CCchorusRes, chorusRes);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Flanger Res", String("Inactive"));
//     }
//   }
// }

// void updatelfoSync() {
//   if (lfoSync > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Sync", String("On"));
//     }
//     sr.writePin(LFO_SYNC_LED, HIGH);
//     midiCCOut(CClfoSync, CC_ON);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Sync", String("Off"));
//     }
//     sr.writePin(LFO_SYNC_LED, LOW);
//     midiCCOut(CClfoSync, 0);
//   }
// }

// void updateshSource() {
//   if (shSource > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("S/H Source", String("VCO 2"));
//     }
//     midiCCOut(CCshSource, CC_ON);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("S/H Source", String("Noise"));
//     }
//     midiCCOut(CCshSource, 0);
//   }
// }

// void updatestringOctave() {
//   switch (stringOctave) {
//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("String Octave", String("Octave Up"));
//       }
//       midiCCOut(CCstringOctave, CC_ON);
//       break;
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("String Octave", String("Normal"));
//       }
//       midiCCOut(CCstringOctave, 64);
//       break;
//     case 0:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("String Octave", String("Octave Down"));
//       }
//       midiCCOut(CCstringOctave, 0);
//       break;
//   }
// }

// void updatebassOctave() {
//   switch (bassOctave) {
//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Bass Octave", String("Octave Up"));
//       }
//       midiCCOut(CCbassOctave, CC_ON);
//       break;
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Bass Octave", String("Normal"));
//       }
//       midiCCOut(CCbassOctave, 64);
//       break;
//     case 0:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Bass Octave", String("Octave Down"));
//       }
//       midiCCOut(CCbassOctave, 0);
//       break;
//   }
// }

// void updatelfoSpeed() {
//   if (lfoSync < 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Speed", String(lfoSpeedstr) + " Hz");
//     }
//   }
//   if (lfoSync > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Speed", String(lfoSpeedstring));
//     }
//   }
//   midiCCOut(CClfoSpeed, lfoSpeed);
// }

// void updateechoTime() {
//   if (echoSync < 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Echo Time", String(echoTimestr) + " ms");
//     }
//   }
//   if (echoSync > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Echo Time", String(echoTimestring));
//     }
//   }
//   midiCCOut(CCechoTime, echoTime);
// }

// void updatereverbType() {
//   switch (reverbType) {
//     case 0:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Reverb Type", String("Hall"));
//       }
//       midiCCOut(CCreverbType, 0);
//       break;
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Reverb Type", String("Plate"));
//       }
//       midiCCOut(CCreverbType, 64);
//       break;
//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Reverb Type", String("Spring"));
//       }
//       midiCCOut(CCreverbType, 127);
//       break;
//   }
// }


// void updatearpSync() {
//   if (arpSync > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Arp Sync", String("On"));
//     }
//     sr.writePin(ARP_SYNC_LED, HIGH);
//     midiCCOut(CCarpSync, CC_ON);
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Arp Sync", String("Off"));
//     }
//     sr.writePin(ARP_SYNC_LED, LOW);
//     midiCCOut(CCarpSync, 0);
//   }
// }

// void updatearpSpeed() {
//   if (arpSync < 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Arp Speed", String(arpSpeedstr) + " Hz");
//     }
//   }
//   if (arpSync > 63) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Arp Speed", String(arpSpeedstring));
//     }
//   }
//   midiCCOut(CCarpSpeed, arpSpeed);
// }

// void updatearpRange() {
//   switch (arpRange) {
//     case 0:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Arp Range", String("1"));
//       }
//       midiCCOut(CCarpRange, 0);
//       break;
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Arp Range", String("2"));
//       }
//       midiCCOut(CCarpRange, 64);
//       break;
//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Arp Range", String("3"));
//       }
//       midiCCOut(CCarpRange, CC_ON);
//       break;
//   }
// }

// void updatestringRelease() {
//   if (stringReleasestr < 1000) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Release", String(stringReleasestr) + " ms");
//     }
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Release", String(stringReleasestr * 0.001) + " s");
//     }
//   }
//   midiCCOut(CCstringRelease, stringRelease);
// }

// void updatestringAttack() {
//   if (stringAttackstr < 1000) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Attack", String(stringAttackstr) + " ms");
//     }
//   } else {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Attack", String(stringAttackstr * 0.001) + " s");
//     }
//   }
//   midiCCOut(CCstringAttack, stringAttack);
// }

// void updatemodSourcePhaser() {
//   switch (modSourcePhaser) {
//     case 0:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Mod Source", String("ENV"));
//       }
//       midiCCOut(CCmodSourcePhaser, 0);
//       break;
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Mod Source", String("S & H"));
//       }
//       midiCCOut(CCmodSourcePhaser, 64);
//       break;
//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Mod Source", String("LFO"));
//       }
//       midiCCOut(CCmodSourcePhaser, CC_ON);
//       break;
//   }
// }

void updatemasterTune() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Tune", String(masterTunestr) + " Semi");
  }
  midiCCOut(CCmasterTune, masterTune);
}

void updatemasterVolume() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Master Volume", String(masterVolumestr) + " %");
  }
  midiCCOut(CCmasterVolume, masterVolume);
}

void updatelayerPan() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Layer Pan", String(layerPanstr) + " %");
  }
  midiCCOut(CClayerPan, layerPan);
}

void updatelayerVolume() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Layer Volume", "Low " + String(layerVolumestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Layer Volume", "Upp " + String(layerVolumestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CClayerVolume, layerVolumeL);
  }
  if (upperSW) {
    midiCCOut(CClayerVolume, layerVolumeU);
  }
}

void updatereverbLevel() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Reverb Level", "Low " + String(reverbLevelstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Reverb Level", "Upp " + String(reverbLevelstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCreverbLevel, reverbLevelL);
  }
  if (upperSW) {
    midiCCOut(CCreverbLevel, reverbLevelU);
  }
}

void updatereverbDecay() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Reverb Decay", "Low " + String(reverbDecaystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Reverb Decay", "Upp " + String(reverbDecaystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCreverbDecay, reverbDecayL);
  }
  if (upperSW) {
    midiCCOut(CCreverbDecay, reverbDecayU);
  }
}

void updatereverbEQ() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Reverb EQ", "Low " + String(reverbEQstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Reverb EQ", "Upp " + String(reverbEQstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCreverbEQ, reverbEQL);
  }
  if (upperSW) {
    midiCCOut(CCreverbEQ, reverbEQU);
  }
}

void updatearpFrequency() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Arp Frequency", "Low " + String(arpFrequencystr) + " Hz");
    }
    if (upperSW) {
      showCurrentParameterPage("Arp Frequency", "Upp " + String(arpFrequencystr) + " Hz");
    }
  }
  if (lowerSW) {
    midiCCOut(CCarpFrequency, arpFrequencyL);
  }
  if (upperSW) {
    midiCCOut(CCarpFrequency, arpFrequencyU);
  }
}

void updateampVelocity() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Amp Velocity", "Low " + String(ampVelocitystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Amp Velocity", "Upp " + String(ampVelocitystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCampVelocity, ampVelocityL);
  }
  if (upperSW) {
    midiCCOut(CCampVelocity, ampVelocityU);
  }
}

void updatefilterVelocity() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Velocity", "Low " + String(filterVelocitystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Velocity", "Upp " + String(filterVelocitystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterVelocity, filterVelocityL);
  }
  if (upperSW) {
    midiCCOut(CCfilterVelocity, filterVelocityU);
  }
}

void updateampRelease() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Amp Release", "Low " + String(ampReleasestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Amp Release", "Upp " + String(ampReleasestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCampRelease, ampReleaseL);
  }
  if (upperSW) {
    midiCCOut(CCampRelease, ampReleaseU);
  }
}

void updateampSustain() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Amp Sustain", "Low " + String(ampSustainstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Amp Sustain", "Upp " + String(ampSustainstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCampSustain, ampSustainL);
  }
  if (upperSW) {
    midiCCOut(CCampSustain, ampSustainU);
  }
}

void updateampDecay() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Amp Decay", "Low " + String(ampDecaystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Amp Decay", "Upp " + String(ampDecaystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCampDecay, ampDecayL);
  }
  if (upperSW) {
    midiCCOut(CCampDecay, ampDecayU);
  }
}

void updateampAttack() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Amp Attack", "Low " + String(ampAttackstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Amp Attack", "Upp " + String(ampAttackstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCampAttack, ampAttackL);
  }
  if (upperSW) {
    midiCCOut(CCampAttack, ampAttackU);
  }
}

void updatefilterKeyboard() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Keyboard Tracl", "Low " + String(filterKeyboardstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("KeyBoard Track", "Upp " + String(filterKeyboardstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterKeyboard, filterKeyboardL);
  }
  if (upperSW) {
    midiCCOut(CCfilterKeyboard, filterKeyboardU);
  }
}

void updatefilterResonance() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Res", "Low " + String(filterResonancestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Res", "Upp " + String(filterResonancestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterResonance, filterResonanceL);
  }
  if (upperSW) {
    midiCCOut(CCfilterResonance, filterResonanceU);
  }
}

void updateosc2Volume() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Osc2 Volume", "Low " + String(osc2Volumestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Osc2 Volume", "Upp " + String(osc2Volumestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc2Volume, osc2VolumeL);
  }
  if (upperSW) {
    midiCCOut(CCosc2Volume, osc2VolumeU);
  }
}

void updateosc2PW() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Osc2 PW", "Low " + String(osc2PWstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Osc2 PW", "Upp " + String(osc2PWstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc2PW, osc2PWL);
  }
  if (upperSW) {
    midiCCOut(CCosc2PW, osc2PWU);
  }
}

void updateosc1PW() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Osc1 PW", "Low " + String(osc1PWstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Osc1 PW", "Upp " + String(osc1PWstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc1PW, osc1PWL);
  }
  if (upperSW) {
    midiCCOut(CCosc1PW, osc1PWU);
  }
}

void updateosc1Volume() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Osc1 Volume", "Low " + String(osc1Volumestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Osc1 Volume", "Upp " + String(osc1Volumestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc1Volume, osc1VolumeL);
  }
  if (upperSW) {
    midiCCOut(CCosc1Volume, osc1VolumeU);
  }
}

void updatefilterCutoff() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Cutoff", "Low " + String(filterCutoffstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Cutoff", "Upp " + String(filterCutoffstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterCutoff, filterCutoffL);
  }
  if (upperSW) {
    midiCCOut(CCfilterCutoff, filterCutoffU);
  }
}

void updatefilterEnvAmount() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Envelope", "Low " + String(filterEnvAmountstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Envelope", "Upp " + String(filterEnvAmountstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterEnvAmount, filterEnvAmountL);
  }
  if (upperSW) {
    midiCCOut(CCfilterEnvAmount, filterEnvAmountU);
  }
}

void updatefilterAttack() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Attack", "Low " + String(filterAttackstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Attack", "Upp " + String(filterAttackstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterAttack, filterAttackL);
  }
  if (upperSW) {
    midiCCOut(CCfilterAttack, filterAttackU);
  }
}

void updatefilterDecay() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Decay", "Low " + String(filterDecaystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Decay", "Upp " + String(filterDecaystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterDecay, filterDecayL);
  }
  if (upperSW) {
    midiCCOut(CCfilterDecay, filterDecayU);
  }
}

void updatefilterSustain() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Sustain", "Low " + String(filterSustainstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Sustain", "Upp " + String(filterSustainstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterSustain, filterSustainL);
  }
  if (upperSW) {
    midiCCOut(CCfilterSustain, filterSustainU);
  }
}

void updatefilterRelease() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Filter Release", "Low " + String(filterReleasestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Filter Release", "Upp " + String(filterReleasestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCfilterRelease, filterReleaseL);
  }
  if (upperSW) {
    midiCCOut(CCfilterRelease, filterReleaseU);
  }
}

void updateechoEQ() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Echo EQ", "Low " + String(echoEQstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Echo EQ", "Upp " + String(echoEQstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCechoEQ, echoEQL);
  }
  if (upperSW) {
    midiCCOut(CCechoEQ, echoEQU);
  }
}

void updateechoLevel() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Echo Level", "Low " + String(echoLevelstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Echo Level", "Upp " + String(echoLevelstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCechoLevel, echoLevelL);
  }
  if (upperSW) {
    midiCCOut(CCechoLevel, echoLevelU);
  }
}

void updateechoFeedback() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Echo Feedbck", "Low " + String(echoFeedbackstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Echo Feedbck", "Upp " + String(echoFeedbackstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCechoFeedback, echoFeedbackL);
  }
  if (upperSW) {
    midiCCOut(CCechoFeedback, echoFeedbackU);
  }
}

void updateechoSpread() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Echo Spread", "Low " + String(echoSpreadstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Echo Spread", "Upp " + String(echoSpreadstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCechoSpread, echoSpreadL);
  }
  if (upperSW) {
    midiCCOut(CCechoSpread, echoSpreadU);
  }
}

void updateechoTime() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Echo Time", "Low " + String(echoTimestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Echo Time", "Upp " + String(echoTimestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCechoTime, echoTimeL);
  }
  if (upperSW) {
    midiCCOut(CCechoTime, echoTimeU);
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
  midiCCOut(CClfo2UpperLower, lfo2Destination);
}

void updateunisonDetune() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Unison Detune", "Low " + String(unisonDetunestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Unison Detune", "Upp " + String(unisonDetunestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCunisonDetune, unisonDetuneL);
  }
  if (upperSW) {
    midiCCOut(CCunisonDetune, unisonDetuneU);
  }
}

void updateglideSpeed() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Glide Speed", "Low " + String(glideSpeedstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Glide Speed", "Upp " + String(glideSpeedstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCglideSpeed, glideSpeedL);
  }
  if (upperSW) {
    midiCCOut(CCglideSpeed, glideSpeedU);
  }
}

void updateosc1Transpose() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("OSC1 Trans", "Low " + String(osc1Transposestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("OSC1 Trans", "Upp " + String(osc1Transposestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc1Transpose, osc1TransposeL);
  }
  if (upperSW) {
    midiCCOut(CCosc1Transpose, osc1TransposeU);
  }
}

void updateosc2Transpose() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("OSC2 Trans", "Low " + String(osc2Transposestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("OSC2 Trans", "Upp " + String(osc2Transposestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc2Transpose, osc2TransposeL);
  }
  if (upperSW) {
    midiCCOut(CCosc2Transpose, osc2TransposeU);
  }
}

void updatenoiseLevel() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Noise Level", "Low " + String(noiseLevelstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Noise Level", "Upp " + String(noiseLevelstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCnoiseLevel, noiseLevelL);
  }
  if (upperSW) {
    midiCCOut(CCnoiseLevel, noiseLevelU);
  }
}

void updateglideAmount() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("Glide Amount", "Low " + String(glideAmountstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("Glide Amount", "Upp " + String(glideAmountstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCglideAmount, glideAmountL);
  }
  if (upperSW) {
    midiCCOut(CCglideAmount, glideAmountU);
  }
}

void updateosc1Tune() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("OSC1 Tune", "Low " + String(osc1Tunestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("OSC1 Tune", "Upp " + String(osc1Tunestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc1Tune, osc1TuneL);
  }
  if (upperSW) {
    midiCCOut(CCosc1Tune, osc1TuneU);
  }
}

void updateosc2Tune() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("OSC2 Tune", "Low " + String(osc2Tunestr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("OSC2 Tune", "Upp " + String(osc2Tunestr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CCosc2Tune, osc2TuneL);
  }
  if (upperSW) {
    midiCCOut(CCosc2Tune, osc2TuneU);
  }
}

void updatebendToFilter() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Bend to Filter", String(bendToFilterstr) + " %");
  }
  midiCCOut(CCbendToFilter, bendToFilter);
}

void updatelfo2ToFilter() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 to Filter", String(lfo2ToFilterstr) + " %");
  }
  midiCCOut(CClfo2ToFilter, lfo2ToFilter);
}

void updatebendToOsc() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("Bend to OSC", String(bendToOscstr) + " %");
  }
  midiCCOut(CCbendToOsc, bendToOsc);
}

void updatelfo2ToOsc() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 to OSC", String(lfo2ToOscstr) + " %");
  }
  midiCCOut(CClfo2ToOsc, lfo2ToOsc);
}

void updatelfo2FreqAcc() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 Freq Acc", String(lfo2FreqAccstr) + " %");
  }
  midiCCOut(CClfo2FreqAcc, lfo2FreqAcc);
}

void updatelfo2InitFrequency() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 Init Freq", String(lfo2InitFrequencystr) + " %");
  }
  midiCCOut(CClfo2InitFrequency, lfo2InitFrequency);
}

void updatelfo2InitAmount() {
  if (!recallPatchFlag) {
    showCurrentParameterPage("LFO2 Init Amnt", String(lfo2InitAmountstr) + " %");
  }
  midiCCOut(CClfo2InitAmount, lfo2InitAmount);
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
      showCurrentParameterPage("LFO1 Freq", "Low " + String(lfo1Frequencystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("LFO1 Freq", "Upp " + String(lfo1Frequencystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CClfo1Frequency, lfo1FrequencyL);
  }
  if (upperSW) {
    midiCCOut(CClfo1Frequency, lfo1FrequencyU);
  }
}

void updatelfo1DepthA() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("LFO1 Depth A", "Low " + String(lfo1DepthAstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("LFO1 Depth A", "Upp " + String(lfo1DepthAstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CClfo1DepthA, lfo1DepthAL);
  }
  if (upperSW) {
    midiCCOut(CClfo1DepthA, lfo1DepthAU);
  }
}

void updatelfo1Delay() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("LFO1 Delay", "Low " + String(lfo1Delaystr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("LFO1 Delay", "Upp " + String(lfo1Delaystr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CClfo1Delay, lfo1DelayL);
  }
  if (upperSW) {
    midiCCOut(CClfo1Delay, lfo1DelayU);
  }
}

void updatelfo1DepthB() {
  if (!recallPatchFlag) {
    if (lowerSW) {
      showCurrentParameterPage("LFO1 Depth B", "Low " + String(lfo1DepthBstr) + " %");
    }
    if (upperSW) {
      showCurrentParameterPage("LFO1 Depth B", "Upp " + String(lfo1DepthBstr) + " %");
    }
  }
  if (lowerSW) {
    midiCCOut(CClfo1DepthB, lfo1DepthBL);
  }
  if (upperSW) {
    midiCCOut(CClfo1DepthB, lfo1DepthBU);
  }
}


// void updateleadNPhigh() {
//   if (!lead2ndVoice) {
//     if (leadNPhigh == 1) {
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Note Priority", "High");
//       }
//       sr.writePin(LEAD_NP_HIGH_LED, HIGH);
//       sr.writePin(LEAD_NP_LOW_LED, LOW);
//       sr.writePin(LEAD_NP_LAST_LED, LOW);
//       leadNPlow = 0;
//       leadNPlast = 0;
//       midiCCOut(CCleadNPhigh, CC_ON);
//     }
//   }
// }

// void updateleadNPlast() {
//   if (!lead2ndVoice) {
//     if (leadNPlast == 1) {
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Note Priority", "Last");
//       }
//       sr.writePin(LEAD_NP_HIGH_LED, LOW);
//       sr.writePin(LEAD_NP_LOW_LED, LOW);
//       sr.writePin(LEAD_NP_LAST_LED, HIGH);
//       leadNPlow = 0;
//       leadNPhigh = 0;
//       midiCCOut(CCleadNPlast, CC_ON);
//     }
//   }
// }

// void updateleadNoteTrigger() {
//   if (leadNoteTrigger == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Note Trigger", "Single");
//     }
//     sr.writePin(LEAD_NOTE_TRIGGER_RED_LED, HIGH);
//     sr.writePin(LEAD_NOTE_TRIGGER_GREEN_LED, LOW);
//     midiCCOut(CCleadNoteTrigger, CC_ON);
//     midiCCOut(CCleadNoteTrigger, 0);
//   } else {
//     sr.writePin(LEAD_NOTE_TRIGGER_GREEN_LED, HIGH);
//     sr.writePin(LEAD_NOTE_TRIGGER_RED_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Note Trigger", "Multi");
//       midiCCOut(CCleadNoteTrigger, 127);
//       midiCCOut(CCleadNoteTrigger, 0);
//     }
//   }
// }

// void updateVCO2KBDTrk() {
//   if (vco2KBDTrk == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Keyboard Trk", "On");
//     }
//     sr.writePin(LEAD_VCO2_KBD_TRK_LED, HIGH);  // LED on
//     midiCCOut(CCvco2KBDTrk, CC_ON);
//     midiCCOut(CCvco2KBDTrk, 0);
//   } else {
//     sr.writePin(LEAD_VCO2_KBD_TRK_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Keyboard Trk", "Off");
//       midiCCOut(CCvco2KBDTrk, 127);
//       midiCCOut(CCvco2KBDTrk, 0);
//     }
//   }
// }

// void updatelead2ndVoice() {
//   if (lead2ndVoice == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("2nd Voice", "On");
//     }
//     sr.writePin(LEAD_SECOND_VOICE_LED, HIGH);  // LED on
//     sr.writePin(LEAD_NP_HIGH_LED, LOW);        // LED on
//     sr.writePin(LEAD_NP_LOW_LED, LOW);         // LED on
//     sr.writePin(LEAD_NP_LAST_LED, LOW);        // LED on
//     midiCCOut(CClead2ndVoice, 127);
//     midiCCOut(CClead2ndVoice, 0);
//     prevleadNPlow = leadNPlow;
//     prevleadNPhigh = leadNPhigh;
//     prevleadNPlast = leadNPlast;
//   } else {
//     sr.writePin(LEAD_SECOND_VOICE_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("2nd Voice", "Off");
//       if (!recallPatchFlag) {

//         leadNPlow = prevleadNPlow;
//         leadNPhigh = prevleadNPhigh;
//         leadNPlast = prevleadNPlast;

//         updateleadNPlow();
//         updateleadNPhigh();
//         updateleadNPlast();
//       }
//       midiCCOut(CClead2ndVoice, 127);
//       midiCCOut(CClead2ndVoice, 0);
//     }
//   }
// }

// void updateleadTrill() {
//   if (leadTrill == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Trill", "On");
//     }
//     sr.writePin(LEAD_TRILL_LED, HIGH);  // LED on
//     midiCCOut(CCleadTrill, 127);
//     midiCCOut(CCleadTrill, 0);
//   } else {
//     sr.writePin(LEAD_TRILL_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Trill", "Off");
//       midiCCOut(CCleadTrill, 127);
//       midiCCOut(CCleadTrill, 0);
//     }
//   }
// }

// void updateleadVCO1wave() {
//   switch (leadVCO1wave) {
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO1 Wave", "Square");
//       }
//       midiCCOut(CCleadVCO1wave, 127);
//       midiCCOut(CCleadVCO1wave, 0);
//       break;

//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO1 Wave", "Sine");
//       }
//       midiCCOut(CCleadVCO1wave, 127);
//       midiCCOut(CCleadVCO1wave, 0);
//       break;

//     case 3:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO1 Wave", "Triangle");
//       }
//       midiCCOut(CCleadVCO1wave, 127);
//       midiCCOut(CCleadVCO1wave, 0);
//       break;

//     case 4:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO1 Wave", "Noise");
//       }
//       midiCCOut(CCleadVCO1wave, 127);
//       midiCCOut(CCleadVCO1wave, 0);
//       break;

//     case 5:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO1 Wave", "Saw Up");
//       }
//       midiCCOut(CCleadVCO1wave, 127);
//       midiCCOut(CCleadVCO1wave, 0);
//       break;
//   }
// }

// void updateVCOWaves() {
//   for (int i = 0; i < leadVCO1wave; i++) {
//     midiCCOut(CCleadVCO1wave, 127);
//     midiCCOut(CCleadVCO1wave, 0);
//   }
//   for (int i = 0; i < leadVCO2wave; i++) {
//     midiCCOut(CCleadVCO2wave, 127);
//     midiCCOut(CCleadVCO2wave, 0);
//   }
//   for (int i = 0; i < polyWave; i++) {
//     midiCCOut(CCpolyWave, 127);
//     midiCCOut(CCpolyWave, 0);
//   }
// }

// void updateleadVCO2wave() {
//   switch (leadVCO2wave) {
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO2 Wave", "Saw Up");
//       }
//       midiCCOut(CCleadVCO2wave, 127);
//       midiCCOut(CCleadVCO2wave, 0);
//       break;

//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO2 Wave", "Square");
//       }
//       midiCCOut(CCleadVCO2wave, 127);
//       midiCCOut(CCleadVCO2wave, 0);
//       break;

//     case 3:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO2 Wave", "Sine");
//       }
//       midiCCOut(CCleadVCO2wave, 127);
//       midiCCOut(CCleadVCO2wave, 0);
//       break;

//     case 4:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO2 Wave", "Triangle");
//       }
//       midiCCOut(CCleadVCO2wave, 127);
//       midiCCOut(CCleadVCO2wave, 0);
//       break;

//     case 5:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO2 Wave", "Noise");
//       }
//       midiCCOut(CCleadVCO2wave, 127);
//       midiCCOut(CCleadVCO2wave, 0);
//       break;

//     case 6:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("VCO2 Wave", "Saw Down");
//       }
//       midiCCOut(CCleadVCO2wave, 127);
//       midiCCOut(CCleadVCO2wave, 0);
//       break;
//   }
// }

// void updatepolyWave() {
//   switch (polyWave) {
//     case 1:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Poly Wave", "Square");
//       }
//       midiCCOut(CCpolyWave, 127);
//       midiCCOut(CCpolyWave, 0);
//       break;

//     case 2:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Poly Wave", "Triangle");
//       }
//       midiCCOut(CCpolyWave, 127);
//       midiCCOut(CCpolyWave, 0);
//       break;

//     case 3:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Poly Wave", "Sine");
//       }
//       midiCCOut(CCpolyWave, 127);
//       midiCCOut(CCpolyWave, 0);
//       break;

//     case 4:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Poly Wave", "Tri-Saw");
//       }
//       midiCCOut(CCpolyWave, 127);
//       midiCCOut(CCpolyWave, 0);
//       break;

//     case 5:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Poly Wave", "Lumpy");
//       }
//       midiCCOut(CCpolyWave, 127);
//       midiCCOut(CCpolyWave, 0);
//       break;

//     case 6:
//       if (!recallPatchFlag) {
//         showCurrentParameterPage("Poly Wave", "Sawtooth");
//       }
//       midiCCOut(CCpolyWave, 127);
//       midiCCOut(CCpolyWave, 0);
//       break;
//   }
// }

// void updatepolyPWMSW() {
//   if (polyPWMSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("PWM Mod", "ADSR");
//     }
//     sr.writePin(POLY_PWM_RED_LED, HIGH);
//     sr.writePin(POLY_PWM_GREEN_LED, LOW);
//     midiCCOut(CCpolyPWMSW, 127);
//     midiCCOut(CCpolyPWMSW, 0);
//   } else {
//     sr.writePin(POLY_PWM_GREEN_LED, HIGH);
//     sr.writePin(POLY_PWM_RED_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("PWM Mod", "LFO");
//       midiCCOut(CCpolyPWMSW, 127);
//       midiCCOut(CCpolyPWMSW, 0);
//     }
//   }
// }

// void updateLFOTri() {
//   if (LFOTri == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wave", "Triangle");
//     }
//     sr.writePin(LFO_TRI_LED, HIGH);
//     sr.writePin(LFO_SAW_UP_LED, LOW);
//     sr.writePin(LFO_SAW_DN_LED, LOW);
//     sr.writePin(LFO_SQUARE_LED, LOW);
//     sr.writePin(LFO_SH_LED, LOW);
//     LFOSawUp = 0;
//     LFOSawDown = 0;
//     LFOSquare = 0;
//     LFOSH = 0;
//     midiCCOut(CCLFOTri, 127);
//   }
// }

// void updateLFOSawUp() {
//   if (LFOSawUp == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wave", "Saw Up");
//     }
//     sr.writePin(LFO_TRI_LED, LOW);
//     sr.writePin(LFO_SAW_UP_LED, HIGH);
//     sr.writePin(LFO_SAW_DN_LED, LOW);
//     sr.writePin(LFO_SQUARE_LED, LOW);
//     sr.writePin(LFO_SH_LED, LOW);
//     LFOTri = 0;
//     LFOSawDown = 0;
//     LFOSquare = 0;
//     LFOSH = 0;
//     midiCCOut(CCLFOSawUp, 127);
//   }
// }

// void updateLFOSawDown() {
//   if (LFOSawDown == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wave", "Saw Down");
//     }
//     sr.writePin(LFO_TRI_LED, LOW);
//     sr.writePin(LFO_SAW_UP_LED, LOW);
//     sr.writePin(LFO_SAW_DN_LED, HIGH);
//     sr.writePin(LFO_SQUARE_LED, LOW);
//     sr.writePin(LFO_SH_LED, LOW);
//     LFOTri = 0;
//     LFOSawUp = 0;
//     LFOSquare = 0;
//     LFOSH = 0;
//     midiCCOut(CCLFOSawDown, 127);
//   }
// }

// void updateLFOSquare() {
//   if (LFOSquare == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wave", "Square");
//     }
//     sr.writePin(LFO_TRI_LED, LOW);
//     sr.writePin(LFO_SAW_UP_LED, LOW);
//     sr.writePin(LFO_SAW_DN_LED, LOW);
//     sr.writePin(LFO_SQUARE_LED, HIGH);
//     sr.writePin(LFO_SH_LED, LOW);
//     LFOTri = 0;
//     LFOSawUp = 0;
//     LFOSawDown = 0;
//     LFOSH = 0;
//     midiCCOut(CCLFOSquare, 127);
//   }
// }

// void updateLFOSH() {
//   if (LFOSH == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("LFO Wave", "Sample & Hold");
//     }
//     sr.writePin(LFO_TRI_LED, LOW);
//     sr.writePin(LFO_SAW_UP_LED, LOW);
//     sr.writePin(LFO_SAW_DN_LED, LOW);
//     sr.writePin(LFO_SQUARE_LED, LOW);
//     sr.writePin(LFO_SH_LED, HIGH);
//     LFOTri = 0;
//     LFOSawUp = 0;
//     LFOSawDown = 0;
//     LFOSquare = 0;
//     midiCCOut(CCLFOSH, 127);
//   }
// }

// void updatestrings8() {
//   if (strings8 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings 8", "On");
//     }
//     sr.writePin(STRINGS_8_LED, HIGH);  // LED on
//     midiCCOut(CCstrings8, 127);
//     midiCCOut(CCstrings8, 0);
//   } else {
//     sr.writePin(STRINGS_8_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings 8", "Off");
//       midiCCOut(CCstrings8, 127);
//       midiCCOut(CCstrings8, 0);
//     }
//   }
// }

// void updatestrings4() {
//   if (strings4 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings 4", "On");
//     }
//     sr.writePin(STRINGS_4_LED, HIGH);  // LED on
//     midiCCOut(CCstrings4, 127);
//     midiCCOut(CCstrings4, 0);
//   } else {
//     sr.writePin(STRINGS_4_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings 4", "Off");
//       midiCCOut(CCstrings4, 127);
//       midiCCOut(CCstrings4, 0);
//     }
//   }
// }

// void updatepolyNoteTrigger() {
//   if (polyNoteTrigger == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Note Trigger", "Single");
//     }
//     sr.writePin(POLY_NOTE_TRIGGER_RED_LED, HIGH);
//     sr.writePin(POLY_NOTE_TRIGGER_GREEN_LED, LOW);
//     midiCCOut(CCpolyNoteTrigger, 127);
//     midiCCOut(CCpolyNoteTrigger, 0);
//   } else {
//     sr.writePin(POLY_NOTE_TRIGGER_GREEN_LED, HIGH);
//     sr.writePin(POLY_NOTE_TRIGGER_RED_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Note Trigger", "Multi");
//       midiCCOut(CCpolyNoteTrigger, 127);
//       midiCCOut(CCpolyNoteTrigger, 0);
//     }
//   }
// }

// void updatepolyVelAmp() {
//   if (polyVelAmp == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Amp velocity", "On");
//     }
//     sr.writePin(POLY_VEL_AMP_LED, HIGH);  // LED on
//     midiCCOut(CCpolyVelAmp, 127);
//     midiCCOut(CCpolyVelAmp, 0);
//   } else {
//     sr.writePin(POLY_VEL_AMP_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Amp Velocity", "Off");
//       midiCCOut(CCpolyVelAmp, 127);
//       midiCCOut(CCpolyVelAmp, 0);
//     }
//   }
// }

// void updatepolyDrift() {
//   if (polyDrift == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Drift", "On");
//     }
//     sr.writePin(POLY_DRIFT_LED, HIGH);  // LED on
//     midiCCOut(CCpolyDrift, 127);
//     midiCCOut(CCpolyDrift, 0);
//   } else {
//     sr.writePin(POLY_DRIFT_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Drift", "Off");
//       midiCCOut(CCpolyDrift, 127);
//       midiCCOut(CCpolyDrift, 0);
//     }
//   }
// }

// void updatepoly16() {
//   if (poly16 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly 16", "On");
//     }
//     sr.writePin(POLY_16_LED, HIGH);  // LED on
//     midiCCOut(CCpoly16, 127);
//     midiCCOut(CCpoly16, 0);
//   } else {
//     sr.writePin(POLY_16_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly 16", "Off");
//       midiCCOut(CCpoly16, 127);
//       midiCCOut(CCpoly16, 0);
//     }
//   }
// }

// void updatepoly8() {
//   if (poly8 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly 8", "On");
//     }
//     sr.writePin(POLY_8_LED, HIGH);  // LED on
//     midiCCOut(CCpoly8, 127);
//     midiCCOut(CCpoly8, 0);
//   } else {
//     sr.writePin(POLY_8_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly 8", "Off");
//       midiCCOut(CCpoly8, 127);
//       midiCCOut(CCpoly8, 0);
//     }
//   }
// }

// void updatepoly4() {
//   if (poly4 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly 4", "On");
//     }
//     sr.writePin(POLY_4_LED, HIGH);  // LED on
//     midiCCOut(CCpoly4, 127);
//     midiCCOut(CCpoly4, 0);
//   } else {
//     sr.writePin(POLY_4_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly 4", "Off");
//       midiCCOut(CCpoly4, 127);
//       midiCCOut(CCpoly4, 0);
//     }
//   }
// }

// void updateebass16() {
//   if (ebass16 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Elec Bass 16", "On");
//     }
//     sr.writePin(EBASS_16_LED, HIGH);  // LED on
//     midiCCOut(CCebass16, 127);
//     midiCCOut(CCebass16, 0);
//   } else {
//     sr.writePin(EBASS_16_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Elec Bass 16", "Off");
//       midiCCOut(CCebass16, 127);
//       midiCCOut(CCebass16, 0);
//     }
//   }
// }

// void updateebass8() {
//   if (ebass8 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Elec Bass 8", "On");
//     }
//     sr.writePin(EBASS_8_LED, HIGH);  // LED on
//     midiCCOut(CCebass8, 127);
//     midiCCOut(CCebass8, 0);
//   } else {
//     sr.writePin(EBASS_8_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Elec Bass 8", "Off");
//       midiCCOut(CCebass8, 127);
//       midiCCOut(CCebass8, 0);
//     }
//   }
// }

// void updatebassNoteTrigger() {
//   if (bassNoteTrigger == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Note Trigger", "Single");
//     }
//     sr.writePin(BASS_NOTE_TRIGGER_RED_LED, HIGH);
//     sr.writePin(BASS_NOTE_TRIGGER_GREEN_LED, LOW);
//     midiCCOut(CCbassNoteTrigger, 127);
//     midiCCOut(CCbassNoteTrigger, 0);
//   } else {
//     sr.writePin(BASS_NOTE_TRIGGER_GREEN_LED, HIGH);
//     sr.writePin(BASS_NOTE_TRIGGER_RED_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Note Trigger", "Multi");
//       midiCCOut(CCbassNoteTrigger, 127);
//       midiCCOut(CCbassNoteTrigger, 0);
//     }
//   }
// }

// void updatestringbass16() {
//   if (stringbass16 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Bass 16", "On");
//     }
//     sr.writePin(STRINGS_BASS_16_LED, HIGH);  // LED on
//     midiCCOut(CCstringbass16, 127);
//     midiCCOut(CCstringbass16, 0);
//   } else {
//     sr.writePin(STRINGS_BASS_16_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Bass 16", "Off");
//       midiCCOut(CCstringbass16, 127);
//       midiCCOut(CCstringbass16, 0);
//     }
//   }
// }

// void updatestringbass8() {
//   if (stringbass8 == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Bass 8", "On");
//     }
//     sr.writePin(STRINGS_BASS_8_LED, HIGH);  // LED on
//     midiCCOut(CCstringbass8, 127);
//     midiCCOut(CCstringbass8, 0);
//   } else {
//     sr.writePin(STRINGS_BASS_8_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("String Bass 8", "Off");
//       midiCCOut(CCstringbass8, 127);
//       midiCCOut(CCstringbass8, 0);
//     }
//   }
// }

// void updatehollowWave() {
//   if (hollowWave == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Hollow Wave", "On");
//     }
//     sr.writePin(HOLLOW_WAVE_LED, HIGH);  // LED on
//     midiCCOut(CChollowWave, 127);
//     midiCCOut(CChollowWave, 0);
//   } else {
//     sr.writePin(HOLLOW_WAVE_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Hollow Wave", "Off");
//       midiCCOut(CChollowWave, 127);
//       midiCCOut(CChollowWave, 0);
//     }
//   }
// }

// void updatebassPitchSW() {
//   if (bassPitchSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass PB", "On");
//     }
//     sr.writePin(BASS_PITCH_LED, HIGH);  // LED on
//     midiCCOut(CCbassPitchSW, 127);
//     midiCCOut(CCbassPitchSW, 0);
//   } else {
//     sr.writePin(BASS_PITCH_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass PB", "Off");
//       midiCCOut(CCbassPitchSW, 127);
//       midiCCOut(CCbassPitchSW, 0);
//     }
//   }
// }

// void updatestringsPitchSW() {
//   if (stringsPitchSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings PB", "On");
//     }
//     sr.writePin(STRINGS_PITCH_LED, HIGH);  // LED on
//     midiCCOut(CCstringsPitchSW, 127);
//     midiCCOut(CCstringsPitchSW, 0);
//   } else {
//     sr.writePin(STRINGS_PITCH_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings PB", "Off");
//       midiCCOut(CCstringsPitchSW, 127);
//       midiCCOut(CCstringsPitchSW, 0);
//     }
//   }
// }

// void updatepolyPitchSW() {
//   if (polyPitchSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Pitch PB", "On");
//     }
//     sr.writePin(POLY_PITCH_LED, HIGH);  // LED on
//     midiCCOut(CCpolyPitchSW, 127);
//     midiCCOut(CCpolyPitchSW, 0);
//   } else {
//     sr.writePin(POLY_PITCH_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Pitch PB", "Off");
//       midiCCOut(CCpolyPitchSW, 127);
//       midiCCOut(CCpolyPitchSW, 0);
//     }
//   }
// }

// void updatepolyVCFSW() {
//   if (polyVCFSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly VCF PB", "On");
//     }
//     sr.writePin(POLY_VCF_LED, HIGH);  // LED on
//     midiCCOut(CCpolyVCFSW, 127);
//     midiCCOut(CCpolyVCFSW, 0);
//   } else {
//     sr.writePin(POLY_VCF_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly VCF PB", "Off");
//       midiCCOut(CCpolyVCFSW, 127);
//       midiCCOut(CCpolyVCFSW, 0);
//     }
//   }
// }

// void updateleadPitchSW() {
//   if (leadPitchSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Pitch PB", "On");
//     }
//     sr.writePin(LEAD_PITCH_LED, HIGH);  // LED on
//     midiCCOut(CCleadPitchSW, 127);
//     midiCCOut(CCleadPitchSW, 0);
//   } else {
//     sr.writePin(LEAD_PITCH_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Pitch PB", "Off");
//       midiCCOut(CCleadPitchSW, 127);
//       midiCCOut(CCleadPitchSW, 0);
//     }
//   }
// }

// void updateleadVCFSW() {
//   if (leadVCFSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead VCF PB", "On");
//     }
//     sr.writePin(LEAD_VCF_LED, HIGH);  // LED on
//     midiCCOut(CCleadVCFSW, 127);
//     midiCCOut(CCleadVCFSW, 0);
//   } else {
//     sr.writePin(LEAD_VCF_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead VCF PB", "Off");
//       midiCCOut(CCleadVCFSW, 127);
//       midiCCOut(CCleadVCFSW, 0);
//     }
//   }
// }

// void updatepolyAfterSW() {
//   if (polyAfterSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly AfterT", "Vol / Brill");
//     }
//     sr.writePin(POLY_TOUCH_DEST_RED_LED, HIGH);
//     sr.writePin(POLY_TOUCH_DEST_GREEN_LED, LOW);
//     midiCCOut(CCpolyAfterSW, 127);
//     midiCCOut(CCpolyAfterSW, 0);
//   } else {
//     sr.writePin(POLY_TOUCH_DEST_GREEN_LED, HIGH);
//     sr.writePin(POLY_TOUCH_DEST_RED_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly AfterT", "PitchBend");
//       midiCCOut(CCpolyAfterSW, 127);
//       midiCCOut(CCpolyAfterSW, 0);
//     }
//   }
// }

// void updateleadAfterSW() {
//   if (leadAfterSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead AfterT", "Vol / Brill");
//     }
//     sr.writePin(LEAD_TOUCH_DEST_RED_LED, HIGH);
//     sr.writePin(LEAD_TOUCH_DEST_GREEN_LED, LOW);
//     midiCCOut(CCleadAfterSW, 127);
//     midiCCOut(CCleadAfterSW, 0);
//   } else {
//     sr.writePin(LEAD_TOUCH_DEST_GREEN_LED, HIGH);
//     sr.writePin(LEAD_TOUCH_DEST_RED_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead AfterT", "PitchBend");
//       midiCCOut(CCleadAfterSW, 127);
//       midiCCOut(CCleadAfterSW, 0);
//     }
//   }
// }

// void updatephaserBassSW() {
//   if (phaserBassSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Phaser", "On");
//     }
//     sr.writePin(PHASE_BASS_LED, HIGH);  // LED on
//     midiCCOut(CCphaserBassSW, 127);
//     midiCCOut(CCphaserBassSW, 0);
//   } else {
//     sr.writePin(PHASE_BASS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Phaser", "Off");
//       midiCCOut(CCphaserBassSW, 127);
//       midiCCOut(CCphaserBassSW, 0);
//     }
//   }
// }

// void updatephaserStringsSW() {
//   if (phaserStringsSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Phaser", "On");
//     }
//     sr.writePin(PHASE_STRINGS_LED, HIGH);  // LED on
//     midiCCOut(CCphaserStringsSW, 127);
//     midiCCOut(CCphaserStringsSW, 0);
//   } else {
//     sr.writePin(PHASE_STRINGS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Phaser", "Off");
//       midiCCOut(CCphaserStringsSW, 127);
//       midiCCOut(CCphaserStringsSW, 0);
//     }
//   }
// }

// void updatephaserPolySW() {
//   if (phaserPolySW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Phaser", "On");
//     }
//     sr.writePin(PHASE_POLY_LED, HIGH);  // LED on
//     midiCCOut(CCphaserPolySW, 127);
//     midiCCOut(CCphaserPolySW, 0);
//   } else {
//     sr.writePin(PHASE_POLY_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Phaser", "Off");
//       midiCCOut(CCphaserPolySW, 127);
//       midiCCOut(CCphaserPolySW, 0);
//     }
//   }
// }

// void updatephaserLeadSW() {
//   if (phaserLeadSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Phaser", "On");
//     }
//     sr.writePin(PHASE_LEAD_LED, HIGH);  // LED on
//     midiCCOut(CCphaserLeadSW, 127);
//   } else {
//     sr.writePin(PHASE_LEAD_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Phaser", "Off");
//       midiCCOut(CCphaserLeadSW, 127);
//     }
//   }
// }

// void updatechorusBassSW() {
//   if (chorusBassSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Chorus", "On");
//     }
//     sr.writePin(CHORUS_BASS_LED, HIGH);  // LED on
//     midiCCOut(CCchorusBassSW, 127);
//   } else {
//     sr.writePin(CHORUS_BASS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Chorus", "Off");
//       midiCCOut(CCchorusBassSW, 127);
//     }
//   }
// }

// void updatechorusStringsSW() {
//   if (chorusStringsSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Chorus", "On");
//     }
//     sr.writePin(CHORUS_STRINGS_LED, HIGH);  // LED on
//     midiCCOut(CCchorusStringsSW, 127);
//   } else {
//     sr.writePin(CHORUS_STRINGS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Chorus", "Off");
//       midiCCOut(CCchorusStringsSW, 127);
//     }
//   }
// }

// void updatechorusPolySW() {
//   if (chorusPolySW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Chorus", "On");
//     }
//     sr.writePin(CHORUS_POLY_LED, HIGH);  // LED on
//     midiCCOut(CCchorusPolySW, 127);
//   } else {
//     sr.writePin(CHORUS_POLY_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Chorus", "Off");
//       midiCCOut(CCchorusPolySW, 127);
//     }
//   }
// }

// void updatechorusLeadSW() {
//   if (chorusLeadSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Chorus", "On");
//     }
//     sr.writePin(CHORUS_LEAD_LED, HIGH);  // LED on
//     midiCCOut(CCchorusLeadSW, 127);
//   } else {
//     sr.writePin(CHORUS_LEAD_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Chorus", "Off");
//       midiCCOut(CCchorusLeadSW, 127);
//     }
//   }
// }

// void updateechoBassSW() {
//   if (echoBassSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Echo", "On");
//     }
//     sr.writePin(ECHO_BASS_LED, HIGH);  // LED on
//     midiCCOut(CCechoBassSW, 127);
//   } else {
//     sr.writePin(ECHO_BASS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Echo", "Off");
//       midiCCOut(CCechoBassSW, 127);
//     }
//   }
// }

// void updateechoStringsSW() {
//   if (echoStringsSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Echo", "On");
//     }
//     sr.writePin(ECHO_STRINGS_LED, HIGH);  // LED on
//     midiCCOut(CCechoStringsSW, 127);
//   } else {
//     sr.writePin(ECHO_STRINGS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Echo", "Off");
//       midiCCOut(CCechoStringsSW, 127);
//     }
//   }
// }

// void updateechoPolySW() {
//   if (echoPolySW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Echo", "On");
//     }
//     sr.writePin(ECHO_POLY_LED, HIGH);  // LED on
//     midiCCOut(CCechoPolySW, 127);
//   } else {
//     sr.writePin(ECHO_POLY_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Echo", "Off");
//       midiCCOut(CCechoPolySW, 127);
//     }
//   }
// }

// void updateechoLeadSW() {
//   if (echoLeadSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Echo", "On");
//     }
//     sr.writePin(ECHO_LEAD_LED, HIGH);  // LED on
//     midiCCOut(CCechoLeadSW, 127);
//   } else {
//     sr.writePin(ECHO_LEAD_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Echo", "Off");
//       midiCCOut(CCechoLeadSW, 127);
//     }
//   }
// }

// void updatereverbBassSW() {
//   if (reverbBassSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Reverb", "On");
//     }
//     sr.writePin(REVERB_BASS_LED, HIGH);  // LED on
//     midiCCOut(CCreverbBassSW, 127);
//   } else {
//     sr.writePin(REVERB_BASS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Bass Reverb", "Off");
//       midiCCOut(CCreverbBassSW, 127);
//     }
//   }
// }

// void updatereverbStringsSW() {
//   if (reverbStringsSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Reverb", "On");
//     }
//     sr.writePin(REVERB_STRINGS_LED, HIGH);  // LED on
//     midiCCOut(CCreverbStringsSW, 127);
//   } else {
//     sr.writePin(REVERB_STRINGS_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Strings Reverb", "Off");
//       midiCCOut(CCreverbStringsSW, 127);
//     }
//   }
// }

// void updatereverbPolySW() {
//   if (reverbPolySW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Reverb", "On");
//     }
//     sr.writePin(REVERB_POLY_LED, HIGH);  // LED on
//     midiCCOut(CCreverbPolySW, 127);
//   } else {
//     sr.writePin(REVERB_POLY_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Poly Reverb", "Off");
//       midiCCOut(CCreverbPolySW, 127);
//     }
//   }
// }

// void updatereverbLeadSW() {
//   if (reverbLeadSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Reverb", "On");
//     }
//     sr.writePin(REVERB_LEAD_LED, HIGH);  // LED on
//     midiCCOut(CCreverbLeadSW, 127);
//   } else {
//     sr.writePin(REVERB_LEAD_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Lead Reverb", "Off");
//       midiCCOut(CCreverbLeadSW, 127);
//     }
//   }
// }

// void updatearpOnSW() {
//   if (arpOnSW == 1) {
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Arpeggiator", "On");
//     }
//     sr.writePin(ARP_ON_OFF_LED, HIGH);  // LED on
//     midiCCOut(CCarpOnSW, 127);
//   } else {
//     sr.writePin(ARP_ON_OFF_LED, LOW);  // LED off
//     if (!recallPatchFlag) {
//       showCurrentParameterPage("Arpeggiator", "Off");
//       midiCCOut(CCarpOnSW, 0);
//     }
//   }
// }

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
    midiCCOut(CCarpRange4SW, 127);
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
    midiCCOut(CCarpRange3SW, 127);
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
    arpRange2SWL = 0;
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
    midiCCOut(CCarpRange2SW, 127);
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
    midiCCOut(CCarpRange1SW, 127);
  }
}

void updatearpSyncSW() {

  if (arpSyncSWL && lowerSW) {
    green.writePin(GREEN_ARP_SYNC_LED, HIGH);
    sr.writePin(ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Lower On");
    }
  }
  if (!arpSyncSWL && lowerSW) {
    green.writePin(GREEN_ARP_SYNC_LED, LOW);
    sr.writePin(ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Lower Off");
    }
  }
  if (arpSyncSWU && upperSW) {
    sr.writePin(ARP_SYNC_LED, HIGH);
    green.writePin(GREEN_ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Upper On");
    }
  }
  if (!arpSyncSWU && upperSW) {
    sr.writePin(ARP_SYNC_LED, LOW);
    green.writePin(GREEN_ARP_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Sync", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCarpSyncSW, 127);
  }
}

void updatearpHoldSW() {

  if (arpHoldSWL && lowerSW) {
    green.writePin(GREEN_ARP_HOLD_LED, HIGH);
    sr.writePin(ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Lower On");
    }
  }
  if (!arpHoldSWL && lowerSW) {
    green.writePin(GREEN_ARP_HOLD_LED, LOW);
    sr.writePin(ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Lower Off");
    }
  }
  if (arpHoldSWU && upperSW) {
    sr.writePin(ARP_HOLD_LED, HIGH);
    green.writePin(GREEN_ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Upper On");
    }
  }
  if (!arpHoldSWU && upperSW) {
    sr.writePin(ARP_HOLD_LED, LOW);
    green.writePin(GREEN_ARP_HOLD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("ARP Hold", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCarpHoldSW, 127);
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
      showCurrentParameterPage("Layer Solo", "Lower On");
    }
  }
  if (!layerSoloSW) {
    green.writePin(GREEN_LAYER_SOLO_LED, LOW);
    sr.writePin(LAYER_SOLO_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Layer Solo", "Lower Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CClayerSoloSW, 127);
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
    midiCCOut(CClowerSW, 127);
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
    midiCCOut(CCupperSW, 127);
    switchLEDs();
  }
}

void updateUtilitySW() {
  if (utilitySW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Utility", "On");
    }
    midiCCOut(CCutilitySW, 127);
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
    midiCCOut(CCarpRandSW, 127);
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
    midiCCOut(CCarpUpDownSW, 127);
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
    midiCCOut(CCarpDownSW, 127);
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
    midiCCOut(CCarpUpSW, 127);
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
    midiCCOut(CCarpOffSW, 127);
  }
}

void updateenvInvSW() {

  if (envInvSWL && lowerSW) {
    green.writePin(GREEN_ENV_INV_LED, HIGH);
    sr.writePin(ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Lower On");
    }
  }
  if (!envInvSWL && lowerSW) {
    green.writePin(GREEN_ENV_INV_LED, LOW);
    sr.writePin(ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Lower Off");
    }
  }
  if (envInvSWU && upperSW) {
    sr.writePin(ENV_INV_LED, HIGH);
    green.writePin(GREEN_ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Upper On");
    }
  }
  if (!envInvSWU && upperSW) {
    sr.writePin(ENV_INV_LED, LOW);
    green.writePin(GREEN_ENV_INV_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Env Invert", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCenvInvSW, 127);
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
    midiCCOut(CCfilterHPSW, 127);
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
    midiCCOut(CCfilterBP2SW, 127);
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
    midiCCOut(CCfilterBP1SW, 127);
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
    midiCCOut(CCfilterLP2SW, 127);
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
    midiCCOut(CCfilterLP1SW, 127);
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
    midiCCOut(CCrevGLTCSW, 127);
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
    midiCCOut(CCrevHallSW, 127);
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
    midiCCOut(CCrevPlateSW, 127);
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
    midiCCOut(CCrevRoomSW, 127);
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
    midiCCOut(CCrevOffSW, 127);
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
    midiCCOut(CCnoisePinkSW, 127);
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
    midiCCOut(CCnoiseWhiteSW, 127);
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
    midiCCOut(CCnoiseOffSW, 127);
  }
}

void updateechoSyncSW() {

  if (echoSyncSWL && lowerSW) {
    green.writePin(GREEN_ECHO_SYNC_LED, HIGH);
    sr.writePin(ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Lower On");
    }
  }
  if (!echoSyncSWL && lowerSW) {
    green.writePin(GREEN_ECHO_SYNC_LED, LOW);
    sr.writePin(ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Lower Off");
    }
  }
  if (echoSyncSWU && upperSW) {
    sr.writePin(ECHO_SYNC_LED, HIGH);
    green.writePin(GREEN_ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Upper On");
    }
  }
  if (!echoSyncSWU && upperSW) {
    sr.writePin(ECHO_SYNC_LED, LOW);
    green.writePin(GREEN_ECHO_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Echo Sync", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCechoSyncSW, 127);
  }
}

void updateosc1ringModSW() {

  if (osc1ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC1_RINGMOD_LED, HIGH);
    sr.writePin(OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Lower On");
    }
  }
  if (!osc1ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC1_RINGMOD_LED, LOW);
    sr.writePin(OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Lower Off");
    }
  }
  if (osc1ringModSWU && upperSW) {
    sr.writePin(OSC1_RINGMOD_LED, HIGH);
    green.writePin(GREEN_OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Upper On");
    }
  }
  if (!osc1ringModSWU && upperSW) {
    sr.writePin(OSC1_RINGMOD_LED, LOW);
    green.writePin(GREEN_OSC1_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 RM", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCosc1ringModSW, 127);
  }
}

void updateosc2ringModSW() {

  if (osc2ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC2_RINGMOD_LED, HIGH);
    sr.writePin(OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Lower On");
    }
  }
  if (!osc2ringModSWL && lowerSW) {
    green.writePin(GREEN_OSC2_RINGMOD_LED, LOW);
    sr.writePin(OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Lower Off");
    }
  }
  if (osc2ringModSWU && upperSW) {
    sr.writePin(OSC2_RINGMOD_LED, HIGH);
    green.writePin(GREEN_OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Upper On");
    }
  }
  if (!osc2ringModSWU && upperSW) {
    sr.writePin(OSC2_RINGMOD_LED, LOW);
    green.writePin(GREEN_OSC2_RINGMOD_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 RM", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCosc2ringModSW, 127);
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
    midiCCOut(CCosc1_osc2PWMSW, 127);
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
    midiCCOut(CCosc1pulseSW, 127);
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
    midiCCOut(CCosc1squareSW, 127);
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
    midiCCOut(CCosc1sawSW, 127);
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
    midiCCOut(CCosc1triangleSW, 127);
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
    midiCCOut(CCosc2_osc1PWMSW, 127);
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
    midiCCOut(CCosc2pulseSW, 127);
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
    midiCCOut(CCosc2squareSW, 127);
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
    midiCCOut(CCosc2sawSW, 127);
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
    midiCCOut(CCosc2triangleSW, 127);
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
    midiCCOut(CCechoPingPongSW, 127);
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
    midiCCOut(CCechoTapeSW, 127);
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
    midiCCOut(CCechoSTDSW, 127);
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
    midiCCOut(CCechoOffSW, 127);
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
    midiCCOut(CCchorus3SW, 127);
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
    midiCCOut(CCchorus2SW, 127);
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
    midiCCOut(CCchorus1SW, 127);
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
    midiCCOut(CCchorusOffSW, 127);
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
    midiCCOut(CCosc1_1SW, 127);
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
    midiCCOut(CCosc1_2SW, 127);
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
    midiCCOut(CCosc1_4SW, 127);
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
    midiCCOut(CCosc1_8SW, 127);
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
    midiCCOut(CCosc1_16SW, 127);
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
    midiCCOut(CCosc2_1SW, 127);
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
    midiCCOut(CCosc2_2SW, 127);
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
    midiCCOut(CCosc2_4SW, 127);
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
    midiCCOut(CCosc2_8SW, 127);
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
    midiCCOut(CCosc2_16SW, 127);
  }
}

void updateosc1glideSW() {

  if (osc1glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC1_LED, HIGH);
    sr.writePin(GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Lower On");
    }
  }
  if (!osc1glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC1_LED, LOW);
    sr.writePin(GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Lower Off");
    }
  }
  if (osc1glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC1_LED, HIGH);
    green.writePin(GREEN_GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Upper On");
    }
  }
  if (!osc1glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC1_LED, LOW);
    green.writePin(GREEN_GLIDE_OSC1_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC1 Glide", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCosc1glideSW, 127);
  }
}

void updateosc2glideSW() {

  if (osc2glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC2_LED, HIGH);
    sr.writePin(GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Lower On");
    }
  }
  if (!osc2glideSWL && lowerSW) {
    green.writePin(GREEN_GLIDE_OSC2_LED, LOW);
    sr.writePin(GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Lower Off");
    }
  }
  if (osc2glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC2_LED, HIGH);
    green.writePin(GREEN_GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Upper On");
    }
  }
  if (!osc2glideSWU && upperSW) {
    sr.writePin(GLIDE_OSC2_LED, LOW);
    green.writePin(GREEN_GLIDE_OSC2_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Glide", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCosc2glideSW, 127);
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
    midiCCOut(CCportSW, 127);
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
    midiCCOut(CCglideSW, 127);
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
    midiCCOut(CCglideOffSW, 127);
  }
}

void updateosc2SyncSW() {

  if (osc2SyncSWL && lowerSW) {
    green.writePin(GREEN_OSC2_SYNC_LED, HIGH);
    sr.writePin(OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Lower On");
    }
  }
  if (!osc2SyncSWL && lowerSW) {
    green.writePin(GREEN_OSC2_SYNC_LED, LOW);
    sr.writePin(OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Lower Off");
    }
  }
  if (osc2SyncSWU && upperSW) {
    sr.writePin(OSC2_SYNC_LED, HIGH);
    green.writePin(GREEN_OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Upper On");
    }
  }
  if (!osc2SyncSWU && upperSW) {
    sr.writePin(OSC2_SYNC_LED, LOW);
    green.writePin(GREEN_OSC2_SYNC_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("OSC2 Sync", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCosc2SyncSW, 127);
  }
}

void updatemultiTriggerSW() {

  if (multiTriggerSWL && lowerSW) {
    green.writePin(GREEN_MULTI_LED, HIGH);
    sr.writePin(MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Lower On");
    }
  }
  if (!multiTriggerSWL && lowerSW) {
    green.writePin(GREEN_MULTI_LED, LOW);
    sr.writePin(MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Lower Off");
    }
  }
  if (multiTriggerSWU && upperSW) {
    sr.writePin(MULTI_LED, HIGH);
    green.writePin(GREEN_MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Upper On");
    }
  }
  if (!multiTriggerSWU && upperSW) {
    sr.writePin(MULTI_LED, LOW);
    green.writePin(GREEN_MULTI_LED, LOW);
    if (!recallPatchFlag) {
      showCurrentParameterPage("Multi Trig", "Upper Off");
    }
  }

  if (!layerPatchFlag) {
    midiCCOut(CCmultiTriggerSW, 127);
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
    doubleSW = 0;
    splitSW = 0;
    upperSW = 1;
    lowerSW = 0;
    recallPatchFlag = true;
    updateUpperSW();
    recallPatchFlag = false;
    midiCCOut(CCsingleSW, 127);
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
    singleSW = 0;
    splitSW = 0;
    midiCCOut(CCdoubleSW, 127);
  }
}

void updatesplitSW() {
  if (splitSW == 1) {
    if (!recallPatchFlag) {
      showCurrentParameterPage("Split", "On");
    }
    sr.writePin(SINGLE_LED, LOW);
    sr.writePin(DOUBLE_LED, LOW);
    sr.writePin(SPLIT_LED, HIGH);
    singleSW = 0;
    doubleSW = 0;
    midiCCOut(CCsplitSW, 127);
  }
}

void switchLEDs() {
  layerPatchFlag = true;
  recallPatchFlag = true;
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


  recallPatchFlag = false;
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

      // case CClfoSpeed:
      //   lfoSpeed = value;
      //   if (lfoSync < 63) {
      //     lfoSpeedstr = QUADRALFO[value];
      //   } else {
      //     lfoSpeedmap = map(lfoSpeed, 0, 127, 0, 19);
      //     lfoSpeedstring = QUADRAARPSYNC[lfoSpeedmap];
      //   }
      //   updatelfoSpeed();
      //   break;


      // case CCechoTime:
      //   echoTime = value;
      //   if (echoSync < 63) {
      //     echoTimestr = QUADRAECHOTIME[value];
      //   }
      //   if (echoSync > 63) {
      //     echoTimemap = map(echoTime, 0, 127, 0, 19);
      //     echoTimestring = QUADRAECHOSYNC[echoTimemap];
      //   }
      //   updateechoTime();
      //   break;


      // case CCarpSpeed:
      //   arpSpeed = value;
      //   if (arpSync < 63) {
      //     arpSpeedstr = QUADRAARPSPEED[value];
      //   } else {
      //     arpSpeedmap = map(arpSpeed, 0, 127, 0, 19);
      //     arpSpeedstring = QUADRAARPSYNC[arpSpeedmap];
      //   }
      //   updatearpSpeed();
      //   break;


    case CCmasterVolume:
      masterVolume = value;
      masterVolumestr = QUADRA100[value];
      updatemasterVolume();
      break;

    case CCmasterTune:
      masterTune = value;
      masterTunestr = QUADRAETUNE[value];
      updatemasterTune();
      break;

    case CClayerPan:
      layerPan = value;
      layerPanstr = QUADRA100[value];
      updatelayerPan();
      break;

    case CClayerVolume:
      if (lowerSW) {
        layerVolumeL = value;
      }
      if (upperSW) {
        layerVolumeU = value;
      }
      layerVolumestr = QUADRA100[value];
      updatelayerVolume();
      break;

    case CCreverbLevel:
      if (lowerSW) {
        reverbLevelL = value;
      }
      if (upperSW) {
        reverbLevelU = value;
      }
      reverbLevelstr = QUADRA100[value];
      updatereverbLevel();
      break;

    case CCreverbDecay:
      if (lowerSW) {
        reverbDecayL = value;
      }
      if (upperSW) {
        reverbDecayU = value;
      }
      reverbDecaystr = QUADRA100[value];
      updatereverbDecay();
      break;

    case CCreverbEQ:
      if (lowerSW) {
        reverbEQL = value;
      }
      if (upperSW) {
        reverbEQU = value;
      }
      reverbEQstr = QUADRA100[value];
      updatereverbEQ();
      break;

    case CCarpFrequency:
      if (lowerSW) {
        arpFrequencyL = value;
      }
      if (upperSW) {
        arpFrequencyU = value;
      }
      arpFrequencystr = QUADRALFO[value];
      updatearpFrequency();
      break;

    case CCampVelocity:
      if (lowerSW) {
        ampVelocityL = value;
      }
      if (upperSW) {
        ampVelocityU = value;
      }
      ampVelocitystr = QUADRA100[value];
      updateampVelocity();
      break;

    case CCfilterVelocity:
      if (lowerSW) {
        filterVelocityL = value;
      }
      if (upperSW) {
        filterVelocityU = value;
      }
      filterVelocitystr = QUADRA100[value];
      updatefilterVelocity();
      break;

    case CCampRelease:
      if (lowerSW) {
        ampReleaseL = value;
      }
      if (upperSW) {
        ampReleaseU = value;
      }
      ampReleasestr = QUADRA100[value];
      updateampRelease();
      break;

    case CCampSustain:
      if (lowerSW) {
        ampSustainL = value;
      }
      if (upperSW) {
        ampSustainU = value;
      }
      ampSustainstr = QUADRA100[value];
      updateampSustain();
      break;

    case CCampDecay:
      if (lowerSW) {
        ampDecayL = value;
      }
      if (upperSW) {
        ampDecayU = value;
      }
      ampDecaystr = QUADRA100[value];
      updateampDecay();
      break;

    case CCampAttack:
      if (lowerSW) {
        ampAttackL = value;
      }
      if (upperSW) {
        ampAttackU = value;
      }
      ampAttackstr = QUADRA100[value];
      updateampAttack();
      break;

    case CCfilterKeyboard:
      if (lowerSW) {
        filterKeyboardL = value;
      }
      if (upperSW) {
        filterKeyboardU = value;
      }
      filterKeyboardstr = QUADRA100[value];
      updatefilterKeyboard();
      break;

    case CCfilterResonance:
      if (lowerSW) {
        filterResonanceL = value;
      }
      if (upperSW) {
        filterResonanceU = value;
      }
      filterResonancestr = QUADRA100[value];
      updatefilterResonance();
      break;

    case CCosc2Volume:
      if (lowerSW) {
        osc2VolumeL = value;
      }
      if (upperSW) {
        osc2VolumeU = value;
      }
      osc2Volumestr = QUADRA100[value];
      updateosc2Volume();
      break;

    case CCosc2PW:
      if (lowerSW) {
        osc2PWL = value;
      }
      if (upperSW) {
        osc2PWU = value;
      }
      osc2PWstr = QUADRA100[value];
      updateosc2PW();
      break;

    case CCosc1PW:
      if (lowerSW) {
        osc1PWL = value;
      }
      if (upperSW) {
        osc1PWU = value;
      }
      osc1PWstr = QUADRA100[value];
      updateosc1PW();
      break;

    case CCosc1Volume:
      if (lowerSW) {
        osc1VolumeL = value;
      }
      if (upperSW) {
        osc1VolumeU = value;
      }
      osc1Volumestr = QUADRA100[value];
      updateosc1Volume();
      break;

    case CCfilterCutoff:
      if (lowerSW) {
        filterCutoffL = value;
      }
      if (upperSW) {
        filterCutoffU = value;
      }
      filterCutoffstr = QUADRA100[value];
      updatefilterCutoff();
      break;

    case CCfilterEnvAmount:
      if (lowerSW) {
        filterEnvAmountL = value;
      }
      if (upperSW) {
        filterEnvAmountU = value;
      }
      filterEnvAmountstr = QUADRA100[value];
      updatefilterEnvAmount();
      break;

    case CCfilterAttack:
      if (lowerSW) {
        filterAttackL = value;
      }
      if (upperSW) {
        filterAttackU = value;
      }
      filterAttackstr = QUADRA100[value];
      updatefilterAttack();
      break;

    case CCfilterDecay:
      if (lowerSW) {
        filterDecayL = value;
      }
      if (upperSW) {
        filterDecayU = value;
      }
      filterDecaystr = QUADRA100[value];
      updatefilterDecay();
      break;

    case CCfilterSustain:
      if (lowerSW) {
        filterSustainL = value;
      }
      if (upperSW) {
        filterSustainU = value;
      }
      filterSustainstr = QUADRA100[value];
      updatefilterSustain();
      break;

    case CCfilterRelease:
      if (lowerSW) {
        filterReleaseL = value;
      }
      if (upperSW) {
        filterReleaseU = value;
      }
      filterReleasestr = QUADRA100[value];
      updatefilterRelease();
      break;

    case CCechoEQ:
      if (lowerSW) {
        echoEQL = value;
      }
      if (upperSW) {
        echoEQU = value;
      }
      echoEQstr = QUADRA100[value];
      updateechoEQ();
      break;

    case CCechoLevel:
      if (lowerSW) {
        echoLevelL = value;
      }
      if (upperSW) {
        echoLevelU = value;
      }
      echoLevelstr = QUADRA100[value];
      updateechoLevel();
      break;

    case CCechoFeedback:
      if (lowerSW) {
        echoFeedbackL = value;
      }
      if (upperSW) {
        echoFeedbackU = value;
      }
      echoFeedbackstr = QUADRA100[value];
      updateechoFeedback();
      break;

    case CCechoSpread:
      if (lowerSW) {
        echoSpreadL = value;
      }
      if (upperSW) {
        echoSpreadU = value;
      }
      echoSpreadstr = QUADRA100[value];
      updateechoSpread();
      break;

    case CCechoTime:
      if (lowerSW) {
        echoTimeL = value;
      }
      if (upperSW) {
        echoTimeU = value;
      }
      echoTimestr = QUADRA100[value];
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
      unisonDetunestr = QUADRA100[value];
      updateunisonDetune();
      break;

    case CCglideSpeed:
      if (lowerSW) {
        glideSpeedL = value;
      }
      if (upperSW) {
        glideSpeedU = value;
      }
      glideSpeedstr = QUADRA100[value];
      updateglideSpeed();
      break;

    case CCosc1Transpose:
      if (lowerSW) {
        osc1TransposeL = value;
      }
      if (upperSW) {
        osc1TransposeU = value;
      }
      osc1Transposestr = QUADRA100[value];
      updateosc1Transpose();
      break;

    case CCosc2Transpose:
      if (lowerSW) {
        osc2TransposeL = value;
      }
      if (upperSW) {
        osc2TransposeU = value;
      }
      osc2Transposestr = QUADRA100[value];
      updateosc2Transpose();
      break;

    case CCnoiseLevel:
      if (lowerSW) {
        noiseLevelL = value;
      }
      if (upperSW) {
        noiseLevelU = value;
      }
      noiseLevelstr = QUADRA100[value];
      updatenoiseLevel();
      break;

    case CCglideAmount:
      if (lowerSW) {
        glideAmountL = value;
      }
      if (upperSW) {
        glideAmountU = value;
      }
      glideAmountstr = QUADRA100[value];
      updateglideAmount();
      break;

    case CCosc1Tune:
      if (lowerSW) {
        osc1TuneL = value;
      }
      if (upperSW) {
        osc1TuneU = value;
      }
      osc1Tunestr = QUADRA100[value];
      updateosc1Tune();
      break;

    case CCosc2Tune:
      if (lowerSW) {
        osc2TuneL = value;
      }
      if (upperSW) {
        osc2TuneU = value;
      }
      osc2Tunestr = QUADRA100[value];
      updateosc2Tune();
      break;

    case CCbendToFilter:
      bendToFilter = value;
      bendToFilterstr = QUADRA100[value];
      updatebendToFilter();
      break;

    case CClfo2ToFilter:
      lfo2ToFilter = value;
      lfo2ToFilterstr = QUADRA100[value];
      updatelfo2ToFilter();
      break;

    case CCbendToOsc:
      bendToOsc = value;
      bendToOscstr = QUADRA100[value];
      updatebendToOsc();
      break;

    case CClfo2ToOsc:
      lfo2ToOsc = value;
      lfo2ToOscstr = QUADRA100[value];
      updatelfo2ToOsc();
      break;

    case CClfo2FreqAcc:
      lfo2FreqAcc = value;
      lfo2FreqAccstr = QUADRA100[value];
      updatelfo2FreqAcc();
      break;

    case CClfo2InitFrequency:
      lfo2InitFrequency = value;
      lfo2InitFrequencystr = QUADRA100[value];
      updatelfo2InitFrequency();
      break;

    case CClfo2InitAmount:
      lfo2InitAmount = value;
      lfo2InitAmountstr = QUADRA100[value];
      updatelfo2InitAmount();
      break;

    case CCseqAssign:
      seqAssign = value;
      seqAssignstr = map(seqAssign, 0, 127, 0, 1);
      updateseqAssign();
      break;

    case CCseqRate:
      seqRate = value;
      seqRatestr = QUADRA100[value];
      updateseqRate();
      break;

    case CCseqGate:
      seqGate = value;
      seqGatestr = QUADRA100[value];
      updateseqGate();
      break;

    case CClfo1Frequency:
      if (lowerSW) {
        lfo1FrequencyL = value;
      }
      if (upperSW) {
        lfo1FrequencyU = value;
      }
      lfo1Frequencystr = QUADRA100[value];
      updatelfo1Frequency();
      break;

    case CClfo1DepthA:
      if (lowerSW) {
        lfo1DepthAL = value;
      }
      if (upperSW) {
        lfo1DepthAU = value;
      }
      lfo1DepthAstr = QUADRA100[value];
      updatelfo1DepthA();
      break;

    case CClfo1Delay:
      if (lowerSW) {
        lfo1DelayL = value;
      }
      if (upperSW) {
        lfo1DelayU = value;
      }
      lfo1Delaystr = QUADRA100[value];
      updatelfo1Delay();
      break;

    case CClfo1DepthB:
      if (lowerSW) {
        lfo1DepthBL = value;
      }
      if (upperSW) {
        lfo1DepthBU = value;
      }
      lfo1DepthBstr = QUADRA100[value];
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

    case CClowerSW:
      value > 0 ? lowerSW = 1 : lowerSW = 0;
      updateLowerSW();
      break;

    case CCupperSW:
      value > 0 ? upperSW = 1 : upperSW = 0;
      updateUpperSW();
      break;

    case CCutilitySW:
      value > 0 ? upperSW = 1 : upperSW = 0;
      updateUtilitySW();
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
  recallPatchFlag = false;
}

void setCurrentPatchData(String data[]) {
  patchName = data[0];

  // Pots

  masterVolume = data[1].toInt();
  masterTune = data[2].toInt();
  layerPan = data[3].toInt();
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

  //Pots

  updatemasterTune();
  updatemasterVolume();
  updatelayerPan();
  updatelayerVolume();

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
  updatesingleSW();
  updatedoubleSW();
  updatesplitSW();

  //Patchname
  updatePatchname();

  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

String getCurrentPatchData() {
  return patchName + "," + String(masterVolume) + "," + String(masterTune) + "," + String(layerPan) + "," + String(layerVolumeL) + "," + String(layerVolumeU) + "," + String(reverbLevelL)
         + "," + String(reverbLevelU) + "," + String(reverbDecayL) + "," + String(reverbDecayU) + "," + String(reverbEQL) + "," + String(reverbEQU) + "," + String(arpFrequencyL)
         + "," + String(arpFrequencyL) + "," + String(ampVelocityL) + "," + String(ampVelocityL) + "," + String(filterVelocityL) + "," + String(filterVelocityU) + "," + String(ampReleaseL)
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
         + "," + String(splitSW);
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

  if (btnIndex == UTILITY_SW && btnType == ROX_PRESSED) {
    utilitySW = 1;
    myControlChange(midiChannel, CCutilitySW, utilitySW);
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

  if (btnIndex == SINGLE_SW && btnType == ROX_PRESSED) {
    singleSW = !singleSW;
    myControlChange(midiChannel, CCsingleSW, singleSW);
  }

  if (btnIndex == DOUBLE_SW && btnType == ROX_PRESSED) {
    doubleSW = !doubleSW;
    myControlChange(midiChannel, CCdoubleSW, doubleSW);
  }

  if (btnIndex == SPLIT_SW && btnType == ROX_PRESSED) {
    splitSW = !splitSW;
    myControlChange(midiChannel, CCsplitSW, splitSW);
  }

  // if (btnIndex == LEAD_VCO1_WAVE_SW && btnType == ROX_PRESSED) {
  //   sr.writePin(LEAD_VCO1_WAVE_LED, LOW);
  //   vco1wave_timer = millis();
  //   leadVCO1wave = leadVCO1wave + 1;
  //   if (leadVCO1wave > 5) {
  //     leadVCO1wave = 1;
  //   }
  //   myControlChange(midiChannel, CCleadVCO1wave, leadVCO1wave);
  // }

  // if (btnIndex == LEAD_VCO2_WAVE_SW && btnType == ROX_PRESSED) {
  //   sr.writePin(LEAD_VCO2_WAVE_LED, LOW);
  //   vco2wave_timer = millis();
  //   leadVCO2wave = leadVCO2wave + 1;
  //   if (leadVCO2wave > 6) {
  //     leadVCO2wave = 1;
  //   }
  //   myControlChange(midiChannel, CCleadVCO2wave, leadVCO2wave);
  // }

  // if (btnIndex == POLY_WAVE_SW && btnType == ROX_PRESSED) {
  //   sr.writePin(POLY_WAVE_LED, LOW);
  //   polywave_timer = millis();
  //   polyWave = polyWave + 1;
  //   if (polyWave > 6) {
  //     polyWave = 1;
  //   }
  //   myControlChange(midiChannel, CCpolyWave, polyWave);
  // }

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
              // case CCpolyLearn:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(120, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(120, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(120, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(120, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCtrillUp:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(116, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(116, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(116, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(116, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCtrillDown:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(117, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(117, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(117, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(117, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCleadLearn:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(121, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(121, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(121, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(121, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCbassLearn:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(119, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(119, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(119, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(119, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCstringsLearn:  // strings learn
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(118, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(118, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(118, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(118, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCphaserLeadSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(0, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(0, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(0, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(0, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCchorusBassSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(1, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(1, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(1, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(1, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCchorusStringsSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(2, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(2, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(2, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(2, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCchorusPolySW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(3, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(3, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(3, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(3, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCchorusLeadSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(4, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(4, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(4, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(4, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCechoBassSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(5, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(5, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(5, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(5, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCechoStringsSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(6, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(6, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(6, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(6, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCechoPolySW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(7, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(7, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(7, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(7, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCechoLeadSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(8, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(8, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(8, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(8, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCreverbBassSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(9, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(9, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(9, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(9, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCreverbStringsSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(10, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(10, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(10, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(10, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCreverbPolySW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(11, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(11, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(11, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(11, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCreverbLeadSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(12, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(12, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(12, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(12, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCarpOnSW:
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(127, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(127, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(127, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(127, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

              // case CCarpDownSW:
              //   // Arp Down
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(126, 127, midiOutCh);  //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(126, 127, midiOutCh);  //MIDI DIN is set to Out
              //   break;

              // case CCarpUpSW:
              //   // Arp Up
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(125, 127, midiOutCh);  //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(125, 127, midiOutCh);  //MIDI DIN is set to Out
              //   break;

              // case CCarpUpDownSW:
              //   // Arp UpDown
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(124, 127, midiOutCh);  //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(124, 127, midiOutCh);  //MIDI DIN is set to Out
              //   break;

              // case CCarpRandomSW:
              //   // Arp Random
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(123, 127, midiOutCh);  //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(123, 127, midiOutCh);  //MIDI DIN is set to Out
              //   break;

              // case CCarpHoldSW:
              //   // Arp Hold
              //   if (updateParams) {
              //     usbMIDI.sendNoteOn(122, 127, midiOutCh);  //MIDI USB is set to Out
              //     usbMIDI.sendNoteOff(122, 0, midiOutCh);   //MIDI USB is set to Out
              //   }
              //   MIDI.sendNoteOn(122, 127, midiOutCh);  //MIDI DIN is set to Out
              //   MIDI.sendNoteOff(122, 0, midiOutCh);   //MIDI USB is set to Out
              //   break;

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
          // usbMIDI.sendControlChange(99, 0, midiOutCh);      //MIDI DIN is set to Out
          // usbMIDI.sendControlChange(98, cc, midiOutCh);     //MIDI DIN is set to Out
          // usbMIDI.sendControlChange(38, value, midiOutCh);  //MIDI DIN is set to Out
          // usbMIDI.sendControlChange(6, 0, midiOutCh);       //MIDI DIN is set to Out

          // midi1.sendControlChange(99, 0, midiOutCh);      //MIDI DIN is set to Out
          // midi1.sendControlChange(98, cc, midiOutCh);     //MIDI DIN is set to Out
          // midi1.sendControlChange(38, value, midiOutCh);  //MIDI DIN is set to Out
          // midi1.sendControlChange(6, 0, midiOutCh);       //MIDI DIN is set to Out

          // MIDI.sendControlChange(99, 0, midiOutCh);      //MIDI DIN is set to Out
          // MIDI.sendControlChange(98, cc, midiOutCh);     //MIDI DIN is set to Out
          // MIDI.sendControlChange(38, value, midiOutCh);  //MIDI DIN is set to Out
          // MIDI.sendControlChange(6, 0, midiOutCh);       //MIDI DIN is set to Out
          break;
        }
      case 2:
        {
          break;
        }
    }
  }
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
  // if ((polywave_timer > 0) && (millis() - polywave_timer > 150)) {
  //   sr.writePin(POLY_WAVE_LED, HIGH);
  //   polywave_timer = 0;
  // }

  // if ((vco1wave_timer > 0) && (millis() - vco1wave_timer > 150)) {
  //   sr.writePin(LEAD_VCO1_WAVE_LED, HIGH);
  //   vco1wave_timer = 0;
  // }

  // if ((vco2wave_timer > 0) && (millis() - vco2wave_timer > 150)) {
  //   sr.writePin(LEAD_VCO2_WAVE_LED, HIGH);
  //   vco2wave_timer = 0;
  // }
}

void flashLearnLED(int displayNumber) {

  if (learning) {
    if ((learn_timer > 0) && (millis() - learn_timer >= 1000)) {
      switch (displayNumber) {
        case 0:
          setLEDDisplay0();
          display0.setBacklight(LEDintensity);
          break;

        case 1:
          setLEDDisplay1();
          display1.setBacklight(LEDintensity);
          break;

        case 2:
          setLEDDisplay2();
          display2.setBacklight(LEDintensity);
          break;
      }

      learn_timer = millis();
    } else if ((learn_timer > 0) && (millis() - learn_timer >= 500)) {
      switch (displayNumber) {
        case 0:
          setLEDDisplay0();
          display0.setBacklight(0);
          break;

        case 1:
          setLEDDisplay1();
          display1.setBacklight(0);
          break;

        case 2:
          setLEDDisplay2();
          display2.setBacklight(0);
          break;
      }
    }
  }
}

void loop() {
  checkMux();           // Read the sliders and switches
  checkSwitches();      // Read the buttons for the program menus etc
  checkEncoder();       // check the encoder status
  octoswitch.update();  // read all the buttons for the Synthex
  sr.update();          // update all the RED LEDs in the buttons
  green.update();       // update all the GREEN LEDs in the buttons
  // Read all the MIDI ports
  myusb.Task();
  midi1.read();  //USB HOST MIDI Class Compliant
  MIDI.read(midiChannel);
  usbMIDI.read(midiChannel);

  checkEEPROM();                         // check anything that may have changed form the initial startup
  stopLEDs();                            // blink the wave LEDs once when pressed
  flashLearnLED(learningDisplayNumber);  // Flash the corresponding learn LED display when button pressed
  convertIncomingNote();                 // read a note when in learn mode and use it to set the values
}
