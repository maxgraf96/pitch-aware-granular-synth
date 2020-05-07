/***** Grain.h *****/
#include "Constants.h"
#include <array>

class Grain {
	public:
		// The beginning of this grain (idx to voice buffer)
		// => All grains in a voice share the same underlying buffer and their sample values
		// are calculated on the fly -> saves a lot of memory
		// as each grain doesn't need its own buffer hehe
		int bufferStartIdx = 0;
		
		// The length of the grain in samples
		int length = 0;
		
		Grain();
		Grain(int length);
		
		void updateLength(int length);
		
		~Grain();
};