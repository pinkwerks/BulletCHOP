#include "BulletCHOP.h"

#include <stdio.h>
#include <string.h>

#define _USE_MATH_DEFINES
#include <math.h>

// RADIAN = 180.0 / M_PI
#define RADIAN 57.295779513082320876798154814105
#define PI_OVER_180 0.01745329251994329576923690768489
#define DEGREES_TO_RADIANS(angle) ((angle) / (RADIAN))
#define RADIANS_TO_DEGREES(radians) ((radians) * (RADIAN))

// http://bulletphysics.org/Bullet/BulletFull/classbtRigidBody.html

// These functions are basic C function, which the DLL loader can find
// much easier than finding a C++ Class.
// The DLLEXPORT prefix is needed so the compile exports these functions from the .dll
// you are creating
extern "C"
{

	DLLEXPORT
		int
		GetCHOPAPIVersion(void)
	{
		// Always return CHOP_CPLUSPLUS_API_VERSION in this function.
		return CHOP_CPLUSPLUS_API_VERSION;
	}

	DLLEXPORT
		CHOP_CPlusPlusBase*
		CreateCHOPInstance(const CHOP_NodeInfo *info)
	{
		// Return a new instance of your class every time this is called.
		// It will be called once per CHOP that is using the .dll
		return new BulletCHOP(info);
	}

	DLLEXPORT
		void
		DestroyCHOPInstance(CHOP_CPlusPlusBase *instance)
	{
		// Delete the instance here, this will be called when
		// Touch is shutting down, when the CHOP using that instance is deleted, or
		// if the CHOP loads a different DLL
		delete (BulletCHOP*)instance;
	}

};


BulletCHOP::BulletCHOP(const CHOP_NodeInfo *info) : myNodeInfo(info)
{
	myExecuteCount = 0;
	pos = 0;
	ms = 0;

	worldSetup();

}

BulletCHOP::~BulletCHOP()
{

	worldDestroy();

}


void BulletCHOP::worldSetup()
{
	collisionConfiguration = new btDefaultCollisionConfiguration;
	dispatcher = new btCollisionDispatcher(collisionConfiguration);
	solver = new btSequentialImpulseConstraintSolver;

	broadphase = new btDbvtBroadphase();

	dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);

	dynamicsWorld->setGravity(btVector3(0,-10,0));

	btContactSolverInfo& info = dynamicsWorld->getSolverInfo();
	info.m_splitImpulse = 1;
	// @todo make this user defined on the chop
	info.m_splitImpulsePenetrationThreshold = (btScalar)-0.02;
}

void BulletCHOP::worldDestroy()
{
	delete dynamicsWorld;
	delete solver;
	delete broadphase;
	delete dispatcher;
	delete collisionConfiguration;
}

void BulletCHOP::getGeneralInfo(CHOP_GeneralInfo *ginfo)
{
	// This will cause the node to cook every frame
	ginfo->cookEveryFrameIfAsked = true;
	ginfo->timeslice = false;
	ginfo->inputMatchIndex = 0;
}

bool BulletCHOP::getOutputInfo(CHOP_OutputInfo *info)
{
	// If there is an input connected, we are going to match it's channel names etc
	// otherwise we'll specify our own.
	if (info->inputArrays->numCHOPInputs > 0)
	{
		return false;
	}
	else
	{
		info->numChannels = 1;

		// Since we are outputting a timeslice, the system will dictate
		// the length and startIndex of the CHOP data
		info->length = 1;
		info->startIndex = 0;

		// For illustration we are going to output 120hz data
		info->sampleRate = 60;
		return true;
	}
}

const char* BulletCHOP::getChannelName(int index, void* reserved)
{
	return "no_input";
}


void BulletCHOP::addBody(btVector3 pos, btVector3 rot, btVector3 scale, btScalar mass)
{
	btCollisionShape* colShape = new btBoxShape(0.5*scale);

	collisionShapes.push_back(colShape);

	/// Create Dynamic Objects
	btTransform startTransform;
	startTransform.setIdentity();

	//rigidbody is dynamic if and only if mass is non zero, otherwise static
	bool isDynamic = (mass != 0.f);

	btVector3 localInertia(0,0,0);
	if (isDynamic)
		colShape->calculateLocalInertia(mass,localInertia);

	btMatrix3x3 rotMat;
	rotMat.setEulerZYX(rot.x(), rot.y(), rot.z());

	startTransform.setOrigin(pos);
	startTransform.setBasis(rotMat);

	//using motionstate is recommended, it provides interpolation capabilities, and only synchronizes 'active' objects
	btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);
	btRigidBody* body = new btRigidBody(rbInfo);

	if(isDynamic == 0)
	{
		body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
		body->setActivationState(DISABLE_DEACTIVATION);
	}

	dynamicsWorld->addRigidBody(body);
}


