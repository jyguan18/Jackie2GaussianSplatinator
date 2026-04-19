#include <UT/UT_DSOVersion.h>
#include <UT/UT_Math.h>
#include <UT/UT_Interrupt.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPoly.h>
#include <CH/CH_LocalVariable.h>
#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <GA/GA_Handle.h>
#include <GA/GA_Iterator.h>
#include <GA/GA_AttributeRefMap.h>
#include <SYS/SYS_Math.h>
#include <UT/UT_Quaternion.h>
#include <limits.h>
#include <map>
#include "GSPAINTBRUSHPlugin.h"
#include <CH/CH_Manager.h>

using namespace HDK_Sample;

void
newSopOperator(OP_OperatorTable* table)
{
    table->addOperator(
        new OP_Operator("CusGSPaintBrush",
            "GSPaintBrush",
            SOP_GSPaintBrush::myConstructor,
            SOP_GSPaintBrush::myTemplateList,
            2,
            3,
            SOP_GSPaintBrush::myVariables,
            0)
    );
}

// parameter names
static PRM_Name names[] = {
    PRM_Name("scale",        "Scale"),
    PRM_Name("opacity",      "Opacity"),
    PRM_Name("density",      "Density"),
    PRM_Name("brush_radius", "Brush Radius"),
    PRM_Name("preview_mode", "Preview Mode"),
    PRM_Name("erase_mode",   "Erase Mode"),
    PRM_Name("origin",       "Origin"),
    PRM_Name("direction",    "Direction"),
    PRM_Name("event",        "Event"),
    PRM_Name("clear_points", "Clear Points"),
    PRM_Name("operation",     "Operation"),
    PRM_Name("paint_color",   "Paint Color"),
    PRM_Name("paint_alpha",   "Paint Alpha"),
    PRM_Name("color_source",  "Color Source"),
    PRM_Name("paint_cd",      "Modify Color"),
    PRM_Name("paint_alpha_on","Modify Alpha"),
    PRM_Name("paint_scale",   "Modify Scale"),
};

static PRM_Default scaleDefault(1.0f);
static PRM_Default opacityDefault(1.0f);
static PRM_Default densityDefault(20.0f);
static PRM_Default brushRadiusDefault(0.5f);
static PRM_Default buttonDefault(0);

static PRM_Range scaleRange(PRM_RANGE_UI, 0.0f, PRM_RANGE_UI, 10.0f);
static PRM_Range opacityRange(PRM_RANGE_UI, 0.0f, PRM_RANGE_UI, 1.0f);
static PRM_Range densityRange(PRM_RANGE_UI, 1.0f, PRM_RANGE_UI, 100.0f);
static PRM_Range brushRadiusRange(PRM_RANGE_UI, 0.01f, PRM_RANGE_UI, 5.0f);

// operation menu
static PRM_Name operationMenuNames[] = {
    PRM_Name("stamp", "Stamp"),
    PRM_Name("paint", "Paint"),
    PRM_Name("erase", "Erase"),
    PRM_Name(0)
};
static PRM_ChoiceList operationMenu(PRM_CHOICELIST_SINGLE, operationMenuNames);
static PRM_Default    operationDefault(0); // stamp

// color source menu
static PRM_Name colorSourceMenuNames[] = {
    PRM_Name("picker", "Color Picker"),
    PRM_Name("stamp",  "Sample from Stamp"),
    PRM_Name(0)
};
static PRM_ChoiceList colorSourceMenu(PRM_CHOICELIST_SINGLE, colorSourceMenuNames);
static PRM_Default    colorSourceDefault(0); // picker

// paint defaults
static PRM_Default paintAlphaDefault(0.5f);
static PRM_Default paintColorDefault[] = {
    PRM_Default(1.0f), PRM_Default(0.0f), PRM_Default(0.0f)
};

// event menu
static PRM_Name eventMenuNames[] = {
    PRM_Name("begin",  "Begin Stroke"),
    PRM_Name("active", "Active Stroke"),
    PRM_Name("end",    "End Stroke"),
    PRM_Name("nop",    "No-op"),
    PRM_Name(0)
};
static PRM_ChoiceList eventMenu(PRM_CHOICELIST_SINGLE, eventMenuNames);
static PRM_Default    eventDefault(3); // nop

