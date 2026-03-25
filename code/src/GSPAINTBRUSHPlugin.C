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
			     2,				// Max # of sources
				 SOP_GSPaintBrush::myVariables,	// Local variables
			     0)		// Not a generator, but a modifier node.
	    );
}

static PRM_Name names[] = {
	PRM_Name("scale",  "Scale"),
	PRM_Name("opacity",  "Opacity"),
};

static PRM_Default scaleDefault(1.0f);
static PRM_Default opacityDefault(1.0f);

static PRM_Range scaleRange(PRM_RANGE_UI, 0.0f, PRM_RANGE_UI, 10.0f);
static PRM_Range opacityRange(PRM_RANGE_UI, 0.0f, PRM_RANGE_UI, 1.0f);

PRM_Template
SOP_GSPaintBrush::myTemplateList[] =
{
	PRM_Template(PRM_FLT, 1, &names[0], &scaleDefault, nullptr, &scaleRange),
	PRM_Template(PRM_FLT, 1, &names[1], &opacityDefault, nullptr, &opacityRange),

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
	// Sandwich product: q * v * q^-1
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

OP_ERROR
SOP_GSPaintBrush::cookMySop(OP_Context &context)
{
	fpreal now = context.getTime();

	if (lockInputs(context) >= UT_ERROR_ABORT)
		return error();

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
	if (!tgt_normal.isValid())
		addWarning(SOP_MESSAGE, "No N attribute on input 1 — using default up vector (0,1,0).");

	const UT_Vector3F stampUpDir(0.f, 1.f, 0.f);


	gdp->clearAndDestroy();

	GA_RWHandleV3 out_cdHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "Cd", 3));
	GA_RWHandleF  out_alphaHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "Alpha", 1));
	GA_RWHandleV4 out_orientHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "orient", 4));
	GA_RWHandleV3 out_scaleHandle(gdp->addFloatTuple(GA_ATTRIB_POINT, "scale", 3));

	// Stamping logic.
	{
		GA_Offset tptoff;
		GA_FOR_ALL_PTOFF(targetGdp, tptoff)
		{
			UT_Vector3 targetPos = targetGdp->getPos3(tptoff);

			UT_Vector3F targetNormal = stampUpDir;
			if (tgt_normal.isValid())
			{
				UT_Vector3F n = UT_Vector3F(tgt_normal.get(tptoff));
				if (n.length() > 1e-6f)
				{
					n.normalize();
					targetNormal = n;
				}
			}

			UT_QuaternionF stampRot = rotationBetween(stampUpDir, targetNormal);

			// Stamp every splat in the brush pattern at this target
			for (const SplatStamp& s : brushPattern)
			{
				// Rotate local offset then translate to target position
				UT_Vector3F rotatedOffset = rotateVector(s.localOffset, stampRot);
				UT_Vector3F worldPos = targetPos + rotatedOffset;

				GA_Offset newPt = gdp->appendPoint();
				gdp->setPos3(newPt, UT_Vector3(worldPos));

				out_alphaHandle.set(newPt, s.alpha);
				out_cdHandle.set(newPt, UT_Vector3(s.cd));
				out_scaleHandle.set(newPt, UT_Vector3(s.scale));

				// Rotate the splat's own orient quaternion by stampRot
				UT_QuaternionF splatOrient(
					s.orient.x(), s.orient.y(), s.orient.z(), s.orient.w());
				UT_QuaternionF rotatedOrient = multiplyQuat(splatOrient, stampRot);
				out_orientHandle.set(newPt, UT_Vector4(
					rotatedOrient.x(), rotatedOrient.y(),
					rotatedOrient.z(), rotatedOrient.w()));
			}
		}
	}

	unlockInputs();
	return error();
}

