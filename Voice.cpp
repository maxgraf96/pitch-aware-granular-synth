/***** Voice.cpp *****/
#include "Voice.h"

Voice::Voice(float sampleRate){
	this->sampleRate = sampleRate;
	cfg = ne10_fft_alloc_c2c_float32_neon (N_FFT);
	
	// Initialise frequency representation grain buffer
	// This will be used to mask the current buffer coming from the main loop
	currentMask = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof(ne10_fft_cpx_float32_t));
	
	// Initialise time representation grain buffer
	timeDomainGrainBuffer = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof(ne10_fft_cpx_float32_t));
}

void Voice::noteOn(std::array<ne10_fft_cpx_float32_t*, 10>& grainSrcBuffer, float frequency){
	this->frequency = frequency;
	this->bufferPosition = 0;
	
	// Extract frequencies from grain source buffer
	int binF0 = int(map(frequency, 0, float(sampleRate) / 2.0f, 0.0, float(N_FFT) / 2.0f));
	
	// Define overtone bins
	std::set<int> overtones;
	int nOvertones = 10;
	for (int i = 0; i < nOvertones; i++){
		overtones.insert(binF0 * (i + 1));
	}

	// Create a mask for the frequency domain representation based on the current fundamental frequency
	for (int hop = 0; hop < 10; hop++){
		for (int k = 0; k < N_FFT; k++){
			// Check if current bin should be included
			auto search = overtones.find(k);
			if (search == overtones.end()) {
				// Current k is not in bins -> exclude
    			currentMask[k].r = 0.0f;
				currentMask[k].i = 0.0f;
			} else {
				// Boost amplitude
				currentMask[k].r = grainSrcBuffer[hop][k].r * 2;
				currentMask[k].i = grainSrcBuffer[hop][k].i;
			}
		}
		
		// Run the inverse FFT -> indicated by the "1" for the last function parameter 
		ne10_fft_c2c_1d_float32_neon (timeDomainGrainBuffer, currentMask, cfg, 1);
		
		// Copy current timeDomainGrainBuffer into final time-domain grain buffer
		// using overlap-and-add
		for (int i = 0; i < N_FFT; i++){
			buffer[bufferPosition++] = timeDomainGrainBuffer[i].r;
			if(bufferPosition >= 10 * FFT_HOP_SIZE)
				bufferPosition = 0;
		}
	}
	
	// Fill grain buffers
	for (int i = 0; i < 20; i++){
		int mainBufferStartIdx = i * grainLength;
		for(int sampleIdx = 0; sampleIdx < grainLength; sampleIdx++){
			grainBuffers[i][sampleIdx] = buffer[mainBufferStartIdx + sampleIdx];
		}
	}
}

void Voice::noteOff(){
	this->frequency = NOT_PLAYING;
	this->bufferPosition = int(NOT_PLAYING);
	
	// Reset grain positions
	for (auto& pos : grainPositions){
		pos = 0;
	}
}

float Voice::play(){
	float mix = 0.0f;
	
	// Test: Get first grain, add to mix and increment
	mix += grainBuffers[3][grainPositions[3]];
	grainPositions[3]++;
	if(grainPositions[3] > grainLength)
		grainPositions[3] = 0;
		
	return mix;
}

Voice::~Voice(){
	//NE10_FREE(timeDomainGrainBuffer);
	//NE10_FREE(currentMask);
}