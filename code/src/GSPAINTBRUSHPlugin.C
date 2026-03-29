#include <UT/UT_DSOVersion.h>
//#include <RE/RE_EGLServer.h>


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
#include "GSPAINTBRUSHPlugin.h"
#include <map>
#include <PY/PY_Python.h>
using namespace HDK_Sample;

//
// Help is stored in a "wiki" style text file. 
//
// See the sample_install.sh file for an example.
//
// NOTE : Follow this tutorial if you have any problems setting up your visual studio 2008 for Houdini 
//  http://www.apileofgrains.nl/setting-up-the-hdk-for-houdini-12-with-visual-studio-2008/


///
/// newSopOperator is the hook that Houdini grabs from this dll
/// and invokes to register the SOP.  In this case we add ourselves
/// to the specified operator table.
///
void
newSopOperator(OP_OperatorTable *table)
{
    table->addOperator(
	    new OP_Operator("CusGSPaintBrush",			// Internal name
			    "GSPaintBrush",			// UI name
			     SOP_GSPaintBrush::myConstructor,	// How to build the SOP
				 SOP_GSPaintBrush::myTemplateList,	// My parameters
			     2,				// Min # of sources // We test one source first, source is GS.
			     3,				// Max # of sources
				 SOP_GSPaintBrush::myVariables,	// Local variables
			     0)		// Not a generator, but a modifier node.
	    );
}

static PRM_Name names[] = {
	PRM_Name("scale",  "Scale"),
	PRM_Name("opacity",  "Opacity"),
	PRM_Name("density", "Density"),
	PRM_Name("preview_mode", "Preview Mode"),
	PRM_Name("paint_stroke", "Paint Stroke"),
	PRM_Name("clear_points", "Clear Points"),
};

static PRM_Default scaleDefault(1.0f);
static PRM_Default opacityDefault(1.0f);
static PRM_Default densityDefault(20.0f);
static PRM_Default buttonDefault(0);

static PRM_Range scaleRange(PRM_RANGE_UI, 0.0f, PRM_RANGE_UI, 10.0f);
static PRM_Range opacityRange(PRM_RANGE_UI, 0.0f, PRM_RANGE_UI, 1.0f);
static PRM_Range   densityRange(PRM_RANGE_UI, 1.0f, PRM_RANGE_UI, 100.0f);

PRM_Template
SOP_GSPaintBrush::myTemplateList[] =
{
	PRM_Template(PRM_FLT, 1, &names[0], &scaleDefault, nullptr, &scaleRange),
	PRM_Template(PRM_FLT, 1, &names[1], &opacityDefault, nullptr, &opacityRange),
	PRM_Template(PRM_FLT, 1, &names[2], &densityDefault, nullptr, &densityRange),
	PRM_Template(PRM_TOGGLE, 1, &names[3]),
	PRM_Template(PRM_CALLBACK, 1, &names[4], &buttonDefault, 0, 0, &SOP_GSPaintBrush::onPaintStroke),
	PRM_Template(PRM_CALLBACK, 1, &names[5], &buttonDefault, 0, 0, &SOP_GSPaintBrush::onClearPoints),

	PRM_Template()
};


// Here's how we define local variables for the SOP.
enum {
	VAR_PT,		// Point number of the star
	VAR_NPT		// Number of points in the star
};

CH_LocalVariable
SOP_GSPaintBrush::myVariables[] = {
    { "PT",	VAR_PT, 0 },		// The table provides a mapping
    { "NPT",	VAR_NPT, 0 },		// from text string to integer token
    { 0, 0, 0 },
};

bool
SOP_GSPaintBrush::evalVariableValue(fpreal &val, int index, int thread)
{
    // myCurrPoint will be negative when we're not cooking so only try to
    // handle the local variables when we have a valid myCurrPoint index.
    if (myCurrPoint >= 0)
    {
	// Note that "gdp" may be null here, so we do the safe thing
	// and cache values we are interested in.
	switch (index)
	{
	    case VAR_PT:
		val = (fpreal) myCurrPoint;
		return true;
	    case VAR_NPT:
		val = (fpreal) myTotalPoints;
		return true;
	    default:
		/* do nothing */;
	}
    }
    // Not one of our variables, must delegate to the base class.
    return SOP_Node::evalVariableValue(val, index, thread);
}

