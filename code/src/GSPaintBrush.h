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
    void setScale(float scale);
	void setOpacity(float opacity);

	float getScale() const;
	float getOpacity() const;

protected:
    float mScale; // Uniform for time being?
    float mOpacity;
};

#endif