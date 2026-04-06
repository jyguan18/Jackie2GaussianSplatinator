#include "MSS_GSPaintState.h"

#include <DM/DM_Defines.h>
#include <DM/DM_ViewportType.h>
#include <GU/GU_PrimCircle.h>
#include <GU/GU_PrimPoly.h>
#include <MSS/MSS_SingleOpState.h>
#include <OP/OP_OperatorTable.h>
#include <PRM/PRM_Parm.h>
#include <RE/RE_Render.h>
#include <SOP/SOP_Node.h>
#include <UT/UT_String.h>

#define MSS_CLICK_BUTTONS (DM_PRIMARY_BUTTON|DM_SECONDARY_BUTTON)

using namespace HDK_Sample;

void
newModelState(BM_ResourceManager* m)
{
    m->registerState(
        new PI_StateTemplate("CusGSPaintBrush",   // must match SOP internal name
            "GS Paint Brush",
            "SOP_CusGSPaintBrush",
            (void*)MSS_GSPaintState::ourConstructor,
            MSS_GSPaintState::ourTemplateList,
            PI_VIEWER_SCENE,
            PI_NETMASK_SOP,
            0));
}

PRM_Template* MSS_GSPaintState::ourTemplateList = 0;

BM_State*
MSS_GSPaintState::ourConstructor(BM_View& view, PI_StateTemplate& templ,
    BM_SceneManager* scene)
{
    return new MSS_GSPaintState((JEDI_View&)view, templ, scene);
}

MSS_GSPaintState::MSS_GSPaintState(JEDI_View& view, PI_StateTemplate& templ,
    BM_SceneManager* scene, const char* cursor)
    : MSS_SingleOpState(view, templ, scene, cursor),
    myBrushHandle((DM_SceneManager&)workbench(), "MSS_GSPaintState")
{
    myIsBrushVisible = false;
    myResizingCursor = false;
    myIsDrawing = false;
    myBrushRadius = 0.5f;
    myCurrentStrokeStart = 0;

    // build circle cursor geometry
    GU_PrimCircleParms cparms;
    cparms.gdp = &myBrushCursor;
    cparms.order = 3;
    cparms.imperfect = 0;
    cparms.xform.identity();
    GU_PrimCircle::build(cparms, GEO_PRIMBEZCURVE);
    myBrushCursorXform.identity();

    setViewportMask(DM_VIEWPORT_PERSPECTIVE);
}

MSS_GSPaintState::~MSS_GSPaintState()
{
}

const char*
MSS_GSPaintState::className() const
{
    return "MSS_GSPaintState";
}

void
MSS_GSPaintState::buildRayIntersect()
{
    myCachedPoints.clear();
    myCachedNormals.clear();
    myCanvasGdp = nullptr;

    SOP_Node* sop = (SOP_Node*)getNode();
    if (!sop) return;

    OP_Context context(getTime());

    // try input 2 first (base Gaussian scene), then input 0 (brush stamp)
    OP_Node* inputNode = sop->getInput(2);
    if (!inputNode) inputNode = sop->getInput(0);
    if (!inputNode) return;

    SOP_Node* inputSop = dynamic_cast<SOP_Node*>(inputNode);
    if (!inputSop) return;

    const GU_Detail* geo = inputSop->getCookedGeo(context);
    if (!geo) return;

    myCanvasGdp = geo;

    // cache all point positions
    GA_ROHandleV3 nHandle(geo->findFloatTuple(GA_ATTRIB_POINT, "N", 3));

    GA_Offset ptoff;
    GA_FOR_ALL_PTOFF(geo, ptoff)
    {
        myCachedPoints.append(UT_Vector3F(geo->getPos3(ptoff)));

        // cache normal if available, otherwise use zero (we'll estimate later)
        if (nHandle.isValid())
            myCachedNormals.append(UT_Vector3F(nHandle.get(ptoff)));
        else
            myCachedNormals.append(UT_Vector3F(0, 1, 0));
    }

    printf("[GSPaint] Cached %d Gaussian points for painting\n",
        (int)myCachedPoints.size());
    fflush(stdout);
}

