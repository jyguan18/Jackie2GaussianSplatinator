#include "GSPaintBrush.h"    
#include <fstream>
#include <stack>

#pragma warning(disable : 4244)
#pragma warning(disable : 4290)
#include "matrix.h"

#define Rad2Deg 57.295779513082320876798154814105
#define Deg2Rad 0.017453292519943295769236907684886

GSPaintBrush::GSPaintBrush() : mScale(1.0), mOpacity(1.0)
{
}

void GSPaintBrush::setScale(float scale)
{
	mScale = scale;
}

void GSPaintBrush::setOpacity(float opacity)
{
    mOpacity = opacity;
}

float GSPaintBrush::getScale() const
{
    return mScale;
}

float GSPaintBrush::getOpacity() const
{
    return mOpacity;
}