PRM_Template
SOP_GSPaintBrush::myTemplateList[] =
{
    PRM_Template(PRM_FLT,    1, &names[0], &scaleDefault,       nullptr, &scaleRange),
    PRM_Template(PRM_FLT,    1, &names[1], &opacityDefault,     nullptr, &opacityRange),
    PRM_Template(PRM_FLT,    1, &names[2], &densityDefault,     nullptr, &densityRange),
    PRM_Template(PRM_FLT,    1, &names[3], &brushRadiusDefault, nullptr, &brushRadiusRange),
    PRM_Template(PRM_ORD,      1, &names[10], &operationDefault,   &operationMenu),
    PRM_Template(PRM_RGB_J,    3, &names[11], paintColorDefault),
    PRM_Template(PRM_FLT,      1, &names[12], &paintAlphaDefault,  nullptr, &opacityRange),
    PRM_Template(PRM_ORD,      1, &names[13], &colorSourceDefault, &colorSourceMenu),
    PRM_Template(PRM_TOGGLE,   1, &names[14]),  // modify color
    PRM_Template(PRM_TOGGLE,   1, &names[15]),  // modify alpha
    PRM_Template(PRM_TOGGLE,   1, &names[16]),  // modify scale
    PRM_Template(PRM_TOGGLE,   1, &names[4]),   // preview mode
    PRM_Template(PRM_CALLBACK, 1, &names[9], &buttonDefault, 0, 0,
                 &SOP_GSPaintBrush::onClearPoints),
    PRM_Template()
};

enum { VAR_PT, VAR_NPT };

CH_LocalVariable
SOP_GSPaintBrush::myVariables[] = {
    { "PT",  VAR_PT,  0 },
    { "NPT", VAR_NPT, 0 },
    { 0, 0, 0 },
};

bool
SOP_GSPaintBrush::evalVariableValue(fpreal& val, int index, int thread)
{
    if (myCurrPoint >= 0)
    {
        switch (index)
        {
        case VAR_PT:  val = (fpreal)myCurrPoint;  return true;
        case VAR_NPT: val = (fpreal)myTotalPoints; return true;
        default: break;
        }
    }
    return SOP_Node::evalVariableValue(val, index, thread);
}

OP_Node*
SOP_GSPaintBrush::myConstructor(OP_Network* net, const char* name, OP_Operator* op)
{
    return new SOP_GSPaintBrush(net, name, op);
}

SOP_GSPaintBrush::SOP_GSPaintBrush(OP_Network* net, const char* name, OP_Operator* op)
    : SOP_Node(net, name, op)
{
    myCurrPoint = -1;
    myLastProcessedStrokeSize = 0;

    myLastDensity = -1.0f;
    myLastScale = -1.0f;
    myLastOpacity = -1.0f;
    myLastBrushRadius = -1.0f;
}

SOP_GSPaintBrush::~SOP_GSPaintBrush() {}

unsigned
SOP_GSPaintBrush::disableParms()
{
    fpreal t = CHgetEvalTime();
    int operation = OPERATION(t);
    bool isPaint = (operation == 1);

    // disable paint-only params when not in paint mode
    enableParm("paint_color", isPaint);
    enableParm("paint_alpha", isPaint);
    enableParm("color_source", isPaint);
    enableParm("paint_cd", isPaint);
    enableParm("paint_alpha_on", isPaint);
    enableParm("paint_scale", isPaint);

    // disable stamp-only params when in paint mode
    enableParm("density", !isPaint);
    enableParm("preview_mode", !isPaint);

    return 0;
}

// ── helper functions (unchanged from before) ─────────────────────────────────

static UT_QuaternionF
rotationBetween(UT_Vector3F from, UT_Vector3F to)
{
    from.normalize(); to.normalize();
    float dot = from.dot(to);
    if (dot >= 1.0f - 1e-6f) return UT_QuaternionF(0, 0, 0, 1);
    if (dot <= -1.0f + 1e-6f)
    {
        UT_Vector3F up(1, 0, 0);
        if (SYSabs(from.dot(up)) > 0.9f) up = UT_Vector3F(0, 1, 0);
        up = cross(from, up); up.normalize();
        UT_QuaternionF q; q.updateFromAngleAxis(M_PI, up); return q;
    }
    UT_Vector3F axis = cross(from, to);
    float angle = SYSacos(SYSclamp(dot, -1.f, 1.f));
    UT_QuaternionF q; q.updateFromAngleAxis(angle, axis); return q;
}

