#ifndef GSPaintBrush_H_
#define GSPaintBrush_H_

#include <string>
#include <vector>
#include <map>
#include "vec.h"


class GSPaintBrush
{
// TODO: Stroke struct.

public:
    GSPaintBrush();
    ~GSPaintBrush() {}

    // Set/get inputs
    void setPosition(vec3 pos);
    void setRadius(float rad);
    void setScale(float scale);
	void setOpacity(float opacity);

    vec3 getPosition() const;
    float getRadius() const;
	float getScale() const;
	float getOpacity() const;

protected:
    vec3 mPosition;
    float mRadius;
    float mScale; // Uniform for time being?
    float mOpacity;
};

#endif