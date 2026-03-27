#pragma once

#include <cinttypes>
#include "RNBO.h"

//always mapping to/from normalized values
namespace midimap {
		inline constexpr uint8_t NOTE_OFF = 0x80;
		inline constexpr uint8_t NOTE_ON = 0x90;
		inline constexpr uint8_t KEY_PRESSURE = 0xA0;
		inline constexpr uint8_t CONTROL_CHANGE = 0xB0;
		inline constexpr uint8_t PITCH_BEND_CHANGE = 0xE0;
		inline constexpr uint8_t SONG_POSITION_POINTER = 0xF2;
		inline constexpr uint8_t PROGRAM_CHANGE = 0xC0;
		inline constexpr uint8_t CHANNEL_PRESSURE = 0xD0;
		inline constexpr uint8_t QUARTER_FRAME = 0xF1;
		inline constexpr uint8_t SONG_SELECT = 0xF3;
		inline constexpr uint8_t TUNE_REQUEST = 0xF6;
		inline constexpr uint8_t TIMING_CLOCK = 0xF8;
		inline constexpr uint8_t START = 0xFA;
		inline constexpr uint8_t CONTINUE = 0xFB;
		inline constexpr uint8_t STOP = 0xFC;
		inline constexpr uint8_t ACTIVE_SENSING = 0xFE;
		inline constexpr uint8_t RESET = 0xFF;
		inline constexpr uint8_t SYSEX_START = 0xF0;
		inline constexpr uint8_t SYSEX_END = 0xF7;

		double value(uint8_t status, uint8_t data0, uint8_t data1, bool normalize = true);
		//0 means we don't map this type
		uint16_t key(uint8_t status, uint8_t data0 = 0);

		//0 means we failed to map this type
		uint16_t key(const RNBO::Json& json);
		RNBO::Json json(uint16_t key);
};

