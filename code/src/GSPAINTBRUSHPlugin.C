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
			     0,				// Min # of sources
			     0,				// Max # of sources
				 SOP_GSPaintBrush::myVariables,	// Local variables
			     OP_FLAG_GENERATOR)		// Flag it as generator
	    );
}

static PRM_Name names[] = {
	PRM_Name("position",     "Position"),
	PRM_Name("radius",   "Radius"),
	PRM_Name("scale",  "Scale"),
	PRM_Name("opacity",  "Opacity"),
};

static PRM_Default positionDefaults[] =
{
	PRM_Default(0.0f),
	PRM_Default(0.0f),
	PRM_Default(0.0f)
};

static PRM_Default radiusDefault(1.0f);
static PRM_Default scaleDefault(1.0f);
static PRM_Default opacityDefault(1.0f);

PRM_Template
SOP_GSPaintBrush::myTemplateList[] =
{
	// vec3 parameter
	PRM_Template(PRM_FLT, 3, &names[0], positionDefaults),

	// float parameters
	PRM_Template(PRM_FLT, 1, &names[1], &radiusDefault),
	PRM_Template(PRM_FLT, 1, &names[2], &scaleDefault),
	PRM_Template(PRM_FLT, 1, &names[3], &opacityDefault),

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

OP_ERROR
SOP_GSPaintBrush::cookMySop(OP_Context &context)
{
	fpreal now = context.getTime();

	UT_Vector3 pos = POSITION(now);

	float radius = RADIUS(now);
	float scale = SCALE(now);
	float opacity = OPACITY(now);

	return error();
}

