#ifndef __GSPAINTBRUSH_PLUGIN_h__
#define __GSPAINTBRUSH_PLUGIN_h__

#include <SOP/SOP_Node.h>
#include "GSPaintBrush.h"

namespace HDK_Sample {
    class SOP_GSPaintBrush : public SOP_Node
    {
    public:
        static OP_Node* myConstructor(OP_Network*, const char*, OP_Operator*);
        static PRM_Template  myTemplateList[];
        static CH_LocalVariable myVariables[];

    protected:
        SOP_GSPaintBrush(OP_Network* net, const char* name, OP_Operator* op);
        virtual ~SOP_GSPaintBrush();

        virtual unsigned     disableParms();
        virtual OP_ERROR     cookMySop(OP_Context& context);
        virtual bool         evalVariableValue(fpreal& val, int index, int thread);
        virtual bool         evalVariableValue(UT_String& v, int i, int thread)
        {
            return evalVariableValue(v, i, thread);
        }

    private:
        // parameter accessors
        fpreal  SCALE(fpreal t) { return evalFloat("scale", 0, t); }
        fpreal  OPACITY(fpreal t) { return evalFloat("opacity", 0, t); }
        fpreal  DENSITY(fpreal t) { return evalFloat("density", 0, t); }
        fpreal  BRUSH_RADIUS(fpreal t) { return evalFloat("brush_radius", 0, t); }
        int     PREVIEWMODE(fpreal t) { return evalInt("preview_mode", 0, t); }
        int     ERASEMODE(fpreal t) { return evalInt("erase_mode", 0, t); }
        int     EVENT(fpreal t) { return evalInt("event", 0, t); }

        UT_Vector3 ORIGIN(fpreal t)
        {
            return UT_Vector3(evalFloat("origin", 0, t),
                evalFloat("origin", 1, t),
                evalFloat("origin", 2, t));
        }
        UT_Vector3 DIRECTION(fpreal t)
        {
            return UT_Vector3(evalFloat("direction", 0, t),
                evalFloat("direction", 1, t),
                evalFloat("direction", 2, t));
        }

        static int onClearPoints(void* data, int index, fpreal t, const PRM_Template*);

        int  myCurrPoint;
        int  myTotalPoints;
    };
} // End HDK_Sample namespace

#endif