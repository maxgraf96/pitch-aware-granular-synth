#include <Bela.h>
#include <cmath>
#include <memory>
#include <set>
#include <libraries/ne10/NE10.h> // NEON FFT library
#include <libraries/Midi/Midi.h>
#include <numeric>
#include <atomic>

// Definition for global variables so as to avoid cluttering the render.cpp code
#include "Globals.h"
// #include "CircularBuffer.h"
#include "SampleData.h"
#include "Voice.h"

// Audio channels
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
int gHopSize = 512;
float gFFTScaleFactor = 0;

// Neon params
ne10_fft_cpx_float32_t* timeDomainIn;
ne10_fft_cpx_float32_t* timeDomainOut;
ne10_fft_cpx_float32_t* frequencyDomain;
ne10_fft_cfg_float32_t cfg;

// Large buffer that's updated every ~100ms given that no keys are pressed
// Once a key is pressed, this buffer will be used as source material
// to extract grains from
std::array<ne10_fft_cpx_float32_t*, 10> frequencyDomainGrainBuffer = {};
std::array<ne10_fft_cpx_float32_t*, 10> frequencyDomainGrainBufferSwap = {};

// Pointer to current grain buffer
std::array<ne10_fft_cpx_float32_t*, 10>* currentGrainBuffer = nullptr;

// Counter to 10 to check when grain buffer is ready
int grainBufferIdx = 0;

// Flag to indicate which buffer is currently safe to read from
// If true, it is safe to read from frequencyDomainGrainBuffer, otherwise it is safe to read from frequencyDomainGrainBufferSwap
bool grainBufferSafeToRead = false;

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

// Voices: An array indicating which voice (default number of voices: 16) currently plays which frequency
// All voices are initially "not playing" =^= -1
float voices[NUM_VOICES] = { NOT_PLAYING };
std::vector<Voice> voiceObjects = {};

// Expose sample rate
int gSampleRate = 0;

// Locks
// std::atomic<bool> canAccessBuffer (true);

void midiCallback(MidiChannelMessage message, void* arg){
	// Note on event: Find the next free voice and assign frequency coming from MIDI note
	if(message.getType() == kmmNoteOn){
		// Get grain buffer
		currentGrainBuffer = grainBufferSafeToRead ? &frequencyDomainGrainBufferSwap : &frequencyDomainGrainBuffer;

		if(message.getDataByte(1) > 0){
			int note = message.getDataByte(0);
			float frequency = powf(2, (note-69)/12.f)*440;
			
			// Find free voice and assign
			for (int i = 0; i < NUM_VOICES; i++){
				if (voices[i] == NOT_PLAYING) {
					// Assign frequency of incoming MIDI note
					voices[i] = frequency;
					// Trigger note on event
					voiceObjects[i].noteOn(*currentGrainBuffer, frequency);
					
					// Break loop
					break;
				}
			}
			rt_printf("\nnote: %d, frequency: %f \n", note, frequency);
		}
	}
	// Note off event: Find voice for incoming frequency and set to NOT_PLAYING
	if(message.getType() == kmmNoteOff){
		// Get frequency of incoming note off 
		int note = message.getDataByte(0);
		float frequency = powf(2, (note-69)/12.f)*440;
		
		// Find the voice that plays this note remove assignment
		for (int i = 0; i < NUM_VOICES; i++){
			if (voices[i] == frequency) {
				// Reset voice
				voices[i] = NOT_PLAYING;
				
				// Trigger note off event
				voiceObjects[i].noteOff();
				
				// Break loop
				break;
			}
		}
	}
}

bool setup(BelaContext *context, void *userData)
{
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

	gFFTScaleFactor = 1.0f / (float)N_FFT;
	gOutputBufferWritePointer += gHopSize;

	// Allocate memory for time domain inputs and outputs
	// frequency domain representation and the configuration for NEON
	timeDomainIn = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	timeDomainOut = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	frequencyDomain = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
	cfg = ne10_fft_alloc_c2c_float32_neon (N_FFT);
	
	// Initialise grain buffers
	for (int i = 0; i < 10; i++){
		frequencyDomainGrainBuffer[i] = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
		frequencyDomainGrainBufferSwap[i] = (ne10_fft_cpx_float32_t*) NE10_MALLOC (N_FFT * sizeof (ne10_fft_cpx_float32_t));
		
		for (int k = 0; k < N_FFT; k++){
			frequencyDomainGrainBuffer[i][k].r = 0.0f;
			frequencyDomainGrainBuffer[i][k].i = 0.0f;
			
			frequencyDomainGrainBufferSwap[i][k].r = 0.0f;
			frequencyDomainGrainBufferSwap[i][k].i = 0.0f;
		}
	}
	
	
	memset(timeDomainOut, 0, N_FFT * sizeof (ne10_fft_cpx_float32_t));
	memset(gOutputBuffer, 0, CB_LENGTH * sizeof(float));

	// Allocate buffer to mirror and modify the input
	gInputAudio = (float *)malloc(context->audioFrames * gAudioChannelNum * sizeof(float));
	if(gInputAudio == 0)
		return false;

	// Allocate the window buffer based on the FFT size
	gWindowBuffer = (float *)malloc(N_FFT * sizeof(float));
	if(gWindowBuffer == 0)
		return false;

	// Calculate a Hann window
	for(int n = 0; n < N_FFT; n++) {
		gWindowBuffer[n] = 0.5f * (1.0f - cosf(2.0f * M_PI * n / (float)(N_FFT - 1)));
	}

	// Initialise auxiliary tasks
	if((gFFTTask = Bela_createAuxiliaryTask(&process_fft_background, 90, "fft-calculation")) == 0)
		return false;
		
	// Setup MIDI
	midi.readFrom(0);
	midi.setParserCallback(midiCallback);
	
	// Expose audio sample rate
	gSampleRate = context->audioSampleRate;
	
	// Initialise voices array 
	for (auto& voice : voices) {
		voice = NOT_PLAYING;
	}
	
	// Initialise voice voice objects
	for (int i = 0; i < NUM_VOICES; i++){
		Voice* voice = new Voice(gSampleRate);
		voiceObjects.push_back(*voice);
	}
	
	return true;
}

