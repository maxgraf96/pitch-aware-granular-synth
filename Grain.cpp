/***** Grain.cpp *****/
#include <cmath>
#include "Grain.h"

Grain::Grain() {
}

Grain::Grain(int length) {
	this->length = length;
	
	// Calculate the Hann window
	for(int i = 0; i < length; i++) {
		window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (float)(length - 1)));
	}
}

Grain::~Grain(){
}