int
MSS_GSPaintState::enter(BM_SimpleState::BM_EntryType how)
{
    int result = MSS_SingleOpState::enter(how);
    wantsLocates(1);
    addClickInterest(MSS_CLICK_BUTTONS);
    updatePrompt();

    // clear stroke data on enter
    myStrokePositions.clear();
    myStrokeNormals.clear();
    myStrokeLengths.clear();
    myCurrentStrokeStart = 0;
    myIsDrawing = false;

    buildRayIntersect();

    OP_Node* op = getNode();
    if (op) op->setHighlight(0);

    return result;
}

void
MSS_GSPaintState::exit()
{
    wantsLocates(0);
    removeClickInterest(MSS_CLICK_BUTTONS);
    myIsBrushVisible = false;
    myIsDrawing = false;
    redrawScene();
    MSS_SingleOpState::exit();
}

void
MSS_GSPaintState::resume(BM_SimpleState* state)
{
    MSS_SingleOpState::resume(state);
    wantsLocates(1);
    addClickInterest(MSS_CLICK_BUTTONS);
    buildRayIntersect();
    updatePrompt();
    OP_Node* op = getNode();
    if (op) op->setHighlight(0);
}

void
MSS_GSPaintState::interrupt(BM_SimpleState* state)
{
    wantsLocates(0);
    removeClickInterest(MSS_CLICK_BUTTONS);
    myIsBrushVisible = false;
    redrawScene();
    MSS_SingleOpState::interrupt(state);
}

void
MSS_GSPaintState::flushToStrokeNode(fpreal t, const char* event)
{
    SOP_Node* sop = (SOP_Node*)getNode();
    if (!sop) { printf("[GSPaint] No SOP node!\n"); return; }

    OP_Network* net = sop->getParent();
    if (!net) { printf("[GSPaint] No parent network!\n"); return; }

    OP_Node* strokeNode = net->findNode("stroke_points");
    if (!strokeNode) { printf("[GSPaint] stroke_points not found!\n"); return; }

    printf("[GSPaint] Flushing %d positions, event=%s\n",
        (int)myStrokePositions.size(), event);
    fflush(stdout);

    // build position and normal strings
    UT_String posStr, normStr, lengthStr;
    posStr = "[";
    normStr = "[";
    for (int i = 0; i < myStrokePositions.size(); i++)
    {
        const UT_Vector3F& p = myStrokePositions[i];
        const UT_Vector3F& n = myStrokeNormals[i];
        UT_String ps, ns;
        ps.sprintf("(%g,%g,%g)", p.x(), p.y(), p.z());
        ns.sprintf("(%g,%g,%g)", n.x(), n.y(), n.z());
        if (i > 0) { posStr += ","; normStr += ","; }
        posStr += ps;
        normStr += ns;
    }
    posStr += "]";
    normStr += "]";

    lengthStr = "[";
    for (int i = 0; i < myStrokeLengths.size(); i++)
    {
        UT_String ls; ls.sprintf("%d", myStrokeLengths[i]);
        if (i > 0) lengthStr += ",";
        lengthStr += ls;
    }
    lengthStr += "]";

    strokeNode->setString(posStr, CH_STRING_LITERAL, "point_positions", 0, t);
    strokeNode->setString(normStr, CH_STRING_LITERAL, "point_normals", 0, t);
    strokeNode->setString(lengthStr, CH_STRING_LITERAL, "stroke_lengths", 0, t);

    OP_Context cookContext(t);
    strokeNode->cook(cookContext);

    redrawScene();
}

