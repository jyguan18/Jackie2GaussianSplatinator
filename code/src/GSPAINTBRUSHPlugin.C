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
    PRM_Template(PRM_TOGGLE, 1, &names[4]),
    PRM_Template(PRM_TOGGLE, 1, &names[5]),
    PRM_Template(PRM_XYZ_J,  3, &names[6]),  // origin
    PRM_Template(PRM_XYZ_J,  3, &names[7]),  // direction
    PRM_Template(PRM_ORD,    1, &names[8], &eventDefault, &eventMenu), // event
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
}

SOP_GSPaintBrush::~SOP_GSPaintBrush() {}

unsigned
SOP_GSPaintBrush::disableParms()
{
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

// ── cookMySop ────────────────────────────────────────────────────────────────

OP_ERROR
SOP_GSPaintBrush::cookMySop(OP_Context& context)
{
    fpreal now = context.getTime();

    if (lockInputs(context) >= UT_ERROR_ABORT)
        return error();

    gdp->clearAndDestroy();

    // ── read brush stamp from Input 0 ────────────────────────────────────────
    struct SplatStamp {
        UT_Vector3 localOffset;
        float      alpha;
        UT_Vector3 cd;
        UT_Vector4 orient;
        UT_Vector3 scale;
    };

    float brushScale = SCALE(now);
    float brushOpacity = OPACITY(now);

    const GU_Detail* splatsGdp = inputGeo(0, context);
    if (!splatsGdp)
    {
        addError(SOP_MESSAGE, "Missing input 0: Gaussian splat cloud.");
        unlockInputs(); return error();
    }

    GA_ROHandleV3 src_cdHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
    GA_ROHandleF  src_alphaHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "Alpha", 1));
    GA_ROHandleV4 src_orientHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "orient", 4));
    GA_ROHandleV3 src_scaleHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "scale", 3));

    UT_Vector3F stampCentroid(0, 0, 0);
    int numSplatPts = splatsGdp->getNumPoints();
    {
        GA_Offset ptoff; GA_FOR_ALL_PTOFF(splatsGdp, ptoff)
            stampCentroid += UT_Vector3F(splatsGdp->getPos3(ptoff));
    }
    stampCentroid /= (float)numSplatPts;

    UT_Array<SplatStamp> brushPattern;
    brushPattern.setCapacity(numSplatPts);
    {
        GA_Offset ptoff;
        GA_FOR_ALL_PTOFF(splatsGdp, ptoff)
        {
            SplatStamp s;
            s.localOffset = UT_Vector3F(splatsGdp->getPos3(ptoff)) - stampCentroid;
            s.alpha = src_alphaHandle.isValid()
                ? SYSclamp(src_alphaHandle.get(ptoff) * brushOpacity, 0.f, 1.f) : 1.f;
            if (src_scaleHandle.isValid())
            {
                UT_Vector3F sc = src_scaleHandle.get(ptoff);
                float ld = SYSlog(SYSmax(brushScale, 1e-6f));
                sc.x() += ld; sc.y() += ld; sc.z() += ld; s.scale = sc;
            }
            else s.scale = UT_Vector3(0, 0, 0);
            s.orient = src_orientHandle.isValid()
                ? UT_Vector4F(src_orientHandle.get(ptoff))
                : UT_Vector4F(0, 0, 0, 1);
            s.cd = src_cdHandle.isValid()
                ? src_cdHandle.get(ptoff) : UT_Vector3(1, 1, 1);
            brushPattern.append(s);
        }
    }

    // ── read stroke points from Input 1 ──────────────────────────────────────
    const GU_Detail* targetGdp = inputGeo(1, context);
    if (!targetGdp)
    {
        addError(SOP_MESSAGE, "Missing input 1: stamp target points.");
        unlockInputs(); return error();
    }

    GA_ROHandleV3 tgt_normal(targetGdp->findFloatTuple(GA_ATTRIB_POINT, "N", 3));
    GA_ROHandleI  tgt_strokeId(targetGdp->findIntTuple(GA_ATTRIB_POINT, "piece", 1));

    float density = DENSITY(now);
    const UT_Vector3F stampUpDir(0, 1, 0);

    std::map<int, UT_Array<UT_Vector3F>> strokePts, strokeNorms;
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

    UT_Array<SplinePoint> allStampSites;
    UT_Array<int> strokeBoundaries; strokeBoundaries.append(0);
    for (auto& kv : strokePts)
    {
        int piece = kv.first;
        auto spline = buildSpline(strokePts[piece], strokeNorms[piece], density);
        for (auto& sp : spline) allStampSites.append(sp);
        strokeBoundaries.append(allStampSites.size());
    }

    // ── preview mode ─────────────────────────────────────────────────────────
    if (PREVIEWMODE(now))
    {
        GA_RWHandleV3 preview_cd(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
        for (int s = 0; s < strokeBoundaries.size() - 1; s++)
        {
            int start = strokeBoundaries[s], end = strokeBoundaries[s + 1];
            UT_Array<GA_Offset> ptOffsets;
            for (int i = start; i < end; i++)
            {
                GA_Offset newPt = gdp->appendPoint();
                gdp->setPos3(newPt, UT_Vector3(allStampSites[i].pos));
                if (preview_cd.isValid())
                    preview_cd.set(newPt, UT_Vector3(1.f, 0.5f, 0.f));
                ptOffsets.append(newPt);
            }
            GU_PrimPoly* poly = GU_PrimPoly::build(gdp, 0, GU_POLY_OPEN, 0);
            for (GA_Offset off : ptOffsets) poly->appendVertex(off);
        }
        const GU_Detail* baseGdp = inputGeo(2, context);
        if (baseGdp) gdp->merge(*baseGdp);
        unlockInputs(); return error();
    }

    // ── stamp instancing ─────────────────────────────────────────────────────
    GA_RWHandleV3 out_cdHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
    GA_RWHandleF  out_alphaHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "Alpha", 1));
    GA_RWHandleV4 out_orientHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "orient", 4));
    GA_RWHandleV3 out_scaleHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "scale", 3));

    for (const SplinePoint& sp : allStampSites)
    {
        UT_Vector3F targetNormal = sp.norm;
        if (targetNormal.length() < 1e-6f) targetNormal = stampUpDir;
        targetNormal.normalize();
        UT_QuaternionF stampRot = rotationBetween(stampUpDir, targetNormal);

        for (const SplatStamp& s : brushPattern)
        {
            UT_Vector3F worldPos = sp.pos + rotateVector(s.localOffset, stampRot);
            GA_Offset newPt = gdp->appendPoint();
            gdp->setPos3(newPt, UT_Vector3(worldPos));
            out_alphaHandle.set(newPt, s.alpha);
            out_cdHandle.set(newPt, UT_Vector3(s.cd));
            out_scaleHandle.set(newPt, UT_Vector3(s.scale));
            UT_QuaternionF splatOrient(s.orient.x(), s.orient.y(),
                s.orient.z(), s.orient.w());
            UT_QuaternionF rotatedOrient = multiplyQuat(splatOrient, stampRot);
            out_orientHandle.set(newPt, UT_Vector4(
                rotatedOrient.x(), rotatedOrient.y(),
                rotatedOrient.z(), rotatedOrient.w()));
        }
    }

    // ── merge base scene ─────────────────────────────────────────────────────
    const GU_Detail* baseGdp = inputGeo(2, context);
    if (baseGdp) gdp->merge(*baseGdp);

    unlockInputs();
    return error();
}