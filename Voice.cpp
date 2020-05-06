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
	
	// Initialise grain buffers
	// Vectors are used to allow for dynamic adjustment of the number of grains that
	// can be synthesised
	grainPositions = {}; // Strangely the program crashes if this definition " = {}" is in the header file?
	for (int i = 0; i < numberOfGrains; i++){
		grainBuffers.push_back(std::array<float, int(GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE / 20)>());
		
		// Initialise grain position as well
		grainPositions.push_back(NOT_PLAYING_I);
	}
	
	// Initialise hann window used to window grains
	// Allocate the window buffer based on the FFT size
	for(int n = 0; n < grainLength; n++) {
		hannWindow[n] = 0.5f * (1.0f - cosf(2.0f * M_PI * n / (float)(grainLength - 1)));
	}
	
	// Initialise random generator
	srand (time(NULL));
}

void Voice::noteOn(std::array<ne10_fft_cpx_float32_t*, GRAIN_SRC_BUFFER_LENGTH>& grainSrcBuffer, float frequency){
	this->frequency = frequency;
	this->bufferPosition = 0;
	
	// Clear buffer
	for (int i = 0; i < GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE; i++){
		buffer[i] = 0.0f;
	}
	
	// Extract frequencies from grain source buffer
	int binF0 = int(map(frequency, 0.0f, float(sampleRate) / 2.0f, 0.0f, float(N_FFT)));
	// Define overtone bins
	std::set<int> overtones;
	int nOvertones = 10;
	for (int i = 0; i < nOvertones; i++){
		overtones.insert(binF0 * (i + 1));
	}

	// Create a mask for the frequency domain representation based on the current fundamental frequency
	for (int hop = 0; hop < GRAIN_SRC_BUFFER_LENGTH; hop++){
		for (int k = 0; k < N_FFT; k++){
			// Check if current bin should be included
			auto search = overtones.find(k);
			if (search == overtones.end()) {
				// Current k is not in bins -> exclude
    			currentMask[k].r = 0.0f;
				currentMask[k].i = 0.0f;
			} else {
				// Include current bin in mask
				currentMask[k].r = grainSrcBuffer[hop][k].r;
				currentMask[k].i = grainSrcBuffer[hop][k].i;
			}
		}
		
		// Run the inverse FFT -> indicated by the "1" for the last function parameter 
		ne10_fft_c2c_1d_float32_neon (timeDomainGrainBuffer, currentMask, cfg, 1);
		
		// Copy current timeDomainGrainBuffer into final time-domain grain buffer
		// using overlap-and-add
		for (int i = 0; i < N_FFT; i++){
			if(bufferPosition + i + 1 >= GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE){
				break;
			}
			buffer[bufferPosition + i] += timeDomainGrainBuffer[i].r;
		}
		bufferPosition += FFT_HOP_SIZE;
		if (bufferPosition >= GRAIN_SRC_BUFFER_LENGTH * FFT_HOP_SIZE){
			break;
		}
	}
	
	// Fill grain buffers with slices of masked time domain signal
	for (int i = 0; i < numberOfGrains; i++){
		int mainBufferStartIdx = i * grainLength;
		for(int sampleIdx = 0; sampleIdx < grainLength; sampleIdx++){
			// Apply hann window
			auto windowed = buffer[mainBufferStartIdx + sampleIdx] * hannWindow[sampleIdx];
			grainBuffers[i][sampleIdx] = windowed;
		}
	}
	
	// Start playing numberOfGrainsPlayback grains
	for (int i = 0; i < numberOfGrainsPlayback; i++){
		grainPositions[i] = 0;
	}
}

float Voice::play(){
	float mix = 0.0f;
	
	// Iterate over the grains currently playing and add their
	// sample values to the mix
	for (int grainIdx = 0; grainIdx < numberOfGrains; grainIdx++){
		if(grainPositions[grainIdx] > NOT_PLAYING_I){
			// Add current sample to mix
			mix += grainBuffers[grainIdx][grainPositions[grainIdx]];
			// Update sample position for grain buffer
			grainPositions[grainIdx]++;
			
			// Reset sample position if over grain buffer length
			if(grainPositions[grainIdx] >= grainLength){
				bool loop = false;
				
				// Make trigger of new note pseudorandom
				// bool shouldTriggerNewGrain = getRandomBool();
				bool shouldTriggerNewGrain = true;
				if(shouldTriggerNewGrain){
					if(loop){
						// Loop mode
						grainPositions[grainIdx] = 0;
					} else {
						// Find new free grain and playyy
						int nextFreeGrainIdx = findNextFreeGrainIdx();
						// Safety check: If all grains are currently playing just stop the current one
						// Otherwise:
						if(nextFreeGrainIdx > NOT_PLAYING_I){
							grainPositions[nextFreeGrainIdx] = 0;
						}
					}
				}
				
				// Stop playback for this grain
				grainPositions[grainIdx] = NOT_PLAYING_I;
			}
		}
	}
	
	//mix = buffer[bufferPosition++];
	//if(bufferPosition >= sizeof(buffer) / sizeof(*buffer))
	//	bufferPosition = 0;
		
	return mix;
}

void Voice::noteOff(){
	this->frequency = NOT_PLAYING;

	// Reset grain positions
	for (auto& pos : grainPositions){
		pos = NOT_PLAYING_I;
	}
}

int Voice::findNextFreeGrainIdx(){
	for (int i = 0; i < numberOfGrains; i++){
		if(grainPositions[i] == NOT_PLAYING_I){
			return i;
		}
	}
	// If no grain is found return NOT_PLAYING_I
	return NOT_PLAYING_I;
}

int Voice::getRandomInRange(int upperLimit){
	return rand() % upperLimit + 1;
}

bool Voice::getRandomBool(){
	return rand() % 2 == 0;
}

Voice::~Voice(){
}