int
MSS_GSPaintState::handleMouseEvent(UI_Event* event)
{
    SOP_Node* sop = (SOP_Node*)getNode();
    if (!sop) return 1;

    fpreal t = getTime();
    fpreal paramRadius = sop->evalFloat("brush_radius", 0, t);
    if (!myResizingCursor)  // don't override if user is manually resizing
        myBrushRadius = (float)paramRadius;

    int x = event->state.values[X];
    int y = event->state.values[Y];

    // resize cursor with shift/alt drag
    if (event->reason == UI_VALUE_START &&
        (event->state.altFlags & UI_ALT_KEY ||
            event->state.altFlags & UI_SHIFT_KEY))
    {
        myResizeCursorX = x;
        myResizeCursorY = y;
        myResizingCursor = true;
    }
    else if (myResizingCursor)
    {
        float dist = x - myLastCursorX + y - myLastCursorY;
        myBrushRadius *= powf(1.01f, dist);
        myBrushRadius = SYSmax(myBrushRadius, 0.01f);

        sop->setFloat("brush_radius", 0, t, myBrushRadius);

        if (event->reason == UI_VALUE_CHANGED)
            myResizingCursor = false;
        updateBrush(myResizeCursorX, myResizeCursorY);
    }
    else if (event->reason == UI_VALUE_LOCATED)
    {
        // just hovering — update cursor
        updateBrush(x, y);
    }
    else
    {
        // painting stroke
        UT_Vector3 rayorig, dir;
        mapToWorld(x, y, dir, rayorig);

        // keeping in world space so no transformations
        /*xformToObjectCoord(rayorig);
        xformToObjectVector(dir);*/

        bool begin = (event->reason == UI_VALUE_START ||
            event->reason == UI_VALUE_PICKED);
        bool active = (event->reason == UI_VALUE_ACTIVE ||
            event->reason == UI_VALUE_PICKED);
        bool end = (event->reason == UI_VALUE_CHANGED ||
            event->reason == UI_VALUE_PICKED);

        if (begin)
        {
            // check if stroke_points was externally cleared
            OP_Network* net = sop->getParent();
            if (net)
            {
                OP_Node* strokeNode = net->findNode("stroke_points");
                if (strokeNode)
                {
                    UT_String posStr;
                    strokeNode->evalString(posStr, "point_positions", 0, t);
                    if (posStr == "" || posStr == "[]")
                    {
                        // externally cleared — reset our cached strokes
                        myStrokePositions.clear();
                        myStrokeNormals.clear();
                        myStrokeLengths.clear();
                        myCurrentStrokeStart = 0;
                       
                        printf("[GSPaint] Detected external clear — resetting strokes.\n");
                    }
                }
            }

            beginDistributedUndoBlock("GS Paint Stroke", ANYLEVEL);
            myCurrentStrokeStart = myStrokePositions.size();
            myIsDrawing = true;
        }

        if (active && myIsDrawing)
        {
            if (myCachedPoints.size() > 0)
            {
                UT_Vector3F ro(rayorig), rd(dir);
                rd.normalize();

                // find nearest Gaussian point to ray
                float bestDist = 1e10f;
                float bestT = 1e10f;  // depth along ray
                int   bestIdx = -1;

                for (int i = 0; i < myCachedPoints.size(); i++)
                {
                    const UT_Vector3F& p = myCachedPoints[i];
                    UT_Vector3F op = p - ro;
                    float t_val = op.dot(rd);
                    if (t_val < 0) continue;  // behind camera

                    UT_Vector3F closest = ro + rd * t_val;
                    float dist = (p - closest).length();

                    if (dist > myBrushRadius) continue;  // outside brush radius

                    // among points within brush radius, prefer the closest to camera
                    if (t_val < bestT)
                    {
                        bestT = t_val;
                        bestDist = dist;
                        bestIdx = i;
                    }
                }

                if (bestIdx >= 0)
                {
                    UT_Vector3F hitPos = myCachedPoints[bestIdx];
                    UT_Vector3F hitNorm = myCachedNormals[bestIdx];
                    if (hitNorm.length() < 1e-6f) hitNorm = UT_Vector3F(0, 1, 0);

                    bool tooClose = false;
                    if (myStrokePositions.size() > (exint)myCurrentStrokeStart)
                    {
                        UT_Vector3F last = myStrokePositions.last();
                        if ((hitPos - last).length() < myBrushRadius * 0.01f)
                            tooClose = true;
                    }

                    if (!tooClose)
                    {
                        myStrokePositions.append(hitPos);
                        myStrokeNormals.append(hitNorm);
                    }
                }
            }

            flushToStrokeNode(t, "active");
        }

        if (end && myIsDrawing)
        {
            // record stroke length
            int strokeLen = myStrokePositions.size() - myCurrentStrokeStart;
            if (strokeLen > 0)
                myStrokeLengths.append(strokeLen);

            myIsDrawing = false;
            flushToStrokeNode(t, "end");
            endDistributedUndoBlock();
        }

        updateBrush(x, y);
    }

    myLastCursorX = x;
    myLastCursorY = y;
    return 1;
}