// This function handles the FFT processing in this example once the buffer has
// been assembled.
void process_fft(float *inBuffer, int inWritePointer, float *outBuffer, int outWritePointer)
{
	// Copy buffer into FFT input
	int pointer = (inWritePointer - N_FFT + CB_LENGTH) % CB_LENGTH;
	for(int n = 0; n < N_FFT; n++) {
		timeDomainIn[n].r = (ne10_float32_t) inBuffer[pointer] * gWindowBuffer[n];
		timeDomainIn[n].i = 0;

		pointer++;
		if(pointer >= CB_LENGTH)
			pointer = 0;
	}

	// Run the FFT -> indicated by the "0" for the last function parameter 
	ne10_fft_c2c_1d_float32_neon (frequencyDomain, timeDomainIn, cfg, 0);
	
	// Update pointer to currently readable grain buffer
	currentGrainBuffer = grainBufferSafeToRead ? &frequencyDomainGrainBufferSwap : &frequencyDomainGrainBuffer;
	auto grainBuffer = *currentGrainBuffer;

	// Add the current frequency domain representation to the grain buffer
	for (int k = 0; k < N_FFT; k++){
		grainBuffer[grainBufferIdx][k].r = frequencyDomain[k].r;
		grainBuffer[grainBufferIdx][k].i = frequencyDomain[k].i;
	}
	
	// Increment grain buffer idx
	grainBufferIdx++;
	
	// Swap buffers if 10 x hop size was filled
	if(grainBufferIdx > 9){
		// Reset grain buffer index
		grainBufferIdx = 0;
		frequencyDomainGrainBuffer.swap(frequencyDomainGrainBufferSwap);
		grainBufferSafeToRead = !grainBufferSafeToRead;
	}

	// Run the inverse FFT -> indicated by the "1" for the last function parameter 
	ne10_fft_c2c_1d_float32_neon (timeDomainOut, frequencyDomain, cfg, 1);
	
	// Overlap-and-add timeDomainOut into the output buffer
	pointer = outWritePointer;
	for(int n = 0; n < N_FFT; n++) {
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
		// Get input buffer, currently from sample
		gInputBuffer[gInputBufferPointer] = ((gInputAudio[n*gAudioChannelNum] + gInputAudio[n*gAudioChannelNum+1]) * 0.5);

		// Write output buffer to sound output
		for(int channel = 0; channel < gAudioChannelNum; channel++){
			audioWrite(context, n, channel, gOutputBuffer[gOutputBufferReadPointer]);
		}
		
		// Check if any voice is currently playing
		int sum = std::accumulate(voices, voices + NUM_VOICES, 0);
		bool allVoicesOff = sum == -1 * NUM_VOICES;
		
		gOutputBuffer[gOutputBufferReadPointer] = 0;
		gOutputBufferReadPointer++;
		if(gOutputBufferReadPointer >= CB_LENGTH)
			gOutputBufferReadPointer = 0;
		gOutputBufferWritePointer++;
		if(gOutputBufferWritePointer >= CB_LENGTH)
			gOutputBufferWritePointer = 0;
				
		if(allVoicesOff){
			// If no voices are playing copy frequency domain data into grain buffer
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
		} else {
			// Get grain audio data from voices
			for(int voiceIdx = 0; voiceIdx < NUM_VOICES; voiceIdx++){
				if(voices[voiceIdx] > NOT_PLAYING){
					gOutputBuffer[gOutputBufferWritePointer] += voiceObjects[voiceIdx].play();
				}
			}
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
	
	// Memory for frequency domain mask
	for (int i = 0; i < 10; i++){
		NE10_FREE(frequencyDomainGrainBuffer[i]);
		NE10_FREE(frequencyDomainGrainBufferSwap[i]);
	}
}