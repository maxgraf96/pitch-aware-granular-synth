#include <Bela.h>
#include <cmath>
#include <memory>
#include <libraries/ne10/NE10.h> // NEON FFT library
#include <libraries/Midi/Midi.h>

// Definition for global variables so as to avoid cluttering the render.cpp code
#include "Globals.h"
// Definition for system-wide constants
#include "Constants.h"
// #include "CircularBuffer.h"
#include "SampleData.h"

// Circular buffer to store past CB_LENGTH samples
// std::unique_ptr<CircularBuffer> circularBuffer;

int gAudioChannelNum;

// Buffers --------------------------------------------------
float gInputBuffer[CB_LENGTH];
int gInputBufferPointer = 0;
float gOutputBuffer[CB_LENGTH];
int gOutputBufferWritePointer = 0;
int gOutputBufferReadPointer = 0;
int gSampleCount = 0;

float *gWindowBuffer;

// FFT params -----------------------------------------------
int gFFTSize = 2048;
int gHopSize = 512;
float gFFTScaleFactor = 0;

// Neon params
ne10_fft_cpx_float32_t* timeDomainIn;
ne10_fft_cpx_float32_t* timeDomainOut;
ne10_fft_cpx_float32_t* frequencyDomain;
ne10_fft_cfg_float32_t cfg;

// Sample info
// Data structure for complex data
SampleData gSampleData;
// Position of last read sample from audio file
int gReadPtr = 0;

// Auxiliary task for calculating FFT
AuxiliaryTask gFFTTask;
int gFFTInputBufferPointer = 0;
int gFFTOutputBufferPointer = 0;

// Necessary function definition for running an auxiliary task later
void process_fft_background(void*);

float *gInputAudio = NULL;

// MIDI object for receiving MIDI data
Midi midi;

// Current f0 of the MIDI note played
float gFrequency = 0.0f;

int gSampleRate = 0;

void midiCallback(MidiChannelMessage message, void* arg){
	if(message.getType() == kmmNoteOn){
		if(message.getDataByte(1) > 0){
			int note = message.getDataByte(0);
			float frequency = powf(2, (note-69)/12.f)*440;
			gFrequency = frequency;
			printf("\nnote: %d, frequency: %f \n", note, frequency);
		}
	}
	if(message.getType() == kmmNoteOff){
		gFrequency = 0.0f;
	}
}

bool setup(BelaContext *context, void *userData)
{
	// Initialise circular buffer
	// circularBuffer.reset(new CircularBuffer(CB_LENGTH));
	
	// If the amout of audio input and output channels is not the same
	// we will use the minimum between input and output
	gAudioChannelNum = std::min(context->audioInChannels, context->audioOutChannels);
	
	// Check that we have the same number of inputs and outputs.
	if(context->audioInChannels != context->audioOutChannels){
		printf("Different number of audio outputs and inputs available. Using %d channels.\n", gAudioChannelNum);
	}
	
	// Retrieve a parameter passed in from the initAudio() call
	gSampleData = *(SampleData *)userData;
	rt_printf("Sample data length: %4.2f seconds \n", (gSampleData.sampleLen / context->audioSampleRate));

	gFFTScaleFactor = 1.0f / (float)gFFTSize;
	gOutputBufferWritePointer += gHopSize;

	timeDomainIn = (ne10_fft_cpx_float32_t*) NE10_MALLOC (gFFTSize * sizeof (ne10_fft_cpx_float32_t));
	timeDomainOut = (ne10_fft_cpx_float32_t*) NE10_MALLOC (gFFTSize * sizeof (ne10_fft_cpx_float32_t));
	frequencyDomain = (ne10_fft_cpx_float32_t*) NE10_MALLOC (gFFTSize * sizeof (ne10_fft_cpx_float32_t));
	cfg = ne10_fft_alloc_c2c_float32_neon (gFFTSize);
	
	memset(timeDomainOut, 0, gFFTSize * sizeof (ne10_fft_cpx_float32_t));
	memset(gOutputBuffer, 0, CB_LENGTH * sizeof(float));

	// Allocate buffer to mirror and modify the input
	gInputAudio = (float *)malloc(context->audioFrames * gAudioChannelNum * sizeof(float));
	if(gInputAudio == 0)
		return false;

	// Allocate the window buffer based on the FFT size
	gWindowBuffer = (float *)malloc(gFFTSize * sizeof(float));
	if(gWindowBuffer == 0)
		return false;

	// Calculate a Hann window
	for(int n = 0; n < gFFTSize; n++) {
		gWindowBuffer[n] = 0.5f * (1.0f - cosf(2.0f * M_PI * n / (float)(gFFTSize - 1)));
	}

	// Initialise auxiliary tasks
	if((gFFTTask = Bela_createAuxiliaryTask(&process_fft_background, 90, "fft-calculation")) == 0)
		return false;
		
	// Setup MIDI
	midi.readFrom(0);
	midi.setParserCallback(midiCallback);
	
	// Expose audio sample rate
	gSampleRate = context->audioSampleRate;
	
	return true;
}