static void
buildCatmullRomPreview(GU_Detail& geo,
    const UT_Array<UT_Vector3F>& pts,
    int start, int count, int samples_per_unit = 200)
{
    if (count < 2) return;

    // arc length
    UT_Array<float> arc;
    arc.append(0.f);
    for (int i = 1; i < count; i++)
        arc.append(arc[i - 1] + (pts[start + i] - pts[start + i - 1]).length());
    float total = arc[count - 1];
    if (total < 0.001f) return;

    int n_samples = SYSmax(2, (int)(total * samples_per_unit));

    UT_Array<GA_Offset> ptOffsets;
    for (int i = 0; i < n_samples; i++)
    {
        float t = total * (float)i / (float)(n_samples - 1);

        // find segment
        if (t >= arc[count - 1]) t = arc[count - 1];
        int seg = 0;
        for (int j = 0; j < count - 1; j++)
            if (arc[j] <= t && t <= arc[j + 1]) { seg = j; break; }

        float seg_len = arc[seg + 1] - arc[seg];
        float u = seg_len > 0.f ? (t - arc[seg]) / seg_len : 0.f;

        UT_Vector3F p0 = pts[start + SYSmax(seg - 1, 0)];
        UT_Vector3F p1 = pts[start + seg];
        UT_Vector3F p2 = pts[start + SYSmin(seg + 1, count - 1)];
        UT_Vector3F p3 = pts[start + SYSmin(seg + 2, count - 1)];

        UT_Vector3F pos = 0.5f * (2.f * p1 + (-p0 + p2) * u +
            (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * u * u +
            (-p0 + 3.f * p1 - 3.f * p2 + p3) * u * u * u);

        GA_Offset pt = geo.appendPoint();
        geo.setPos3(pt, UT_Vector3(pos));
        ptOffsets.append(pt);
    }

    GU_PrimPoly* poly = GU_PrimPoly::build(&geo, 0, GU_POLY_OPEN, 0);
    for (GA_Offset off : ptOffsets) poly->appendVertex(off);
}

void
MSS_GSPaintState::doRender(RE_Render* r, int, int, int ghost)
{
    if (!isPreempted() && myIsBrushVisible)
    {
        UT_Color clr;
        r->pushMatrix();
        r->multiplyMatrix(myBrushCursorXform);

        if (myIsDrawing)
            clr = UT_Color(UT_RGB, 1.0, 0.5, 0.0);  // orange when painting
        else if (ghost)
            clr = UT_Color(UT_RGB, 0.5, 0.5, 0.5);  // grey when occluded
        else
            clr = UT_Color(UT_RGB, 1.0, 1.0, 1.0);  // white when hovering

        myBrushHandle.renderWire(r, 0, 0, 0, clr, &myBrushCursor);
        r->popMatrix();
    }

    // having issue with drawing stamps during painting, so will just draw stroke preview directly during painting
    if (!isPreempted() && myIsDrawing && myStrokePositions.size() > 1)
    {
        GU_Detail strokePreview;

        // draw completed strokes as smooth curves
        int start = 0;
        for (int s = 0; s < myStrokeLengths.size(); s++)
        {
            buildCatmullRomPreview(strokePreview, myStrokePositions,
                start, myStrokeLengths[s]);
            start += myStrokeLengths[s];
        }

        // draw current in-progress stroke
        int currentLen = myStrokePositions.size() - myCurrentStrokeStart;
        if (currentLen >= 2)
            buildCatmullRomPreview(strokePreview, myStrokePositions,
                myCurrentStrokeStart, currentLen);

        UT_Color strokeClr(UT_RGB, 1.0, 0.5, 0.0);
        myBrushHandle.renderWire(r, 0, 0, 0, strokeClr, &strokePreview);
    }
}

void
MSS_GSPaintState::updatePrompt()
{
    showPrompt("LMB drag to paint. Shift-drag to resize brush. Escape to exit.");
}

void
MSS_GSPaintState::updateBrush(int x, int y)
{
    getViewportItransform(myBrushCursorXform);
    UT_Vector3 forward = rowVecMult3(UT_Vector3(0, 0, -1), myBrushCursorXform);
    UT_Vector3 rayorig, dir;
    mapToWorld(x, y, dir, rayorig);
    UT_Vector3 delta(1.0 / dot(dir, forward) * dir);
    myBrushCursorXform.translate(delta.x(), delta.y(), delta.z());
    myBrushCursorXform.prescale(myBrushRadius, myBrushRadius, 1);
    myIsBrushVisible = true;
    redrawScene();
}