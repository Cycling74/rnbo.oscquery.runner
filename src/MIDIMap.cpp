#include "MIDIMap.h"

namespace {
	static constexpr uint8_t NOTE_OFF = 0x80;
	static constexpr uint8_t NOTE_ON = 0x90;
	static constexpr uint8_t KEY_PRESSURE = 0xA0;
	static constexpr uint8_t CONTROL_CHANGE = 0xB0;
	static constexpr uint8_t PITCH_BEND_CHANGE = 0xE0;
	static constexpr uint8_t SONG_POSITION_POINTER = 0xF2;
	static constexpr uint8_t PROGRAM_CHANGE = 0xC0;
	static constexpr uint8_t CHANNEL_PRESSURE = 0xD0;
	static constexpr uint8_t QUARTER_FRAME = 0xF1;
	static constexpr uint8_t SONG_SELECT = 0xF3;
	static constexpr uint8_t TUNE_REQUEST = 0xF6;
	static constexpr uint8_t TIMING_CLOCK = 0xF8;
	static constexpr uint8_t START = 0xFA;
	static constexpr uint8_t CONTINUE = 0xFB;
	static constexpr uint8_t STOP = 0xFC;
	static constexpr uint8_t ACTIVE_SENSING = 0xFE;
	static constexpr uint8_t RESET = 0xFF;
	static constexpr uint8_t SYSEX_START = 0xF0;
	static constexpr uint8_t SYSEX_END = 0xF7;
}

namespace midimap {
	double value(uint8_t status, uint8_t data0, uint8_t data1) {
		switch (status & 0xF0) {
			//note off maps to note on key
			case NOTE_OFF:
				return 0.0;
			case NOTE_ON:
				return data1 == 0 ? 0.0 : 1.0;
				break;
			case KEY_PRESSURE:
			case CONTROL_CHANGE:
				return static_cast<double>(data1) / 127.0;
				break;
			case PITCH_BEND_CHANGE:
				return static_cast<double>((static_cast<uint16_t>(data1) << 7) + static_cast<uint16_t>(data0)) / 16383.0;
			case PROGRAM_CHANGE:
			case CHANNEL_PRESSURE:
				return static_cast<double>(data0) / 127.0;

			case 0xF0:
				switch (status) {
					case SONG_POSITION_POINTER:
						//14-bit unsigned
						return static_cast<double>((static_cast<uint16_t>(data1) << 7) + static_cast<uint16_t>(data0)) / 16383.0;
					case QUARTER_FRAME:
					case SONG_SELECT:
						//one byte
						return static_cast<double>(data0) / 127.0;
					case TUNE_REQUEST:
					case START:
					case CONTINUE:
					case STOP:
					case ACTIVE_SENSING:
					case RESET:
						return 1.0;
				}
				break;
		}
		return 0.0;
	}

	uint16_t key(uint8_t status, uint8_t data0) {
		switch (status & 0xF0) {
			//note off maps to note on key
			case NOTE_OFF:
			case NOTE_ON:
				status = NOTE_ON | (status & 0x0F);
				break;
			case KEY_PRESSURE:
			case CONTROL_CHANGE:
				//no change
				break;
			case PITCH_BEND_CHANGE:
			case PROGRAM_CHANGE:
			case CHANNEL_PRESSURE:
				data0 = 0; //2 byte or 14-bit, mask off data0
				return 2;

			case 0xF0:
				//unsupported
				switch (status) {
					case TIMING_CLOCK:
					case SYSEX_START:
					case SYSEX_END:
						return 0;
				}

				data0 = 0;//mask off data0
				break;
		}

		return (static_cast<uint16_t>(status) << 8) | static_cast<uint16_t>(data0);
	}

	/*
		 uint16_t ParamMIDIMap::len(uint8_t status) {
		 switch (status & 0xF0) {
		 case NOTE_OFF:
		 case NOTE_ON:
		 case KEY_PRESSURE:
		 case CONTROL_CHANGE:
		 case PITCH_BEND_CHANGE:
		 return 3;

		 case PROGRAM_CHANGE:
		 case CHANNEL_PRESSURE:
		 return 2;

		 case 0xF0:
		 switch (status) {
		 case SONG_POSITION_POINTER:
		 return 3;
		 case QUARTER_FRAME:
		 case SONG_SELECT:
		 return 2;
		 case TUNE_REQUEST:
	//case TIMING_CLOCK:
	case START:
	case CONTINUE:
	case STOP:
	case ACTIVE_SENSING:
	case RESET:
	return 1;
	}
	break;
	}
	return 0;
	}
	*/
}