OP_Node *
SOP_GSPaintBrush::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_GSPaintBrush(net, name, op);
}

SOP_GSPaintBrush::SOP_GSPaintBrush(OP_Network *net, const char *name, OP_Operator *op)
	: SOP_Node(net, name, op)
{
    myCurrPoint = -1;	// To prevent garbage values from being returned
}

SOP_GSPaintBrush::~SOP_GSPaintBrush() {}

unsigned
SOP_GSPaintBrush::disableParms()
{
    return 0;
}

// Rotate instantiated stamp to normal of point.
// Param from: original orientation of stamp.
// Param to: normal of point to rotate to.
static UT_QuaternionF
rotationBetween(UT_Vector3F from, UT_Vector3F to)
{
	from.normalize();
	to.normalize();

	float dot = from.dot(to);

	// Aligned already.
	if (dot >= 1.0f - 1e-6f)
		return UT_QuaternionF(0.f, 0.f, 0.f, 1.f);

	// Opposite direction, rotate 180 degrees.
	if (dot <= -1.0f + 1e-6f)
	{
		UT_Vector3F upVec = UT_Vector3F(1, 0, 0);
		if (SYSabs(from.dot(upVec)) > 0.9f)
			upVec = UT_Vector3F(0, 1, 0);
		upVec = cross(from, upVec);
		upVec.normalize();
		UT_QuaternionF q;
		q.updateFromAngleAxis(M_PI, upVec);
		return q;
	}

	// Else.
	UT_Vector3F axis = cross(from, to);
	float angle = SYSacos(SYSclamp(dot, -1.f, 1.f));
	UT_QuaternionF q;
	q.updateFromAngleAxis(angle, axis);
	return q;
}

static UT_Vector3F
rotateVector(const UT_Vector3F& v, const UT_QuaternionF& q)
{
	UT_QuaternionF qv(v.x(), v.y(), v.z(), 0.f);
	UT_QuaternionF qInv = q;
	qInv.invert();
	UT_QuaternionF result = q * qv * qInv;
	return UT_Vector3F(result.x(), result.y(), result.z());
}

static UT_QuaternionF
multiplyQuat(const UT_QuaternionF& q1, const UT_QuaternionF& q2)
{
	return q2 * q1;
}

