#ifndef __GSPAINTBRUSH_PLUGIN_h__
#define __GSPAINTBRUSH_PLUGIN_h__

#include <SOP/SOP_Node.h>
#include "GSPaintBrush.h"
#include <UT/UT_Map.h>
#include <UT/UT_Set.h>
#include <UT/UT_Array.h>
#include <GA/GA_Types.h>

// For kd tree.
#include <GEO/GEO_PointTree.h>

// For undo.
#include <UT/UT_Undo.h>

namespace HDK_Sample {
    class SOP_UndoGSPaintStroke : public UT_Undo
    {
    public:
        SOP_UndoGSPaintStroke(OP_Node* node,
            const UT_String& oldPos,
            const UT_String& newPos,
            const UT_String& oldNorm,
            const UT_String& newNorm,
            const UT_String& oldLen,
            const UT_String& newLen);

        void undo() override;
        void redo() override;

    private:
        int myNodeId;

        UT_String myOldPos, myNewPos;
        UT_String myOldNorm, myNewNorm;
        UT_String myOldLen, myNewLen;

        void apply(const UT_String&, const UT_String&, const UT_String&);
    };

    class SOP_GSPaintBrush : public SOP_Node
    {
    public:
        static OP_Node* myConstructor(OP_Network*, const char*, OP_Operator*);
        static PRM_Template  myTemplateList[];
        static CH_LocalVariable myVariables[];

        GEO_PointTreeGAOffset* getBaseKDTree() const
        {
            return myBaseKDTree.get();
        }

        void setBaseKDTree(std::unique_ptr<GEO_PointTreeGAOffset>&& tree, exint frame)
        {
            myBaseKDTree = std::move(tree);
            myBaseKDBuildFrame = frame;
        }

        bool hasValidBaseKDTree(exint frame) const
        {
            return myBaseKDTree && myBaseKDBuildFrame == frame;
        }

    protected:
        SOP_GSPaintBrush(OP_Network* net, const char* name, OP_Operator* op);
        virtual ~SOP_GSPaintBrush();

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
        int     ERASEMODE(fpreal t) { return evalInt("erase_mode", 0, t); }
        int     EVENT(fpreal t) { return evalInt("event", 0, t); }

        int     OPERATION(fpreal t) { return evalInt("operation", 0, t); }
        UT_Vector3 PAINTCOLOR(fpreal t)
        {
            return UT_Vector3(evalFloat("paint_color", 0, t),
                evalFloat("paint_color", 1, t),
                evalFloat("paint_color", 2, t));
        }
        fpreal  PAINTALPHA(fpreal t) { return evalFloat("paint_alpha", 0, t); }
        int     COLORSOURCE(fpreal t) { return evalInt("color_source", 0, t); }
        int     PAINTCD(fpreal t) { return evalInt("paint_cd", 0, t); }
        int     PAINTALPHA2(fpreal t) { return evalInt("paint_alpha_on", 0, t); }
        int     PAINTSCALE(fpreal t) { return evalInt("paint_scale", 0, t); }
        fpreal PAINTSCALEMUL(fpreal t) { return evalFloat("paint_scale_mul", 0, t); }

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

        int ERASEBASE(fpreal t) { return evalInt("erase_base", 0, t); }
        int ORIENTMODE(fpreal t) { return evalInt("orient_mode", 0, t); }

        static int onClearPoints(void* data, int index, fpreal t, const PRM_Template*);

        // persistent accumulated state
        struct GaussianAttribs {
            UT_Vector3F cd;
            float       alpha;
            UT_Vector3F scale;
            UT_Vector4F orient;
            UT_Vector3F pos;
            UT_Vector3F stampCenter; // spline hit point this splat was placed from
            int         piece;       // which stroke this splat belongs to
            int         seqIdx;      // position within the stroke spline (for ordering)
        };

        // Updated stamp mode: map of stamp ID to GaussianAttrib.
        UT_Map<int, GaussianAttribs> myStampedGaussians;
        // Stamp ID counter.
        int myNextStampId;

        int myLastOperation = -1;
        int myOperationSwitchStrokeSize = 0;

        // paint mode: per-point color overrides (keyed by point index in base scene)
        UT_Map<GA_Index, GaussianAttribs> myPaintedAttribs;

        // erase mode: set of erased point indices from base scene
        UT_Set<GA_Index> myErasedPoints;

        // Index to operation mode (stamp/paint/erase) for each stroke.
        UT_Map<int, int> myStrokeModeMap;
        // set of all piece indices seen so far, to detect newly arriving pieces.
        UT_Set<int> myKnownPieces;

        // track last processed stroke length to avoid reprocessing
        int myLastProcessedStrokeSize;

        std::unique_ptr<GEO_PointTreeGAOffset> myBaseKDTree;
        exint myBaseKDBuildFrame = -1;

        std::unique_ptr<GEO_PointTreeGAOffset> myStampKDTree;

        UT_Array<GA_Offset> myStampPointOffsets;
        UT_Array<UT_Vector3F> myStampPositions;

        GU_Detail myTempStampGeo;
        UT_Array<int> myStampKeyOrder;

        int  myCurrPoint;
        int  myTotalPoints;

        // Track last updated parameters.
        float myLastDensity;
        float myLastScale;
        float myLastOpacity;
        float myLastBrushRadius;
    };
} // End HDK_Sample namespace

#endif