/***** CircularBuffer.h *****/

/*
Implementation of a circular buffer with defined length "bufferLength"
*/
class CircularBuffer {
	public:
		// Create a circular buffer of length "bufferLength"
		CircularBuffer(int bufferLength);
		// Destruct the circular buffer object and release memory
		~CircularBuffer();
		// Push a sample into the buffer
		void pushFIFO(float sample);
		// Reset all values to 0
		void resetFIFO();
	
	private:
		// The length of the buffer in samples
		int bufferLength = 0;
		// The actual buffer data (array)
		float* buffer = nullptr;
		// The current buffer position
		int position = 0;
		// The value of the most recent sample
		float running = 0;
};