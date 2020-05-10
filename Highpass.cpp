/***** Highpass.cpp *****/
#include "Highpass.h"

Highpass::Highpass(float sampleRate){
	this->sampleRate = sampleRate;
	// Set sampling period and sampling period squared for convenience
	T = 1 / sampleRate;
	TSquared = pow(T, 2);
}

float Highpass::processSample(float sampleInput){
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

void Highpass::calculate_coefficients(float frequency, float q)
{
	// Adaption of the JUCE second order HP implementation, see https://docs.juce.com/master/classIIRFilter.html
	float n = std::tan (M_PI * frequency / sampleRate);
    float nSquared = n * n;
    float invQ = 1.0f / q;
    float c1 = 1.0f / (1.0f + invQ * n + nSquared);

	b0 = c1;
	b1 = c1 * -2.0f;
	b2 = c1;
	float a0 = 1.0f;
	a1 = c1 * 2.0f * (nSquared - 1.0f);
	a2 = c1 * (1 - invQ * n + nSquared);
	
    divisor = a0;

    b0 = b0 / divisor;
	b1 = b1 / divisor;
    b2 = b2 / divisor;
    a1 = a1 / divisor;
    a2 = a2 / divisor;
}