// This function handles the FFT processing in this example once the buffer has
// been assembled.
void process_fft(float *inBuffer, int inWritePointer, float *outBuffer, int outWritePointer)
{
	// Copy buffer into FFT input
	int pointer = (inWritePointer - gFFTSize + CB_LENGTH) % CB_LENGTH;
	for(int n = 0; n < gFFTSize; n++) {
		timeDomainIn[n].r = (ne10_float32_t) inBuffer[pointer] * gWindowBuffer[n];
		timeDomainIn[n].i = 0;

		pointer++;
		if(pointer >= CB_LENGTH)
			pointer = 0;
	}

	// Run the FFT -> indicated by the "0" for the last function parameter 
	ne10_fft_c2c_1d_float32_neon (frequencyDomain, timeDomainIn, cfg, 0);

	// Discard phase information
	/*for(int n = 0; n < gFFTSize; n++) {
		float amplitude = sqrtf(frequencyDomain[n].r * frequencyDomain[n].r + frequencyDomain[n].i * frequencyDomain[n].i);
		frequencyDomain[n].r = amplitude;
		frequencyDomain[n].i = 0;
	}*/
	
	// Get bin for current fundamental frequency
	int binF0 = int(map(gFrequency, 0, float(gSampleRate) / 2.0f, 0.0, float(gFFTSize)));
	
	// Define first seven overtone bins
	int nOvertones = 15;
	int* overtones = new int[nOvertones];
	for (int i = 0; i < nOvertones; i++) {
		overtones[i] = binF0 * (i + 1);
	}
	
	// Create a mask based on the current fundamental frequency
	if (gFrequency > 0.0f){
		// rt_printf("Current binF0: %i \n", binF0);
		for (int k = 0; k < gFFTSize; k++){
			
			// Check if current bin is overtone bin
			bool kInOvertone = false;
			for (int i = 0; i < nOvertones; i++) {
				if(k == overtones[i] || k - 1 == overtones[i] || k + 1 == overtones[i])
					kInOvertone = true;
			}
			
			if (!kInOvertone){
				frequencyDomain[k].r = 0;
				frequencyDomain[k].i = 0;
			}
		}
	}
	
	delete[] overtones;

	// Run the inverse FFT -> indicated by the "1" for the last function parameter 
	ne10_fft_c2c_1d_float32_neon (timeDomainOut, frequencyDomain, cfg, 1);
	
	// Overlap-and-add timeDomainOut into the output buffer
	pointer = outWritePointer;
	for(int n = 0; n < gFFTSize; n++) {
		outBuffer[pointer] += (timeDomainOut[n].r);// * gFFTScaleFactor;
		if(std::isnan(outBuffer[pointer]))
			rt_printf("outBuffer OLA \n");
		pointer++;
		if(pointer >= CB_LENGTH)
			pointer = 0;
	}
}

// Function to process the FFT in a thread at lower priority
void process_fft_background(void*) {
	process_fft(gInputBuffer, gFFTInputBufferPointer, gOutputBuffer, gFFTOutputBufferPointer);
}

void render(BelaContext *context, void *userData)
{
	// Get number of audio frames
	int numAudioFrames = context->audioFrames;

	// Prep the "input" to be the sound file played in a loop
	for(int n = 0; n < numAudioFrames; n++) {
		if(gReadPtr < gSampleData.sampleLen)
			gInputAudio[2 * n] = gInputAudio[2 * n + 1] = gSampleData.samples[gReadPtr];
		else
			gInputAudio[2 * n] = gInputAudio[2 * n + 1] = 0;
		if(++gReadPtr >= gSampleData.sampleLen)
			gReadPtr = 0;
	}

	for(int n = 0; n < numAudioFrames; n++) {
		gInputBuffer[gInputBufferPointer] = ((gInputAudio[n*gAudioChannelNum] + gInputAudio[n*gAudioChannelNum+1]) * 0.5);

		// Copy output buffer to output
		for(int channel = 0; channel < gAudioChannelNum; channel++){
			audioWrite(context, n, channel, gOutputBuffer[gOutputBufferReadPointer]);
		}

		// Clear the output sample in the buffer so it is ready for the next overlap-add
		gOutputBuffer[gOutputBufferReadPointer] = 0;
		gOutputBufferReadPointer++;
		if(gOutputBufferReadPointer >= CB_LENGTH)
			gOutputBufferReadPointer = 0;
		gOutputBufferWritePointer++;
		if(gOutputBufferWritePointer >= CB_LENGTH)
			gOutputBufferWritePointer = 0;

		gInputBufferPointer++;
		if(gInputBufferPointer >= CB_LENGTH)
			gInputBufferPointer = 0;

		gSampleCount++;
		if(gSampleCount >= gHopSize) {
			//process_fft(gInputBuffer, gInputBufferPointer, gOutputBuffer, gOutputBufferPointer);
			gFFTInputBufferPointer = gInputBufferPointer;
			gFFTOutputBufferPointer = gOutputBufferWritePointer;
			Bela_scheduleAuxiliaryTask(gFFTTask);

			gSampleCount = 0;
		}
	}
}

void cleanup(BelaContext *context, void *userData)
{
	NE10_FREE(timeDomainIn);
	NE10_FREE(timeDomainOut);
	NE10_FREE(frequencyDomain);
	NE10_FREE(cfg);
	free(gInputAudio);
	free(gWindowBuffer);
}