void BulletCHOP::addPlane(btVector3 pos, btVector3 rot){

	btCollisionShape* groundShape = new btBoxShape(btVector3(btScalar(50.),btScalar(50.),btScalar(50.)));
	//btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0,1,0),0);

	collisionShapes.push_back(groundShape);

	btTransform groundTransform;
	groundTransform.setIdentity();
	//groundTransform.setOrigin(pos);
	groundTransform.setOrigin(btVector3(0,-50,0));


	btScalar mass(0.);

	//rigidbody is dynamic if and only if mass is non zero, otherwise static
	bool isDynamic = (mass != 0.f);

	btVector3 localInertia(0,0,0);
	if (isDynamic)
		groundShape->calculateLocalInertia(mass,localInertia);

	//using motionstate is recommended, it provides interpolation capabilities, and only synchronizes 'active' objects
	btDefaultMotionState* myMotionState = new btDefaultMotionState(groundTransform);
	btRigidBody::btRigidBodyConstructionInfo rbInfo(mass,myMotionState,groundShape,localInertia);
	btRigidBody* body = new btRigidBody(rbInfo);

	//add the body to the dynamics world
	dynamicsWorld->addRigidBody(body);


}

void BulletCHOP::removeBodies(){
	int i;

	for (i=dynamicsWorld->getNumCollisionObjects()-1; i>=0; i--)
	{
		btCollisionObject* obj = dynamicsWorld->getCollisionObjectArray()[i];
		btRigidBody* body = btRigidBody::upcast(obj);
		if (body && body->getMotionState())
		{
			delete body->getMotionState();
		}
		dynamicsWorld->removeCollisionObject( obj );
		delete obj;
	}

	// delete collision shapes
	for (int j=0;j<collisionShapes.size();j++)
	{
		btCollisionShape* shape = collisionShapes[j];
		delete shape;
	}
	collisionShapes.clear();

}

void BulletCHOP::execute(const CHOP_Output* output,
						 const CHOP_InputArrays* inputs,
						 void* reserved)
{
	myExecuteCount++;

	dynamicsWorld->setGravity(btVector3(
		inputs->floatInputs[1].values[0],
		inputs->floatInputs[1].values[1],
		inputs->floatInputs[1].values[2]));

	if (inputs->numCHOPInputs > 0)
	{
		if (inputs->floatInputs[0].values[0] == 1)
		{
			// reset simulation
			removeBodies();

			// build objects
			for (int i = 0; i < inputs->CHOPInputs[0].length; i++)
			{
				output->channels[0][i]  = inputs->CHOPInputs[0].channels[0][i]; // tx
				output->channels[1][i]  = inputs->CHOPInputs[0].channels[1][i]; // ty
				output->channels[2][i]  = inputs->CHOPInputs[0].channels[2][i]; // tz
				output->channels[3][i]  = inputs->CHOPInputs[0].channels[3][i]; // rx
				output->channels[4][i]  = inputs->CHOPInputs[0].channels[4][i]; // ry
				output->channels[5][i]  = inputs->CHOPInputs[0].channels[5][i]; // rz
				output->channels[6][i]  = inputs->CHOPInputs[0].channels[6][i]; // sx
				output->channels[7][i]  = inputs->CHOPInputs[0].channels[7][i]; // sy
				output->channels[8][i]  = inputs->CHOPInputs[0].channels[8][i]; // sz
				output->channels[9][i]  = inputs->CHOPInputs[0].channels[9][i]; // mass
				output->channels[10][i] = inputs->CHOPInputs[0].channels[10][i]; // speed

				btVector3 pos = btVector3(
					inputs->CHOPInputs[0].channels[0][i],
					inputs->CHOPInputs[0].channels[1][i],
					inputs->CHOPInputs[0].channels[2][i]);

				btVector3 rot = PI_OVER_180 * btVector3(
					inputs->CHOPInputs[0].channels[3][i],
					inputs->CHOPInputs[0].channels[4][i],
					inputs->CHOPInputs[0].channels[5][i]);

				btVector3 scale = btVector3(
					inputs->CHOPInputs[0].channels[6][i],
					inputs->CHOPInputs[0].channels[7][i],
					inputs->CHOPInputs[0].channels[8][i]);

				addBody(pos, rot, scale, inputs->CHOPInputs[0].channels[9][i]); // last arg is mass
			}

			//addPlane(btVector3(0,0,0),btVector3(0,0,0));

		} else {
			// http://www.bulletphysics.org/mediawiki-1.5.8/index.php/Stepping_The_World

			// should we be getting the ms value from touch?
			ms = getDeltaTimeMicroseconds() / 1000000.f;

			// step the simulation
			dynamicsWorld->stepSimulation(ms, (int)inputs->floatInputs[0].values[2], 1.f/inputs->floatInputs[0].values[1]);

			// output dynamic object transforms
			if (dynamicsWorld->getNumCollisionObjects() != 0)
			{
				for (int i = 0; i < inputs->CHOPInputs[0].length; i++)
				{
					btCollisionObject* obj = dynamicsWorld->getCollisionObjectArray()[i];
					btRigidBody* body = btRigidBody::upcast(obj);

					// if dynamic, get transform and output
					if (inputs->CHOPInputs[0].channels[9][i] != 0.f)
					{
						// translation
						btTransform trans;
						body->getMotionState()->getWorldTransform(trans);
						output->channels[0][i] = float(trans.getOrigin().getX());
						output->channels[1][i] = float(trans.getOrigin().getY());
						output->channels[2][i] = float(trans.getOrigin().getZ());

						// rotation
						btScalar rotX, rotY, rotZ;
						trans.getBasis().getEulerZYX(rotZ, rotY, rotX);
						output->channels[3][i] = float(rotX * RADIAN);
						output->channels[4][i] = float(rotY * RADIAN);
						output->channels[5][i] = float(rotZ * RADIAN);

						// scale
						output->channels[6][i] = inputs->CHOPInputs[0].channels[6][i];
						output->channels[7][i] = inputs->CHOPInputs[0].channels[7][i];
						output->channels[8][i] = inputs->CHOPInputs[0].channels[8][i];

						// output mass either way
						output->channels[9][i] = inputs->CHOPInputs[0].channels[9][i];

						// convert linear velocity to speed (the length of the velocity vector)
						btVector3 vel = body->getLinearVelocity();
						output->channels[10][i] = pow(vel.x(), 2) + pow(vel.y(), 2) + pow(vel.z(), 2);
					}
					else // update static object transforms
					{
						// pass through input
						output->channels[0][i] = inputs->CHOPInputs[0].channels[0][i];
						output->channels[1][i] = inputs->CHOPInputs[0].channels[1][i];
						output->channels[2][i] = inputs->CHOPInputs[0].channels[2][i];
						output->channels[3][i] = inputs->CHOPInputs[0].channels[3][i];
						output->channels[4][i] = inputs->CHOPInputs[0].channels[4][i];
						output->channels[5][i] = inputs->CHOPInputs[0].channels[5][i];
						output->channels[6][i] = inputs->CHOPInputs[0].channels[6][i];
						output->channels[7][i] = inputs->CHOPInputs[0].channels[7][i];
						output->channels[8][i] = inputs->CHOPInputs[0].channels[8][i];
						output->channels[9][i] = inputs->CHOPInputs[0].channels[9][i];
						output->channels[10][i] = inputs->CHOPInputs[0].channels[10][i];

						// set static object params for the solver
						btVector3 pos = btVector3(
							inputs->CHOPInputs[0].channels[0][i],
							inputs->CHOPInputs[0].channels[1][i],
							inputs->CHOPInputs[0].channels[2][i]);
						btVector3 rot = PI_OVER_180 * btVector3(
							inputs->CHOPInputs[0].channels[3][i],
							inputs->CHOPInputs[0].channels[4][i],
							inputs->CHOPInputs[0].channels[5][i]);

						btTransform trans;
						trans.setOrigin(pos);
						btMatrix3x3 rotMat;
						rotMat.setEulerZYX(rot.x(), rot.y(), rot.z());
						trans.setBasis(rotMat);
						body->getMotionState()->setWorldTransform(trans);
					}
				}
			}
		}
	}
	else // If no input is connected...
	{
		output->channels[0][0] = 0.;
	}
}