static UT_Vector3F
rotateVector(const UT_Vector3F& v, const UT_QuaternionF& q)
{
    UT_QuaternionF qv(v.x(), v.y(), v.z(), 0.f);
    UT_QuaternionF qInv = q; qInv.invert();
    UT_QuaternionF result = q * qv * qInv;
    return UT_Vector3F(result.x(), result.y(), result.z());
}

static UT_QuaternionF
multiplyQuat(const UT_QuaternionF& q1, const UT_QuaternionF& q2)
{
    return q2 * q1;
}

static UT_Vector3F
catmullRom(const UT_Array<UT_Vector3F>& pts, const UT_Array<float>& arc, float t)
{
    int n = pts.size();
    if (t <= arc[0])   return pts[0];
    if (t >= arc[n - 1]) return pts[n - 1];
    int seg = 0;
    for (int i = 0; i < n - 1; i++)
        if (arc[i] <= t && t <= arc[i + 1]) { seg = i; break; }
    float seg_len = arc[seg + 1] - arc[seg];
    float u = seg_len > 0.f ? (t - arc[seg]) / seg_len : 0.f;
    UT_Vector3F p0 = pts[SYSmax(seg - 1, 0)], p1 = pts[seg],
        p2 = pts[SYSmin(seg + 1, n - 1)], p3 = pts[SYSmin(seg + 2, n - 1)];
    return 0.5f * (2.f * p1 + (-p0 + p2) * u + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * u * u +
        (-p0 + 3.f * p1 - 3.f * p2 + p3) * u * u * u);
}

struct SplinePoint { UT_Vector3F pos, norm; };

static UT_Array<SplinePoint>
buildSpline(const UT_Array<UT_Vector3F>& pts,
    const UT_Array<UT_Vector3F>& norms, float density)
{
    UT_Array<SplinePoint> result;
    int n = pts.size();
    if (n < 2) return result;
    UT_Array<float> arc; arc.append(0.f);
    for (int i = 1; i < n; i++)
        arc.append(arc[i - 1] + (pts[i] - pts[i - 1]).length());
    float total = arc[n - 1];
    if (total < 0.001f) return result;
    int n_samples = SYSmax(2, (int)(total * density));
    for (int i = 0; i < n_samples; i++)
    {
        float t = total * (float)i / (float)(n_samples - 1);
        UT_Vector3F pos = catmullRom(pts, arc, t);
        int nearest = 0; float best = SYSabs(arc[0] - t);
        for (int j = 1; j < n; j++)
        {
            float d = SYSabs(arc[j] - t); if (d < best) { best = d; nearest = j; }
        }
        SplinePoint sp; sp.pos = pos; sp.norm = norms[nearest];
        result.append(sp);
    }
    return result;
}

// ── onClearPoints ─────────────────────────────────────────────────────────────

int
SOP_GSPaintBrush::onClearPoints(void* data, int index, fpreal t,
    const PRM_Template*)
{
    SOP_GSPaintBrush* sop = (SOP_GSPaintBrush*)data;
    OP_Network* net = sop->getParent();
    OP_Node* strokeNode = net->findNode("stroke_points");
    if (!strokeNode) return 1;
    strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL,
        "point_positions", 0, t);
    strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL,
        "point_normals", 0, t);
    strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL,
        "stroke_lengths", 0, t);
    OP_Context cookContext(t);
    strokeNode->cook(cookContext);
    return 1;
}

