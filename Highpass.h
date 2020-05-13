/***** Highpass.h *****/
#include <Bela.h>
#include <libraries/Gui/Gui.h>
#include <libraries/GuiController/GuiController.h>
#include <cmath>

/* 
 * Second order highpass filter
*/
class Highpass{
	public:
		// Create an instance of the filter for the specified sample rate
		Highpass(float sampleRate);
		
		// Calculate filter coefficients with given specifications
		void calculate_coefficients(float frequency, float q);
		
		// Process one sample
		float processSample(float sampleInput);
		
	private:
		float sampleRate = 0.0f;
		// Filter coefficients and T (= 1 / sample rate) as well as T squared
		float b0, b1, b2, a1, a2, T, TSquared;
		// The factor by which the coefficients are normalised (the a0 coefficient)
		float divisor = 0;
		// Placeholders for the previous input and the input previous to that
		float lastIn = 0, lastLastIn = 0, lastOut = 0, lastLastOut = 0;
		// The same for the second filter
		float lastIn2 = 0, lastLastIn2 = 0, lastOut2 = 0, lastLastOut2 = 0;
	
};
