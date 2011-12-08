/*  Copyright 2011, The Ready Bunch

    This file is part of Ready.

    Ready is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ready is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ready. If not, see <http://www.gnu.org/licenses/>.         */

// local:
#include "BaseRD.hpp"

// stdlib:
#include <stdlib.h>
#include <math.h>

// STL:
#include <cassert>
using namespace std;

// VTK:
#include <vtkImageData.h>

BaseRD::BaseRD() : image_data(NULL), timesteps_taken(0)
{
}

BaseRD::~BaseRD()
{
    if(this->image_data)
        this->image_data->Delete();
}

int BaseRD::GetDimensionality() 
{
    assert(this->image_data);
    int dimensionality=0;
    for(int iDim=0;iDim<3;iDim++)
        if(this->image_data->GetDimensions()[iDim]>1)
            dimensionality++;
    return dimensionality;
}

int BaseRD::GetNumberOfChemicals()
{
    assert(this->image_data);
    return this->image_data->GetNumberOfScalarComponents();
}

float BaseRD::GetTimestep() 
{ 
    return this->timestep; 
}

vtkImageData* BaseRD::GetVTKImage() 
{ 
    assert(this->image_data);
    return this->image_data; 
}

int BaseRD::GetTimestepsTaken()
{
    return this->timesteps_taken;
}

// ------------- utility functions feel a bit lost here ----------------------

float frand(float lower,float upper)
{
    return lower + rand()*(upper-lower)/RAND_MAX;
}

double hypot2(double x,double y) 
{ 
    return sqrt(x*x+y*y); 
}

double hypot3(double x,double y,double z) 
{ 
    return sqrt(x*x+y*y+z*z); 
}