OP_ERROR
SOP_GSPaintBrush::cookMySop(OP_Context& context)
{
    fpreal now = context.getTime();
    if (lockInputs(context) >= UT_ERROR_ABORT)
        return error();

    gdp->clearAndDestroy();

    int operation = OPERATION(now);

    float density = DENSITY(now);
    float scale = SCALE(now);
    float opacity = OPACITY(now);
    float radius = BRUSH_RADIUS(now);

    // Check if parameter change.
    bool parmChanged =
        density != myLastDensity ||
        scale != myLastScale ||
        opacity != myLastOpacity ||
        radius != myLastBrushRadius;

    // ── get base scene ────────────────────────────────────────────────────
    const GU_Detail* baseGdp = inputGeo(2, context);

    // ── get current stroke points ─────────────────────────────────────────
    const GU_Detail* targetGdp = inputGeo(1, context);

    // ── process NEW stroke points since last cook ─────────────────────────
    if (targetGdp && (targetGdp->getNumPoints() > myLastProcessedStrokeSize || parmChanged))
    {
        float brushRadius = BRUSH_RADIUS(now);
        float radius2 = brushRadius * brushRadius;

        // collect only NEW stroke positions
        UT_Array<UT_Vector3F> newStrokePositions;
        int idx = 0;
        GA_Offset ptoff;
        bool useAllStrokePoints = parmChanged;

        GA_FOR_ALL_PTOFF(targetGdp, ptoff)
        {
            if (useAllStrokePoints || idx >= myLastProcessedStrokeSize)
                newStrokePositions.append(
                    UT_Vector3F(targetGdp->getPos3(ptoff))
                );
            idx++;
        }
        myLastProcessedStrokeSize = targetGdp->getNumPoints();

        if (operation == 0 && baseGdp) // stamp mode
        {
            const GU_Detail* splatsGdp = inputGeo(0, context);
            if (splatsGdp && splatsGdp->getNumPoints() > 0)
            {
                // read brush stamp
                struct SplatStamp {
                    UT_Vector3F localOffset;
                    float       alpha;
                    UT_Vector3F cd;
                    UT_Vector4F orient;
                    UT_Vector3F scale;
                };

                float brushScale = SCALE(now);
                float brushOpacity = OPACITY(now);

                GA_ROHandleV3 src_cd(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
                GA_ROHandleF  src_alpha(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "alpha", 1));
                GA_ROHandleV4 src_orient(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "orient", 4));
                GA_ROHandleV3 src_scale(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "scale", 3));

                // compute centroid
                UT_Vector3F centroid(0, 0, 0);
                int npts = splatsGdp->getNumPoints();
                {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(splatsGdp, ptoff)
                        centroid += UT_Vector3F(splatsGdp->getPos3(ptoff));
                }
                centroid /= (float)npts;

                // build brush pattern
                UT_Array<SplatStamp> brushPattern;
                {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(splatsGdp, ptoff)
                    {
                        SplatStamp s;
                        s.localOffset = UT_Vector3F(splatsGdp->getPos3(ptoff)) - centroid;
                        s.alpha = src_alpha.isValid()
                            ? SYSclamp(src_alpha.get(ptoff) * brushOpacity, 0.f, 1.f) : 1.f;
                        if (src_scale.isValid())
                        {
                            UT_Vector3F sc = src_scale.get(ptoff);
                            float scaleMul = SYSmax(brushScale, 1e-6f);
                            sc.x() *= scaleMul;
                            sc.y() *= scaleMul;
                            sc.z() *= scaleMul;
                            s.scale = sc;
                        }
                        else s.scale = UT_Vector3F(0, 0, 0);
                        s.orient = src_orient.isValid()
                            ? UT_Vector4F(src_orient.get(ptoff)) : UT_Vector4F(0, 0, 0, 1);
                        s.cd = src_cd.isValid()
                            ? UT_Vector3F(src_cd.get(ptoff)) : UT_Vector3F(1, 1, 1);
                        brushPattern.append(s);
                    }
                }

                // build spline from ALL stroke points
                GA_ROHandleV3 tgt_normal(targetGdp->findFloatTuple(GA_ATTRIB_POINT, "N", 3));
                GA_ROHandleI  tgt_strokeId(targetGdp->findIntTuple(GA_ATTRIB_POINT, "piece", 1));

                std::map<int, UT_Array<UT_Vector3F>> strokePts, strokeNorms;
                const UT_Vector3F stampUpDir(0, 1, 0);
                {
                    GA_Offset ptoff;
                    GA_FOR_ALL_PTOFF(targetGdp, ptoff)
                    {
                        int piece = tgt_strokeId.isValid() ? tgt_strokeId.get(ptoff) : 0;
                        UT_Vector3F pos = targetGdp->getPos3(ptoff);
                        UT_Vector3F norm = stampUpDir;
                        if (tgt_normal.isValid())
                        {
                            UT_Vector3F n = tgt_normal.get(ptoff);
                            if (n.length() > 1e-6f) { n.normalize(); norm = n; }
                        }
                        strokePts[piece].append(pos);
                        strokeNorms[piece].append(norm);
                    }
                }

                // rebuild stamp sites from scratch and repopulate myStampedGaussians
                myStampedGaussians.clear();
                float density = DENSITY(now);

                for (auto& kv : strokePts)
                {
                    int piece = kv.first;
                    auto spline = buildSpline(strokePts[piece], strokeNorms[piece], density);

                    for (const SplinePoint& sp : spline)
                    {
                        UT_Vector3F targetNormal = sp.norm;
                        if (targetNormal.length() < 1e-6f) targetNormal = stampUpDir;
                        targetNormal.normalize();
                        UT_QuaternionF stampRot = rotationBetween(stampUpDir, targetNormal);

                        for (const SplatStamp& s : brushPattern)
                        {
                            GaussianAttribs a;
                            a.pos = sp.pos + rotateVector(s.localOffset, stampRot);
                            a.cd = s.cd;
                            a.alpha = s.alpha;
                            a.scale = s.scale;

                            UT_QuaternionF splatOrient(s.orient.x(), s.orient.y(),
                                s.orient.z(), s.orient.w());
                            UT_QuaternionF rotated = multiplyQuat(splatOrient, stampRot);
                            a.orient = UT_Vector4F(rotated.x(), rotated.y(),
                                rotated.z(), rotated.w());
                            myStampedGaussians.append(a);
                        }
                    }
                }
            }
        }
        else if (operation == 1 && baseGdp) // paint mode
        {
            float paintAlpha = PAINTALPHA(now);
            int   colorSource = COLORSOURCE(now);
            bool  modifyCd = PAINTCD(now);
            bool  modifyAlpha = PAINTALPHA2(now);
            bool  modifyScale = PAINTSCALE(now);
            UT_Vector3F paintColor(PAINTCOLOR(now));

            // for each base Gaussian near new stroke points, update myPaintedAttribs
            GA_Offset bptoff;
            GA_ROHandleV3 src_cd(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
            GA_ROHandleF  src_alpha(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "alpha", 1));
            GA_ROHandleV3 src_scale(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "scale", 3));
            GA_ROHandleV4 src_orient(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "orient", 4));

            GA_FOR_ALL_PTOFF(baseGdp, bptoff)
            {
                UT_Vector3F pos = UT_Vector3F(baseGdp->getPos3(bptoff));
                float minDist2 = 1e10f;
                for (const UT_Vector3F& sp : newStrokePositions)
                {
                    float d2 = (pos - sp).length2();
                    if (d2 < minDist2) minDist2 = d2;
                }
                if (minDist2 > radius2) continue;

                float dist = SYSsqrt(minDist2);
                float falloff = 1.0f - (dist / brushRadius);
                float blend = paintAlpha * falloff;
                float inv = 1.0f - blend;

                GA_Index ptnum = baseGdp->pointIndex(bptoff);

                // check if already painted
                auto it = myPaintedAttribs.find(ptnum);
                if (it == myPaintedAttribs.end())
                {
                    // first time painting this point — initialize from base
                    GaussianAttribs newAttribs;
                    newAttribs.cd = src_cd.isValid() ? UT_Vector3F(src_cd.get(bptoff)) : UT_Vector3F(1, 1, 1);
                    newAttribs.alpha = src_alpha.isValid() ? src_alpha.get(bptoff) : 1.f;
                    newAttribs.scale = src_scale.isValid() ? UT_Vector3F(src_scale.get(bptoff)) : UT_Vector3F(0, 0, 0);
                    newAttribs.orient = src_orient.isValid() ? UT_Vector4F(src_orient.get(bptoff)) : UT_Vector4F(0, 0, 0, 1);
                    myPaintedAttribs[ptnum] = newAttribs;
                    it = myPaintedAttribs.find(ptnum);
                }

                GaussianAttribs& attribs = it->second;
                if (modifyCd)
                    attribs.cd = attribs.cd * inv + paintColor * blend;
                if (modifyAlpha)
                    attribs.alpha = attribs.alpha * inv + blend;
            }
        }
        else if (operation == 2 && baseGdp) // erase mode
        {
            float radius2 = brushRadius * brushRadius;

            // erase from base scene
            //GA_Offset bptoff;
            //GA_FOR_ALL_PTOFF(baseGdp, bptoff)
            //{
            //    UT_Vector3F pos = UT_Vector3F(baseGdp->getPos3(bptoff));
            //    for (const UT_Vector3F& sp : newStrokePositions)
            //    {
            //        if ((pos - sp).length2() <= radius2)
            //        {
            //            myErasedPoints.insert(baseGdp->pointIndex(bptoff));
            //            break;
            //        }
            //    }
            //}

            // also erase stamps within brush radius
            UT_Array<GaussianAttribs> survivingStamps;
            for (const GaussianAttribs& a : myStampedGaussians)
            {
                bool erased = false;
                for (const UT_Vector3F& sp : newStrokePositions)
                {
                    if ((a.pos - sp).length2() <= radius2)
                    {
                        erased = true;
                        break;
                    }
                }
                if (!erased) survivingStamps.append(a);
            }
            myStampedGaussians = survivingStamps;

            // also remove paint modifications within brush radius
            UT_Array<GA_Index> toRemove;
            for (auto& kv : myPaintedAttribs)
            {
                GA_Offset ptoff = baseGdp->pointOffset(kv.first);
                UT_Vector3F pos = UT_Vector3F(baseGdp->getPos3(ptoff));
                for (const UT_Vector3F& sp : newStrokePositions)
                {
                    if ((pos - sp).length2() <= radius2)
                    {
                        toRemove.append(kv.first);
                        break;
                    }
                }
            }
            for (GA_Index idx : toRemove)
                myPaintedAttribs.erase(idx);
                }
    }
    else if (targetGdp && targetGdp->getNumPoints() == 0)
    {
        // stroke was cleared externally — reset all accumulated state
        myStampedGaussians.clear();
        myPaintedAttribs.clear();
        myErasedPoints.clear();
        myLastProcessedStrokeSize = 0;
    }

    // ── build output ──────────────────────────────────────────────────────

    GA_RWHandleV3 out_cd(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
    GA_RWHandleF  out_alpha(gdp->addFloatTuple(GA_ATTRIB_POINT, "alpha", 1));
    GA_RWHandleV4 out_orient(gdp->addFloatTuple(GA_ATTRIB_POINT, "orient", 4));
    GA_RWHandleV3 out_scale(gdp->addFloatTuple(GA_ATTRIB_POINT, "scale", 3));

    // 1. output base scene with paint modifications and erases applied
    if (baseGdp)
    {
        GA_ROHandleV3 src_cd(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
        GA_ROHandleF  src_alpha(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "Alpha", 1));
        GA_ROHandleV4 src_orient(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "orient", 4));
        GA_ROHandleV3 src_scale(baseGdp->findFloatTuple(GA_ATTRIB_POINT, "scale", 3));

        GA_Offset ptoff;
        GA_FOR_ALL_PTOFF(baseGdp, ptoff)
        {
            GA_Index ptnum = baseGdp->pointIndex(ptoff);

            // skip erased points
            if (myErasedPoints.count(ptnum)) continue;

            GA_Offset newPt = gdp->appendPoint();
            gdp->setPos3(newPt, baseGdp->getPos3(ptoff));

            // check if this point has paint modifications
            auto it = myPaintedAttribs.find(ptnum);
            if (it != myPaintedAttribs.end())
            {
                const GaussianAttribs& a = it->second;
                if (out_cd.isValid())    out_cd.set(newPt, UT_Vector3(a.cd));
                if (out_alpha.isValid()) out_alpha.set(newPt, a.alpha);
                if (out_orient.isValid())
                    out_orient.set(newPt, UT_Vector4(a.orient));
                if (out_scale.isValid())
                    out_scale.set(newPt, UT_Vector3(a.scale));
            }
            else
            {
                // copy original attributes
                if (out_cd.isValid() && src_cd.isValid())
                    out_cd.set(newPt, src_cd.get(ptoff));
                if (out_alpha.isValid() && src_alpha.isValid())
                    out_alpha.set(newPt, src_alpha.get(ptoff));
                if (out_orient.isValid() && src_orient.isValid())
                    out_orient.set(newPt, src_orient.get(ptoff));
                if (out_scale.isValid() && src_scale.isValid())
                    out_scale.set(newPt, src_scale.get(ptoff));
            }
        }
    }

    // 2. output accumulated stamps on top
    for (const GaussianAttribs& a : myStampedGaussians)
    {
        GA_Offset newPt = gdp->appendPoint();
        gdp->setPos3(newPt, UT_Vector3(a.pos));
        if (out_cd.isValid())     out_cd.set(newPt, UT_Vector3(a.cd));
        if (out_alpha.isValid())  out_alpha.set(newPt, a.alpha);
        if (out_orient.isValid()) out_orient.set(newPt, UT_Vector4(a.orient));
        if (out_scale.isValid())  out_scale.set(newPt, UT_Vector3(a.scale));
    }

    myLastDensity = density;
    myLastScale = scale;
    myLastOpacity = opacity;
    myLastBrushRadius = radius;

    unlockInputs();
    return error();
}