int
	BulletCHOP::getNumInfoCHOPChans()
{
	// We return the number of channel we want to output to any Info CHOP
	// connected to the CHOP. In this example we are just going to send one channel.
	return 2;
}

void
	BulletCHOP::getInfoCHOPChan(int index,
	CHOP_InfoCHOPChan *chan)
{
	// This function will be called once for each channel we said we'd want to return
	// In this example it'll only be called once.

	if (index == 0)
	{
		chan->name = "executeCount";
		chan->value = (float)myExecuteCount;
	}

	if (index == 1)
	{
		chan->name = "ms";
		chan->value = ms;
	}
}

bool		
	BulletCHOP::getInfoDATSize(CHOP_InfoDATSize *infoSize)
{
	infoSize->rows = 2;
	infoSize->cols = 2;
	// Setting this to false means we'll be assigning values to the table
	// one row at a time. True means we'll do it one column at a time.
	infoSize->byColumn = false;
	return true;
}

void
	BulletCHOP::getInfoDATEntries(int index,
	int nEntries,
	CHOP_InfoDATEntries *entries)
{
	if (index == 0)
	{
		// It's safe to use static buffers here because Touch will make it's own
		// copies of the strings immediately after this call returns
		// (so the buffers can be reuse for each column/row)
		static char tempBuffer1[4096];
		static char tempBuffer2[4096];
		// Set the value for the first column
		strcpy(tempBuffer1, "executeCount");
		entries->values[0] = tempBuffer1;

		// Set the value for the second column
		sprintf(tempBuffer2, "%d", myExecuteCount);
		entries->values[1] = tempBuffer2;

	}

	if (index == 1)
	{
		// It's safe to use static buffers here because Touch will make it's own
		// copies of the strings immediately after this call returns
		// (so the buffers can be reuse for each column/row)
		static char tempBuffer1[4096];
		static char tempBuffer2[4096];
		// Set the value for the first column
		strcpy(tempBuffer1, "ms");
		entries->values[0] = tempBuffer1;

		// Set the value for the second column
		sprintf(tempBuffer2, "%f", ms);
		entries->values[1] = tempBuffer2;


	}
}
