/***** CircularBuffer.cpp *****/
#include "CircularBuffer.h"

CircularBuffer::CircularBuffer(int bufferLength){
	this->bufferLength = bufferLength;
	this->buffer = new float[bufferLength];
	
	// Set all buffer elements to 0
	for (int i = 0; i < bufferLength; i++){
		buffer[i] = 0;
	}
}

void CircularBuffer::pushFIFO(float sample) {
	// Update the running value (most recent sample)
    const float old = buffer[position]; 
    running -= old;
    const float newValue = sample;
    buffer[position] = newValue;
    running += newValue; 
    // Update position
    position++;
    // Reset position to 0 if past buffer length
    position %= bufferLength;
}

void CircularBuffer::resetFIFO(){
	// Set all values to 0
	for(int i = 0; i < bufferLength; i++) 
		buffer[i] = 0;
	
	// Reset reader values
	running = 0;
	position = 0;
}

CircularBuffer::~CircularBuffer(){
	// Release array memory
	delete buffer;
}