static UT_Vector3F
catmullRom(const UT_Array<UT_Vector3F>& pts,
	const UT_Array<float>& arc,
	float t)
{
	int n = pts.size();
	if (t <= arc[0])   return pts[0];
	if (t >= arc[n - 1]) return pts[n - 1];

	int seg = 0;
	for (int i = 0; i < n - 1; i++) {
		if (arc[i] <= t && t <= arc[i + 1]) { seg = i; break; }
	}

	float seg_len = arc[seg + 1] - arc[seg];
	float u = (seg_len > 0.f) ? (t - arc[seg]) / seg_len : 0.f;

	UT_Vector3F p0 = pts[SYSmax(seg - 1, 0)];
	UT_Vector3F p1 = pts[seg];
	UT_Vector3F p2 = pts[SYSmin(seg + 1, n - 1)];
	UT_Vector3F p3 = pts[SYSmin(seg + 2, n - 1)];

	return 0.5f * (
		2.f * p1
		+ (-p0 + p2) * u
		+ (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * u * u
		+ (-p0 + 3.f * p1 - 3.f * p2 + p3) * u * u * u
		);
}

struct SplinePoint {
	UT_Vector3F pos;
	UT_Vector3F norm;
};

static UT_Array<SplinePoint>
buildSpline(const UT_Array<UT_Vector3F>& pts,
	const UT_Array<UT_Vector3F>& norms,
	float density)
{
	UT_Array<SplinePoint> result;
	int n = pts.size();
	if (n < 2) return result;

	// arc length parameterisation
	UT_Array<float> arc;
	arc.append(0.f);
	for (int i = 1; i < n; i++) {
		arc.append(arc[i - 1] + (pts[i] - pts[i - 1]).length());
	}
	float total = arc[n - 1];
	if (total < 0.001f) return result;

	int n_samples = SYSmax(2, (int)(total * density));

	for (int i = 0; i < n_samples; i++) {
		float t = total * (float)i / (float)(n_samples - 1);
		UT_Vector3F pos = catmullRom(pts, arc, t);

		// nearest input point for normal
		int nearest = 0;
		float best = SYSabs(arc[0] - t);
		for (int j = 1; j < n; j++) {
			float d = SYSabs(arc[j] - t);
			if (d < best) { best = d; nearest = j; }
		}

		SplinePoint sp;
		sp.pos = pos;
		sp.norm = norms[nearest];
		result.append(sp);
	}
	return result;
}

int
SOP_GSPaintBrush::onPaintStroke(void* data, int index, fpreal t, const PRM_Template*)
{
	SOP_GSPaintBrush* sop = (SOP_GSPaintBrush*)data;

	// find stroke_points node — go up to parent network and search
	OP_Network* net = sop->getParent();
	OP_Node* strokeNode = net->findNode("stroke_points");
	if (!strokeNode)
	{
		UT_String msg;
		msg.sprintf("Could not find stroke_points node in network.");
		sop->addMessage(SOP_MESSAGE, msg);
		return 1;
	}

	UT_String plyNodePath("");
	OP_Node* plyInput = sop->getInput(2);
	if (plyInput)
	{
		plyInput->getFullPath(plyNodePath);
	}
	else
	{
		sop->addWarning(SOP_MESSAGE, "No PLY input connected to input 2 ï¿½ canvas raycast will be disabled.");
	}

	// clear existing points first
	strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL, "point_positions", 0, t);
	strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL, "point_normals", 0, t);
	strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL, "stroke_lengths", 0, t);
	OP_Context cookContext(t);
	strokeNode->cook(cookContext);

	UT_String pyCmd;
	pyCmd.sprintf(
		"import importlib.util\n"
		"import hou\n"
		"import os\n"
		"hou.session.gaussian_paint_canvas_path = '%s'\n"
		"try:\n"
		"    sv = hou.ui.paneTabOfType(hou.paneTabType.SceneViewer)\n"
		"    sv.setCurrentState('Select')\n"
		"except:\n"
		"    pass\n"
		"try:\n"
		"    hou.ui.unregisterViewerState('gaussian_paint')\n"
		"except:\n"
		"    pass\n"
		"state_file = os.path.join(hou.getenv('HOUDINI_USER_PREF_DIR'), 'viewer_states', 'gaussian_paint.py')\n"
		"spec = importlib.util.spec_from_file_location('gaussian_paint', state_file)\n"
		"mod  = importlib.util.module_from_spec(spec)\n"
		"spec.loader.exec_module(mod)\n"
		"hou.ui.registerViewerState(mod.createViewerStateTemplate())\n"
		"hou.ui.paneTabOfType(hou.paneTabType.SceneViewer).setCurrentState('gaussian_paint')\n",
		plyNodePath.c_str()
	);

	PYrunPythonStatementsAndExpectNoErrors(pyCmd.buffer());

	return 1;
}

int
SOP_GSPaintBrush::onClearPoints(void* data, int index, fpreal t, const PRM_Template*)
{
	SOP_GSPaintBrush* sop = (SOP_GSPaintBrush*)data;

	OP_Network* net = sop->getParent();
	OP_Node* strokeNode = net->findNode("stroke_points");
	if (!strokeNode)
		return 1;

	strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL, "point_positions", 0, t);
	strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL, "point_normals", 0, t);
	strokeNode->setString("", CH_StringMeaning::CH_STRING_LITERAL, "stroke_lengths", 0, t);
	OP_Context cookContext(t);
	strokeNode->cook(cookContext);

	return 1;
}

