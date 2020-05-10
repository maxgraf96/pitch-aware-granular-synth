/***** Lowpass.cpp *****/
#include "Lowpass.h"

Lowpass::Lowpass(float sampleRate){
	// Set sampling period and sampling period squared for convenience
	T = 1 / sampleRate;
	TSquared = pow(T, 2);
}

float Lowpass::processSample(float sampleInput){
    float in = sampleInput;
    
	// Calculate output of filter one
	float out = (
		in * b0
		+ lastIn * b1
		+ lastLastIn * b2
		- lastOut * a1
		- lastLastOut * a2);
	
	// Update past inputs and outputs
	lastLastIn = lastIn;
	lastIn = in;
	lastLastOut = lastOut;
	lastOut = out;
	
	// The same procedure for the second filter
	float out2 = (
	out * b0
	+ lastIn2 * b1
	+ lastLastIn2 * b2
	- lastOut2 * a1
	- lastLastOut2 * a2);
	
	// Update past inputs and outputs
	lastLastIn2 = lastIn2;
	lastIn2 = out;
	lastLastOut2 = lastOut2;
	lastOut2 = out2;
	
	return out2;
}

void Lowpass::calculate_coefficients(float frequency, float q)
{
	// Denormalise filter frequency
	frequency *= 2 * M_PI;
	
	// Convenience placeholder for the cutoff frequency squared
	float freqSquared = pow(frequency, 2);
	
	// Set the normalisation factor for the filter coefficients
	divisor = 4 * q + 2 * frequency * T + freqSquared * q * TSquared;
	
	// Set the coefficients
	b0 = (freqSquared * q * TSquared) / divisor;
	b1 = 2 * b0;
	b2 = b0;
	a1 = (2 * freqSquared * q * TSquared - 8 * q) / divisor;
	a2 = (4 * q + freqSquared * q * TSquared - 2 * frequency * T) / divisor;
}
