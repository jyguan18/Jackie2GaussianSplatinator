#include "GSPaintBrush.h"    
#include <fstream>
#include <stack>

#pragma warning(disable : 4244)
#pragma warning(disable : 4290)
#include "matrix.h"

#define Rad2Deg 57.295779513082320876798154814105
#define Deg2Rad 0.017453292519943295769236907684886

GSPaintBrush::GSPaintBrush() : mPosition(vec3(0.0,0.0,0.0)), mRadius(1.0), mScale(1.0), mOpacity(1.0)
{
}

void GSPaintBrush::setPosition(vec3 pos)
{
    mPosition = pos;
}

void GSPaintBrush::setRadius(float rad)
{
    mRadius = rad;
}

void GSPaintBrush::setScale(float scale)
{
	mScale = scale;
}

void GSPaintBrush::setOpacity(float opacity)
{
    mOpacity = opacity;
}

vec3 GSPaintBrush::getPosition() const
{
    return mPosition;
}

float GSPaintBrush::getRadius() const
{
    return mRadius;
}

float GSPaintBrush::getScale() const
{
    return mScale;
}

float GSPaintBrush::getOpacity() const
{
    return mOpacity;
}