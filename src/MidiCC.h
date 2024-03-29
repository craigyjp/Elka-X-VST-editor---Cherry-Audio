//MIDI CC control numbers
//These broadly follow standard CC assignments

#define MIDImasterTune 2
#define MIDIlayerPanU 3
#define MIDIlayerPanL 4
#define MIDIlayerVolumeU 5
#define MIDIlayerVolumeL 6
#define MIDImasterVolume 7
#define MIDIreverbLevelU 8
#define MIDIreverbLevelL 9
#define MIDIreverbDecayU 10
#define MIDIpolyMonoU 11
#define MIDIreverbDecayL 12
#define MIDIreverbEQU 13
#define MIDIreverbEQL 14
#define MIDIarpFrequencyU 15
#define MIDIarpFrequencyL 16
#define MIDIampVelocityU 17
#define MIDIampVelocityL 18
#define MIDIfilterVelocityU 19
#define MIDIfilterVelocityL 20
#define MIDIampReleaseU 21
#define MIDIampReleaseL 22
#define MIDIampSustainU 23
#define MIDIampSustainL 24
#define MIDIampDecayU 25
#define MIDIampDecayL 26
#define MIDIampAttackU 27
#define MIDIampAttackL 28
#define MIDIfilterKeyboardU 29
#define MIDIfilterKeyboardL 30
#define MIDIfilterResonanceU 31
#define MIDIfilterResonanceL 33
#define MIDIosc2VolumeU 34
#define MIDIosc2VolumeL 35
#define MIDIosc2PWU 36
#define MIDIosc2PWL 37
#define MIDIosc1PWU 38
#define MIDIosc1PWL 39
#define MIDIosc1VolumeU 40
#define MIDIosc1VolumeL 41
#define MIDIfilterCutoffU 42
#define MIDIfilterCutoffL 43
#define MIDIfilterEnvAmountU 44
#define MIDIfilterEnvAmountL 45
#define MIDIfilterAttackU 46
#define MIDIfilterAttackL 47
#define MIDIfilterDecayU 48
#define MIDIfilterDecayL 49
#define MIDIfilterSustainU 50
#define MIDIfilterSustainL 51
#define MIDIfilterReleaseU 52
#define MIDIfilterReleaseL 53
#define MIDIechoEQU 54
#define MIDIechoEQL 55
#define MIDIechoLevelU 56
#define MIDIechoLevelL 57
#define MIDIechoFeedbackU 58
#define MIDIechoFeedbackL 59
#define MIDIechoSpreadU 60
#define MIDIechoSpreadL 61
#define MIDIechoTimeU 62
#define MIDIechoTimeL 63

#define MIDIlfo2UpperLower 65
#define MIDIlfo2SyncSW 66
#define MIDIunisonDetuneU 67
#define MIDIunisonDetuneL 68
#define MIDIglideSpeedU 69
#define MIDIglideSpeedL 70
#define MIDIosc1TransposeU 71
#define MIDIosc1TransposeL 72
#define MIDIosc2TransposeU 73
#define MIDIosc2TransposeL 74
#define MIDInoiseLevelU 75
#define MIDInoiseLevelL 76
#define MIDIglideAmountU 77
#define MIDIglideAmountL 78
#define MIDIosc1TuneU 79
#define MIDIosc1TuneL 80
#define MIDIosc2TuneU 81
#define MIDIosc2TuneL 82
#define MIDIbendToFilter 83
#define MIDIlfo2ToFilter 84
#define MIDIbendToOsc 85
#define MIDIlfo2ToOsc 86
#define MIDIlfo2FreqAcc 87
#define MIDIlfo2InitFrequency 88
#define MIDIlfo2InitAmount 89
#define MIDIlfo1FrequencyU 90
#define MIDIlfo1FrequencyL 91
#define MIDIlfo1DepthAU 92
#define MIDIlfo1DepthAL 93
#define MIDIlfo1DelayU 94
#define MIDIlfo1DelayL 95
#define MIDIlfo1DepthBU 96
#define MIDIlfo1DepthBL 97
#define MIDIosc1FootU 98
#define MIDIosc1FootL 99
#define MIDIosc2FootU 100
#define MIDIosc2FootL 101
#define MIDIosc1WaveU 102
#define MIDIosc1WaveL 103
#define MIDIosc2WaveU 104
#define MIDIosc2WaveL 105
#define MIDIkeyboard 106 // sets the keyboard mode single/double/split
#define MIDIpanel 107    // sets the upper or lower sections, single will recall upper
#define MIDIlfo1WaveU 108
#define MIDIlfo1WaveL 109
#define MIDIfilterU 110
#define MIDIfilterL 111
#define MIDIechoU 112
#define MIDIechoL 113
#define MIDIreverbU 114
#define MIDIreverbL 115
#define MIDIchorusU 116
#define MIDIchorusL 117
#define MIDIarpRangeU 118
#define MIDIarpRangeL 119
#define MIDIarpDirectionU 120
#define MIDIpolyMonoL 121
#define MIDIarpDirectionL 122
#define CCallnotesoff 123  //Panic button

