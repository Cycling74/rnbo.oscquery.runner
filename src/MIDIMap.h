#pragma once

#include <cinttypes>
#include "RNBO.h"

//always mapping to/from normalized values
namespace midimap {
		double value(uint8_t status, uint8_t data0, uint8_t data1);
		//0 means we don't map this type
		uint16_t key(uint8_t status, uint8_t data0 = 0);

		//0 means we failed to map this type
		uint16_t key(const RNBO::Json& json);
		RNBO::Json json(uint16_t key);
};

