/***** Grain.cpp *****/
#include <cmath>
#include "Grain.h"

Grain::Grain() {
}

Grain::Grain(int length) {
	this->length = length;
}

void Grain::updateLength(int length){
	this->length = length;
}

Grain::~Grain(){
}