#define MIDIglideU 124
#define MIDIglideL 125
#define MIDInoiseU 126
#define MIDInoiseL 127


// Above 128 will be handled as Note On events

#define MIDIarpSyncU 128  // note 0
#define MIDIarpSyncL 129  // note 1
#define MIDIarpHoldU 130  // note 2
#define MIDIarpHoldL 131  // note 3
#define MIDIenvInvU 132   // note 4
#define MIDIenvInvL 133   // note 5
#define MIDIosc2SyncU 134
#define MIDIosc2SyncL 135
#define MIDImultiTriggerU 136
#define MIDImultiTriggerL 137
#define MIDIechoSyncU 138
#define MIDIechoSyncL 139
#define MIDIosc1ringU 140
#define MIDIosc1ringL 141
#define MIDIosc2ringU 142
#define MIDIosc2ringL 143
#define MIDIosc1glideU 144
#define MIDIosc1glideL 145
#define MIDIosc2glideU 146
#define MIDIosc2glideL 147
#define MIDIlimiter 148
#define MIDIlayerSolo 149
#define MIDIlfo1SyncU 150
#define MIDIlfo1SyncL 151
#define MIDIlfo1modWheelU 152
#define MIDIlfo1modWheelL 153
#define MIDIlfo1resetU 154
#define MIDIlfo1resetL 155
#define MIDIlfo1osc1U 156
#define MIDIlfo1osc1L 157
#define MIDIlfo1osc2U 158
#define MIDIlfo1osc2L 159
#define MIDIlfo1pw1U 160
#define MIDIlfo1pw1L 161
#define MIDIlfo1pw2U 162
#define MIDIlfo1pw2L 163
#define MIDIlfo1filtU 164
#define MIDIlfo1filtL 165
#define MIDIlfo1ampU 166
#define MIDIlfo1ampL 167
#define MIDIlfo1seqRateU 168
#define MIDIlfo1seqRateL 169

#define MIDIchordMemoryL 171
#define MIDIchordMemoryU 172
#define MIDIsingleSW 173
#define MIDIdoubleSW 174
#define MIDIsplitSW 175
#define MIDIseqPlaySW 1     // A
#define MIDIseqStopSW 2     // B
#define MIDIseqKeySW 3      // C
#define MIDIseqTransSW 4    // D
#define MIDIseqLoopSW 5     // E
#define MIDIseqFwSW 6       // F
#define MIDIseqBwSW 7      // G
#define MIDIseqEnable1SW 8 // H
#define MIDIseqEnable2SW 9 // I
#define MIDIseqEnable3SW 10 // J
#define MIDIseqEnable4SW 11 // K
#define MIDIseqSyncSW 12    // L
#define MIDIseqrecEditSW 13 // M
#define MIDIseqinsStepSW 14 // N
#define MIDIseqdelStepSW 15 // O
#define MIDIseqaddStepSW 16 // P
#define MIDIseqRestSW 17    // Q
#define MIDIseqUtilSW 18    // R
#define MIDImaxVoicesSW 19  // S
#define MIDIEnter 100
#define MIDIEscape 101
#define MIDIUpArrow 102
#define MIDIDownArrow 103
