#ifndef __MSS_GSPaintState_H__
#define __MSS_GSPaintState_H__

#include <MSS/MSS_SingleOpState.h>
#include <DM/DM_Detail.h>
#include <GU/GU_Detail.h>

// For kd tree.
#include <GEO/GEO_PointTree.h>

// Include PaintBrush plugin.
# include "GSPAINTBRUSHPlugin.h"

class JEDI_View;

namespace HDK_Sample {
    class MSS_GSPaintState : public MSS_SingleOpState
    {
    public:
        MSS_GSPaintState(JEDI_View& view, PI_StateTemplate& templ,
            BM_SceneManager* scene,
            const char* cursor = BM_DEFAULT_CURSOR);
        ~MSS_GSPaintState() override;

        static BM_State* ourConstructor(BM_View& view, PI_StateTemplate& templ,
            BM_SceneManager* scene);
        static PRM_Template* ourTemplateList;

        void handleOpNodeChange(OP_Node& node) override;

        const char* className() const override;

    protected:
        int                  enter(BM_SimpleState::BM_EntryType how) override;
        void                 exit() override;
        void                 interrupt(BM_SimpleState* = 0) override;
        void                 resume(BM_SimpleState* = 0) override;
        int                  handleMouseEvent(UI_Event* event) override;
        void                 doRender(RE_Render* r, int x, int y,
            int ghost) override;
        void                 updatePrompt();
        void                 updateBrush(int x, int y);

    private:
        bool        myIsBrushVisible;
        DM_Detail   myBrushHandle;
        GU_Detail   myBrushCursor;
        UT_Matrix4  myBrushCursorXform;
        float       myBrushRadius;
        bool        myResizingCursor;
        short       myLastCursorX, myLastCursorY;
        short       myResizeCursorX, myResizeCursorY;
        bool        myIsDrawing;
        bool        myIsPaintMode;
        UT_Vector3F myRayHitPos;
        UT_Vector3F myRayDir;
        bool        myHasCurrentHit;
        UT_Vector3F myCursorWorldPos;
        UT_Vector3F myCursorRight;

        bool myRayCacheDirty = true;
        exint myCachedPointCount = -1;
        const GU_Detail* myLastGeo = nullptr;

        const GU_Detail* getCanvasGeo(SOP_Node* sop, OP_Context& ctx) const;

        // stroke points accumulated during drag
        UT_Array<UT_Vector3F> myStrokePositions;
        UT_Array<UT_Vector3F> myStrokeNormals;
        UT_Array<int>         myStrokeLengths;
        UT_Array<UT_Vector4F> myStrokeBaseOrients;
        int                   myCurrentStrokeStart;
        UT_Array<UT_Vector3F> myStrokeRayHitPositions;
        UT_Array<UT_Vector3F> myStrokeRayDirs;

        const GU_Detail* myCanvasGdp;
        UT_Array<UT_Vector3F> myCachedPoints;
        UT_Array<UT_Vector3F> myCachedNormals;
        UT_Array<UT_Vector3F> myStampCenters; // stamp centers from SOP output, for hover hit

        // Cached geom for hover logic.
        // Only recalc when dirty.
        UT_Vector3F myLastHighlightPos;
        bool myHighlightDirty = true;
        GU_Detail myCachedHighlightGeo;
		GU_Detail myCachedStampHighlightGeo;

        void        flushToStrokeNode(fpreal t, const char* event);
        void        buildRayIntersect();

        // Kd tree variable.
        std::unique_ptr<GEO_PointTreeGAOffset> myPointTree;
    };
} // End HDK_Sample namespace

#endif