OP_ERROR
SOP_GSPaintBrush::cookMySop(OP_Context &context)
{
	fpreal now = context.getTime();

	if (lockInputs(context) >= UT_ERROR_ABORT)
		return error();

	gdp->clearAndDestroy();

	// Create stamp.
	struct SplatStamp
	{
		UT_Vector3 localOffset; // position relative to brushPos
		float alpha;
		UT_Vector3 cd;
		UT_Vector4 orient;
		UT_Vector3 scale;
	};

	float      brushScale = SCALE(now);
	float      brushOpacity = OPACITY(now);

	const GU_Detail* splatsGdp = inputGeo(0, context);
	if (!splatsGdp)
	{
		addError(SOP_MESSAGE, "Missing input 0: Gaussian splat cloud.");
		unlockInputs();
		return error();
	}

	// Colour of input GSplat. 3 part float vector.
	GA_ROHandleV3 src_cdHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
	// Alpha value of input GSplat. 1 value float.
	GA_ROHandleF  src_alphaHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "Alpha", 1));
	// Orientation of input GSplat. 4 part quaternion value.
	GA_ROHandleV4 src_orientHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "orient", 4));
	// Scale of input GSplat. 3 part float vector.
	GA_ROHandleV3 src_scaleHandle(splatsGdp->findFloatTuple(GA_ATTRIB_POINT, "scale", 3));

	if (!src_alphaHandle.isValid())
		addWarning(SOP_MESSAGE, "alpha attribute not found on input 0.");
	if (!src_scaleHandle.isValid())
		addWarning(SOP_MESSAGE, "scale attribute not found on input 0.");
	if (!src_orientHandle.isValid())
		addWarning(SOP_MESSAGE, "orient attribute not found on input 0.");

	UT_Vector3F stampCentroid(0.f, 0.f, 0.f);
	int numSplatPts = splatsGdp->getNumPoints();
	{
		GA_Offset ptoff;
		GA_FOR_ALL_PTOFF(splatsGdp, ptoff)
			stampCentroid += UT_Vector3F(splatsGdp->getPos3(ptoff));
		stampCentroid /= (float)numSplatPts;
	}

	// SplatStamp lives in GSPaintBrush.
	UT_Array<SplatStamp> brushPattern;
	brushPattern.setCapacity(numSplatPts);

	{
		GA_Offset ptoff;
		GA_FOR_ALL_PTOFF(splatsGdp, ptoff)
		{

			SplatStamp s;
			s.localOffset = UT_Vector3F(splatsGdp->getPos3(ptoff)) - stampCentroid;

			s.alpha = src_alphaHandle.isValid()
				? SYSclamp(src_alphaHandle.get(ptoff) * brushOpacity, 0.0f, 1.0f)
				: 1.0f;

			// Scale applied in log space.
			if (src_scaleHandle.isValid())
			{
				UT_Vector3F sc = UT_Vector3F(src_scaleHandle.get(ptoff));
				float logDelta = SYSlog(SYSmax(brushScale, 1e-6f));
				sc.x() += logDelta;
				sc.y() += logDelta;
				sc.z() += logDelta;
				s.scale = sc;
			}
			else
			{
				s.scale = UT_Vector3(0, 0, 0);
			}

			if (src_orientHandle.isValid())
			{
				UT_Vector4F ov = UT_Vector4F(src_orientHandle.get(ptoff));
				s.orient = ov;
			}
			else
			{
				s.orient = UT_Vector4F(0.f, 0.f, 0.f, 1.f); // identity
			}
;
			s.cd = src_cdHandle.isValid()
				? src_cdHandle.get(ptoff)
				: UT_Vector3(1, 1, 1);

			brushPattern.append(s);
		}
	}

	// Debug: report how many splats were captured in the brush
	{
		UT_String msg;
		msg.sprintf("Brush pattern captured %d splats.", (int)brushPattern.size());
		addMessage(SOP_MESSAGE, msg);
	}

	// For point group.
	const GU_Detail* targetGdp = inputGeo(1, context);
	if (!targetGdp)
	{
		addError(SOP_MESSAGE, "Missing input 1: stamp target points.");
		unlockInputs();
		return error();
	}

	// Normals of points.
	GA_ROHandleV3 tgt_normal(targetGdp->findFloatTuple(GA_ATTRIB_POINT, "N", 3));
	GA_ROHandleI  tgt_strokeId(targetGdp->findIntTuple(GA_ATTRIB_POINT, "piece", 1));

	float density = DENSITY(now);
	const UT_Vector3F stampUpDir(0.f, 1.f, 0.f);

	// group points by stroke piece id
	std::map<int, UT_Array<UT_Vector3F>> strokePts;
	std::map<int, UT_Array<UT_Vector3F>> strokeNorms;

	{
		GA_Offset ptoff;
		GA_FOR_ALL_PTOFF(targetGdp, ptoff)
		{
			int piece = tgt_strokeId.isValid() ? tgt_strokeId.get(ptoff) : 0;
			UT_Vector3F pos = UT_Vector3F(targetGdp->getPos3(ptoff));
			UT_Vector3F norm = stampUpDir;
			if (tgt_normal.isValid()) {
				UT_Vector3F n = UT_Vector3F(tgt_normal.get(ptoff));
				if (n.length() > 1e-6f) { n.normalize(); norm = n; }
			}
			strokePts[piece].append(pos);
			strokeNorms[piece].append(norm);
		}
	}

	// build spline for each stroke separately, keeping track of which points belong to which stroke
	UT_Array<SplinePoint> allStampSites;
	UT_Array<int> strokeBoundaries; // index where each new stroke starts
	strokeBoundaries.append(0);

	for (auto& kv : strokePts) {
		int piece = kv.first;
		UT_Array<SplinePoint> spline = buildSpline(
			strokePts[piece], strokeNorms[piece], density
		);
		for (auto& sp : spline)
			allStampSites.append(sp);
		strokeBoundaries.append(allStampSites.size()); // mark end of this stroke
	}

	int previewMode = PREVIEWMODE(now);

	if (previewMode)
	{
		GA_RWHandleV3 preview_cd(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));

		// build a separate poly per stroke
		for (int s = 0; s < strokeBoundaries.size() - 1; s++)
		{
			int start = strokeBoundaries[s];
			int end = strokeBoundaries[s + 1];

			UT_Array<GA_Offset> ptOffsets;
			for (int i = start; i < end; i++)
			{
				GA_Offset newPt = gdp->appendPoint();
				gdp->setPos3(newPt, UT_Vector3(allStampSites[i].pos));
				if (preview_cd.isValid())
					preview_cd.set(newPt, UT_Vector3(1.0f, 0.5f, 0.0f));
				ptOffsets.append(newPt);
			}

			GU_PrimPoly* poly = GU_PrimPoly::build(gdp, 0, GU_POLY_OPEN, 0);
			for (GA_Offset off : ptOffsets)
				poly->appendVertex(off);
		}

		// merge base scene in preview mode too so it stays visible
		const GU_Detail* baseGdp = inputGeo(2, context);
		if (baseGdp)
		{
			gdp->merge(*baseGdp);
		}

		unlockInputs();
		return error();
	}


	GA_RWHandleV3 out_cdHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
	GA_RWHandleF  out_alphaHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "Alpha", 1));
	GA_RWHandleV4 out_orientHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "orient", 4));
	GA_RWHandleV3 out_scaleHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "scale", 3));

	// Stamping logic.
	{
		for (const SplinePoint& sp : allStampSites)
		{
			UT_Vector3F targetNormal = sp.norm;
			if (targetNormal.length() < 1e-6f) targetNormal = stampUpDir;
			targetNormal.normalize();

			UT_QuaternionF stampRot = rotationBetween(stampUpDir, targetNormal);

			for (const SplatStamp& s : brushPattern)
			{
				UT_Vector3F rotatedOffset = rotateVector(s.localOffset, stampRot);
				UT_Vector3F worldPos = sp.pos + rotatedOffset;

				GA_Offset newPt = gdp->appendPoint();
				gdp->setPos3(newPt, UT_Vector3(worldPos));

				out_alphaHandle.set(newPt, s.alpha);
				out_cdHandle.set(newPt, UT_Vector3(s.cd));
				out_scaleHandle.set(newPt, UT_Vector3(s.scale));

				UT_QuaternionF splatOrient(
					s.orient.x(), s.orient.y(), s.orient.z(), s.orient.w());
				UT_QuaternionF rotatedOrient = multiplyQuat(splatOrient, stampRot);
				out_orientHandle.set(newPt, UT_Vector4(
					rotatedOrient.x(), rotatedOrient.y(),
					rotatedOrient.z(), rotatedOrient.w()));
			}
		}
	}

	// merge in base Gaussian scene from input 2 if provided
	const GU_Detail* baseGdp = inputGeo(2, context);
	if (baseGdp)
	{
		gdp->merge(*baseGdp);
	}

	unlockInputs();
	return error();
}

