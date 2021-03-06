/* Copyright (c) <2003-2016> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/

#include "toolbox_stdafx.h"
#include "SkyBox.h"
#include "DemoMesh.h"
#include "DemoCamera.h"
#include "PhysicsUtils.h"
#include "TargaToOpenGl.h"
#include "DemoEntityManager.h"
#include "DebugDisplay.h"
#include "HeightFieldPrimitive.h"


#define SERVO_VEHICLE_CAMERA_EYEPOINT			1.5f
#define SERVO_VEHICLE_CAMERA_HIGH_ABOVE_HEAD	2.0f
#define SERVO_VEHICLE_CAMERA_DISTANCE			8.0f

struct SERVO_VEHICLE_DEFINITION
{
	enum SHAPES_ID
	{
		m_terrain		= 1<<0,
		m_landPart		= 1<<1,
		m_bodyPart		= 1<<2,
		m_linkPart		= 1<<3,
		m_tirePart		= 1<<4,
		m_tireInnerRing = 1<<5,
	};

	char m_boneName[32];
	char m_shapeTypeName[32];
	dFloat m_mass;
	int m_bodyPartID;
	char m_articulationName[32];
};


static SERVO_VEHICLE_DEFINITION forkliftDefinition[] =
{
	{"body",		"convexHull",			900.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "mainBody"},
	{"fr_tire",		"tireShape",			 50.0f, SERVO_VEHICLE_DEFINITION::m_tirePart, "frontTire"},
	{"fl_tire",		"tireShape",			 50.0f, SERVO_VEHICLE_DEFINITION::m_tirePart, "frontTire"},
	{"rr_tire",		"tireShape",			 50.0f, SERVO_VEHICLE_DEFINITION::m_tirePart, "rearTire"},
	{"rl_tire",		"tireShape",			 50.0f, SERVO_VEHICLE_DEFINITION::m_tirePart, "rearTire"},
	{"lift_1",		"convexHull",			 50.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "hingeActuator"},
	{"lift_2",		"convexHull",			 40.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "liftActuator"},
	{"lift_3",		"convexHull",			 30.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "liftActuator"},
	{"lift_4",		"convexHull",			 20.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "liftActuator"},
	{"left_teeth",  "convexHullAggregate",	 10.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "paletteActuator"},
	{"right_teeth", "convexHullAggregate",	 10.0f, SERVO_VEHICLE_DEFINITION::m_bodyPart, "paletteActuator"},
};


class ServoEntityModel: public DemoEntity
{
	public:
	class InputRecord
	{
		public:
		InputRecord()
		{
			memset(this, 0, sizeof(InputRecord));
		}

		dFloat m_liftValue;
		dFloat m_forkValue;
		dFloat m_paletteValue;
		int m_steerValue;
		int m_throttleValue;
	};

	ServoEntityModel(DemoEntityManager* const scene, const char* const name)
		:DemoEntity(dGetIdentityMatrix(), NULL)
		,m_inputs()
		,m_engineMotor(NULL)
		,m_engineJoint(NULL)
		,m_forkBase(NULL)
		,m_maxEngineSpeed(20.0f)
	{
		m_liftJoints[0] = NULL;
		m_liftJoints[1] = NULL;
		m_liftJoints[2] = NULL;
		m_paletteJoints[0] = NULL;
		m_paletteJoints[1] = NULL;
		m_rearTireJoints[0] = NULL;
		m_rearTireJoints[1] = NULL;
		m_frontTiresJoints[0] = NULL;
		m_frontTiresJoints[1] = NULL;
		// load the vehicle model
		LoadNGD_mesh(name, scene->GetNewton());
	}

	ServoEntityModel(const ServoEntityModel& copy)
		:DemoEntity(copy)
		,m_inputs()
		,m_engineMotor(NULL)
		,m_engineJoint(NULL)
		,m_forkBase(NULL)
		,m_maxEngineSpeed(20.0f)
	{
		m_liftJoints[0] = NULL;
		m_liftJoints[1] = NULL;
		m_liftJoints[2] = NULL;
		m_paletteJoints[0] = NULL;
		m_paletteJoints[1] = NULL;
		m_rearTireJoints[0] = NULL;
		m_rearTireJoints[1] = NULL;
		m_frontTiresJoints[0] = NULL;
		m_frontTiresJoints[1] = NULL;
	}

	DemoEntity* CreateClone() const
	{
		return new ServoEntityModel(*this);
	}

	void SetInput(const InputRecord& inputs)
	{
		m_inputs = inputs;
	}

	InputRecord m_inputs;
	dCustomMotor* m_engineMotor;
	dCustomDoubleHinge* m_engineJoint;
	dCustomHingeActuator* m_forkBase;
	dCustomWheel* m_rearTireJoints[2];
	dCustomWheel* m_frontTiresJoints[2];
	dCustomSliderActuator* m_liftJoints[3];
	dCustomSliderActuator* m_paletteJoints[2];
	dFloat m_maxEngineSpeed;
};


class ServoInputManager: public dCustomInputManager
{
	public:
	ServoInputManager(DemoEntityManager* const scene)
		:dCustomInputManager(scene->GetNewton())
		,m_scene(scene)
		,m_cameraMode(true)
		,m_changeVehicle(true)
		,m_palette(0.0f)
		,m_forkAngle(0.0f)
		,m_listPosit(0.0f)
		,m_playersCount(0)
		,m_currentPlayer(0)
		,m_needsWakeUp(false)
	{
		// plug a callback for 2d help display
		scene->Set2DDisplayRenderFunction(RenderPlayerHelp, NULL, this);
		scene->SetUpdateCameraFunction(UpdateCameraCallback, this);
	}

	void OnBeginUpdate(dFloat timestepInSecunds)
	{
		ServoEntityModel::InputRecord inputs;
		ServoEntityModel* const vehicleModel = (ServoEntityModel*)m_player[m_currentPlayer % m_playersCount]->GetUserData();

		inputs.m_steerValue = int(m_scene->GetKeyState('D')) - int(m_scene->GetKeyState('A'));
		inputs.m_throttleValue = int(m_scene->GetKeyState('W')) - int(m_scene->GetKeyState('S'));

		inputs.m_paletteValue = -m_palette;
		inputs.m_liftValue = m_listPosit;
		inputs.m_forkValue = m_forkAngle * dDegreeToRad;

		// check if we must activate the player
		if (m_needsWakeUp ||
			m_scene->GetKeyState('A') ||
			m_scene->GetKeyState('D') ||
			m_scene->GetKeyState('W') ||
			m_scene->GetKeyState('S')) {
			NewtonBody* const body = m_player[m_currentPlayer % m_playersCount]->GetRoot()->m_body;
			NewtonBodySetSleepState(body, false);
		}

#if 0
#if 0
		static FILE* file = fopen("log.bin", "wb");
		if (file) {
			fwrite(&inputs, sizeof(inputs), 1, file);
			fflush(file);
		}
#else 
		static FILE* file = fopen("log.bin", "rb");
		if (file) {
			fread(&inputs, sizeof(inputs), 1, file);
		}
#endif
#endif
		vehicleModel->SetInput(inputs);
	}

	static void UpdateCameraCallback(DemoEntityManager* const manager, void* const context, dFloat timestep)
	{
		ServoInputManager* const me = (ServoInputManager*)context;
		me->UpdateCamera(timestep);
	}

	void UpdateCamera(dFloat timestepInSecunds)
	{
		DemoCamera* const camera = m_scene->GetCamera();
		ServoEntityModel* const vehicleModel = (ServoEntityModel*)m_player[m_currentPlayer % m_playersCount]->GetUserData();

		//if (!m_currentPlayer) {
		//	return;
		//}

		if (m_changeVehicle.UpdateTrigger(m_scene->GetKeyState('P'))) {
			m_currentPlayer++;
		}

		dMatrix camMatrix(camera->GetNextMatrix());
		dMatrix playerMatrix(vehicleModel->GetNextMatrix());

		dVector frontDir(camMatrix[0]);
		dVector camOrigin(0.0f);
		m_cameraMode.UpdatePushButton(m_scene->GetKeyState('C'));
		if (m_cameraMode.GetPushButtonState()) {
			camOrigin = playerMatrix.TransformVector(dVector(0.0f, SERVO_VEHICLE_CAMERA_HIGH_ABOVE_HEAD, 0.0f, 0.0f));
			camOrigin -= frontDir.Scale(SERVO_VEHICLE_CAMERA_DISTANCE);
		} else {
			camMatrix = camMatrix * playerMatrix;
			camOrigin = playerMatrix.TransformVector(dVector(-0.8f, SERVO_VEHICLE_CAMERA_EYEPOINT, 0.0f, 0.0f));
		}

		camera->SetNextMatrix(*m_scene, camMatrix, camOrigin);
	}

	void OnEndUpdate(dFloat timestepInSecunds)
	{
	}

	void AddPlayer(dCustomTransformController* const player)
	{
		m_player[m_playersCount] = player;
		m_playersCount++;
	}

	void RenderPlayerHelp(DemoEntityManager* const scene)
	{
		dVector color(1.0f, 1.0f, 0.0f, 0.0f);
		scene->Print(color, "Navigation Keys");
		scene->Print(color, "drive forward:      W");
		scene->Print(color, "drive backward:     S");
		scene->Print(color, "turn right:         D");
		scene->Print(color, "turn left:          A");

		m_needsWakeUp = false;
		m_needsWakeUp = ImGui::SliderFloat("lift", &m_listPosit, -0.5f, 1.5f) || m_needsWakeUp;
		m_needsWakeUp = ImGui::SliderFloat("tilt", &m_forkAngle, -30.0f, 30.0f) || m_needsWakeUp;
		m_needsWakeUp = ImGui::SliderFloat("palette", &m_palette, -0.2f, 0.6f) || m_needsWakeUp;
	}

	static void RenderPlayerHelp(DemoEntityManager* const scene, void* const context)
	{
		ServoInputManager* const me = (ServoInputManager*)context;
		me->RenderPlayerHelp(scene);
	}


	DemoEntityManager* m_scene;
	dCustomTransformController* m_player[2];
	DemoEntityManager::ButtonKey m_cameraMode;
	DemoEntityManager::ButtonKey m_changeVehicle;
	dFloat32 m_palette;
	dFloat32 m_forkAngle;
	dFloat32 m_listPosit;
	int m_playersCount;
	int m_currentPlayer;
	bool m_needsWakeUp;
};


class ServoVehicleManagerManager: public dCustomTransformManager
{
	public:
	ServoVehicleManagerManager(DemoEntityManager* const scene)
		:dCustomTransformManager(scene->GetNewton())
	{
		// create a material for early collision culling
		int material = NewtonMaterialGetDefaultGroupID(scene->GetNewton());
		NewtonMaterialSetCallbackUserData(scene->GetNewton(), material, material, this);
		NewtonMaterialSetCompoundCollisionCallback(scene->GetNewton(), material, material, CompoundSubCollisionAABBOverlap);
		NewtonMaterialSetCollisionCallback(scene->GetNewton(), material, material, OnBoneAABBOverlap, OnContactsProcess);
	}

	virtual void OnDebug(dCustomJoint::dDebugDisplay* const debugContext)
	{
	}

//	static int OnBoneAABBOverlap(const NewtonMaterial* const material, const NewtonBody* const body0, const NewtonBody* const body1, int threadIndex)
	static int OnBoneAABBOverlap(const NewtonJoint* const contactJoint, dFloat timestep, int threadIndex)
	{
		const NewtonBody* const body0 = NewtonJointGetBody0(contactJoint);
		const NewtonBody* const body1 = NewtonJointGetBody1(contactJoint);
		const NewtonCollision* const collision0 = NewtonBodyGetCollision(body0);
		const NewtonCollision* const collision1 = NewtonBodyGetCollision(body1);
		SERVO_VEHICLE_DEFINITION::SHAPES_ID id0 = SERVO_VEHICLE_DEFINITION::SHAPES_ID(NewtonCollisionGetUserID(collision0));
		SERVO_VEHICLE_DEFINITION::SHAPES_ID id1 = SERVO_VEHICLE_DEFINITION::SHAPES_ID(NewtonCollisionGetUserID(collision1));
		dAssert(id0 != SERVO_VEHICLE_DEFINITION::m_tireInnerRing);
		dAssert(id1 != SERVO_VEHICLE_DEFINITION::m_tireInnerRing);

		switch (id0 | id1) 
		{
			//case SERVO_VEHICLE_DEFINITION::m_linkPart | SERVO_VEHICLE_DEFINITION::m_linkPart:
			//case SERVO_VEHICLE_DEFINITION::m_linkPart | SERVO_VEHICLE_DEFINITION::m_bodyPart:
			case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_bodyPart:
			case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_tirePart:
			case SERVO_VEHICLE_DEFINITION::m_bodyPart | SERVO_VEHICLE_DEFINITION::m_bodyPart:
			{
				return 0;
				break;
			}

			case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_bodyPart:
			case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_tirePart:
			case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_landPart:
			//case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_linkPart:
			//case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_bodyPart:
			//case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_linkPart:
			//case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_landPart:
			//case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_bodyPart:
			//case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_linkPart:
			case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_landPart:
			{
				return 1;
				break;
			}
			default:
			{
				dAssert(0);
				return 1;
			}
		}
	}


	static int CompoundSubCollisionAABBOverlap (const NewtonJoint* const contact, dFloat timestep, const NewtonBody* const body0, const void* const collisionNode0, const NewtonBody* const body1, const void* const collisionNode1, int threadIndex)
	{
		dAssert(0);
		return 0;
/*
		dAssert(collisionNode0);
		NewtonCollision* const collision0 = NewtonCompoundCollisionGetCollisionFromNode (NewtonBodyGetCollision(body0), collisionNode0);
		NewtonCollision* const collision1 = collisionNode1 ? NewtonCompoundCollisionGetCollisionFromNode(NewtonBodyGetCollision(body1), collisionNode1) : NewtonBodyGetCollision(body1);

		SERVO_VEHICLE_DEFINITION::SHAPES_ID id0 = SERVO_VEHICLE_DEFINITION::SHAPES_ID(NewtonCollisionGetUserID(collision0));
		SERVO_VEHICLE_DEFINITION::SHAPES_ID id1 = SERVO_VEHICLE_DEFINITION::SHAPES_ID(NewtonCollisionGetUserID(collision1));
//		dAssert(id0 != SERVO_VEHICLE_DEFINITION::m_tireInnerRing);
//		dAssert(id1 != SERVO_VEHICLE_DEFINITION::m_tireInnerRing);

		switch (id0 | id1)
		{
			//case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_linkPart:			
			//case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_tireInnerRing:
			//case SERVO_VEHICLE_DEFINITION::m_linkPart | SERVO_VEHICLE_DEFINITION::m_tireInnerRing:
			case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_terrain:
			case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_landPart:
			case SERVO_VEHICLE_DEFINITION::m_tireInnerRing | SERVO_VEHICLE_DEFINITION::m_linkPart:
			{
				return 1;
				break;
			}

			case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_linkPart:
			case SERVO_VEHICLE_DEFINITION::m_tireInnerRing | SERVO_VEHICLE_DEFINITION::m_landPart:
			case SERVO_VEHICLE_DEFINITION::m_tireInnerRing | SERVO_VEHICLE_DEFINITION::m_terrain:
			{
				return 0;
				break;
			}
			default:
			{
				dAssert(0);
				return 0;
			}
		}
*/
	}
	
	static void OnContactsProcess (const NewtonJoint* const contactJoint, dFloat timestep, int threadIndex)
	{
//		dAssert(0);
/*
		int countCount = 0;
		void* contactList[32];

		for (void* contact = NewtonContactJointGetFirstContact(contactJoint); contact; contact = NewtonContactJointGetNextContact(contactJoint, contact)) {
			contactList[countCount] = contact;
			countCount++;
		}

		NewtonBody* const body0 = NewtonJointGetBody0(contactJoint);
		NewtonBody* const body1 = NewtonJointGetBody1(contactJoint);
		for (int i = 0; i < countCount; i ++) {
			NewtonMaterial* const material = NewtonContactGetMaterial (contactList[i]);
			NewtonCollision* const collision0 = NewtonMaterialGetBodyCollidingShape(material, body0);
			NewtonCollision* const collision1 = NewtonMaterialGetBodyCollidingShape(material, body1);

			SERVO_VEHICLE_DEFINITION::SHAPES_ID id0 = SERVO_VEHICLE_DEFINITION::SHAPES_ID(NewtonCollisionGetUserID(collision0));
			SERVO_VEHICLE_DEFINITION::SHAPES_ID id1 = SERVO_VEHICLE_DEFINITION::SHAPES_ID(NewtonCollisionGetUserID(collision1));

			switch (id0 | id1) 
			{
				//case SERVO_VEHICLE_DEFINITION::m_terrain | SERVO_VEHICLE_DEFINITION::m_tireInnerRing:
				//case SERVO_VEHICLE_DEFINITION::m_linkPart | SERVO_VEHICLE_DEFINITION::m_tirePart:
//				{
//					NewtonContactJointRemoveContact(contactJoint, contactList[i]);
//					break;
//				}
				
				case SERVO_VEHICLE_DEFINITION::m_bodyPart | SERVO_VEHICLE_DEFINITION::m_terrain:
				case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_terrain:
				case SERVO_VEHICLE_DEFINITION::m_tirePart | SERVO_VEHICLE_DEFINITION::m_linkPart:
				case SERVO_VEHICLE_DEFINITION::m_linkPart | SERVO_VEHICLE_DEFINITION::m_terrain:
				case SERVO_VEHICLE_DEFINITION::m_linkPart | SERVO_VEHICLE_DEFINITION::m_tireInnerRing:
				case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_tirePart:
				case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_bodyPart:
				case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_linkPart:
				case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_terrain:
				case SERVO_VEHICLE_DEFINITION::m_landPart | SERVO_VEHICLE_DEFINITION::m_landPart:
				{
					break;
				}
				default:
				{
					dAssert(0);
				}
			}
		}
*/
	}

	virtual void OnUpdateTransform(const dCustomTransformController::dSkeletonBone* const bone, const dMatrix& localMatrix) const
	{
		DemoEntity* const ent = (DemoEntity*)NewtonBodyGetUserData(bone->m_body);
		if (ent) {
			DemoEntityManager* const scene = (DemoEntityManager*)NewtonWorldGetUserData(NewtonBodyGetWorld(bone->m_body));

			dQuaternion rot(localMatrix);
			ent->SetMatrix(*scene, rot, localMatrix.m_posit);
		}
	}


	virtual void OnPreUpdate (dCustomTransformController* const controller, dFloat timestep, int threadIndex) const
	{
		ServoEntityModel* const vehicleModel = (ServoEntityModel*)controller->GetUserData();

		if (vehicleModel->m_engineJoint) {
			dFloat engineRPM = 0.0f;
			dFloat brakeTorque = 2000.0f;

			if (vehicleModel->m_inputs.m_throttleValue > 0) {
				brakeTorque = 0.0f;
				engineRPM = -vehicleModel->m_maxEngineSpeed;
			} else if (vehicleModel->m_inputs.m_throttleValue < 0) {
				brakeTorque = 0.0f;
				engineRPM = vehicleModel->m_maxEngineSpeed;
			}

			// apply DC engine torque
			vehicleModel->m_engineMotor->SetSpeed(engineRPM);

			// apply breaks
			//for (int i = 0; i < vehicleModel->m_tractionTiresCount; i++) {
			//	vehicleModel->m_tractionTiresJoints[i]->SetFriction(brakeTorque);
			//}
		}

		// update steering wheels
		dFloat steeringAngle = vehicleModel->m_rearTireJoints[0]->GetSteerAngle();
		if (vehicleModel->m_inputs.m_steerValue > 0) {
			steeringAngle = 30.0f * dDegreeToRad;
		} else if (vehicleModel->m_inputs.m_steerValue < 0) {
			steeringAngle = -30.0f * dDegreeToRad;
		}
		vehicleModel->m_rearTireJoints[0]->SetTargetSteerAngle(steeringAngle);
		vehicleModel->m_rearTireJoints[1]->SetTargetSteerAngle(steeringAngle);

		vehicleModel->m_forkBase->SetTargetAngle(vehicleModel->m_inputs.m_forkValue);
		vehicleModel->m_liftJoints[0]->SetTargetPosit(vehicleModel->m_inputs.m_liftValue);
		vehicleModel->m_liftJoints[1]->SetTargetPosit(vehicleModel->m_inputs.m_liftValue);
		vehicleModel->m_liftJoints[2]->SetTargetPosit(vehicleModel->m_inputs.m_liftValue);
		vehicleModel->m_paletteJoints[0]->SetTargetPosit(vehicleModel->m_inputs.m_paletteValue);
		vehicleModel->m_paletteJoints[1]->SetTargetPosit(vehicleModel->m_inputs.m_paletteValue);
	}

	NewtonCollision* MakeConvexHull(DemoEntity* const bodyPart) const
	{
		dVector points[1024 * 16];

		DemoMesh* const mesh = (DemoMesh*)bodyPart->GetMesh();
		dAssert(mesh->IsType(DemoMesh::GetRttiType()));
		dAssert(mesh->m_vertexCount && (mesh->m_vertexCount < int(sizeof(points) / sizeof(points[0]))));

		// go over the vertex array and find and collect all vertices's weighted by this bone.
		const dFloat* const array = mesh->m_vertex;
		for (int i = 0; i < mesh->m_vertexCount; i++) {
			points[i][0] = array[i * 3 + 0];
			points[i][1] = array[i * 3 + 1];
			points[i][2] = array[i * 3 + 2];
			points[i][3] = 0.0f;
		}
		bodyPart->GetMeshMatrix().TransformTriplex(&points[0][0], sizeof(dVector), &points[0][0], sizeof(dVector), mesh->m_vertexCount);
		return NewtonCreateConvexHull(GetWorld(), mesh->m_vertexCount, &points[0][0], sizeof(dVector), 1.0e-3f, SERVO_VEHICLE_DEFINITION::m_bodyPart, NULL);
	}

	NewtonCollision* MakePalleteShape(DemoEntity* const bodyPart) const
	{
		DemoMesh* const mesh = (DemoMesh*)bodyPart->GetMesh();
		dAssert(mesh->IsType(DemoMesh::GetRttiType()));

		// go over the vertex array and find and collect all vertices's weighted by this bone.
		dVector minExtend(1.0e10f);
		dVector maxExtend(-1.0e10f);
		const dFloat* const array = mesh->m_vertex;
		for (int i = 0; i < mesh->m_vertexCount; i++) {
			minExtend.m_x = dMin(minExtend.m_x, array[i * 3 + 0]);
			minExtend.m_y = dMin(minExtend.m_y, array[i * 3 + 1]);
			minExtend.m_z = dMin(minExtend.m_z, array[i * 3 + 2]);

			maxExtend.m_x = dMax(maxExtend.m_x, array[i * 3 + 0]);
			maxExtend.m_y = dMax(maxExtend.m_y, array[i * 3 + 1]);
			maxExtend.m_z = dMax(maxExtend.m_z, array[i * 3 + 2]);
		}

		dMatrix oririn(dGetIdentityMatrix());
		oririn.m_posit = (minExtend + maxExtend).Scale(0.5f);
		oririn.m_posit.m_w = 1.0f;
		maxExtend -= minExtend;

		oririn.m_posit.m_y -= maxExtend.m_y * 0.43f;
		maxExtend.m_y *= 0.125f;
		return NewtonCreateBox(GetWorld(), maxExtend.m_x, maxExtend.m_y, maxExtend.m_z, SERVO_VEHICLE_DEFINITION::m_bodyPart, &oririn[0][0]);
	}

	NewtonCollision* MakeForkLiftTireShape(DemoEntity* const bodyPart) const
	{
		dFloat radius = 0.0f;
		dFloat maxWidth = 0.0f;
		dFloat minWidth = 0.0f;

		DemoMesh* const mesh = (DemoMesh*)bodyPart->GetMesh();
		dAssert(mesh->IsType(DemoMesh::GetRttiType()));
		const dMatrix& matrix = bodyPart->GetMeshMatrix();
		dFloat* const array = mesh->m_vertex;
		for (int i = 0; i < mesh->m_vertexCount; i++) {
			dVector p(matrix.TransformVector(dVector(array[i * 3 + 0], array[i * 3 + 1], array[i * 3 + 2], 1.0f)));
			maxWidth = dMax(p.m_x, maxWidth);
			minWidth = dMin(p.m_x, minWidth);
			radius = dMax(p.m_y, radius);
		}
		dFloat width = maxWidth - minWidth;
		radius -= width * 0.5f;
		return NewtonCreateChamferCylinder(GetWorld(), radius, width, 0, NULL);
	}

	NewtonBody* CreateBodyPart(DemoEntity* const bodyPart, const SERVO_VEHICLE_DEFINITION& definition)
	{
		NewtonCollision* shape = NULL;
		if (!strcmp(definition.m_shapeTypeName, "tireShape")) {
			shape = MakeForkLiftTireShape(bodyPart);
		} else if (!strcmp(definition.m_shapeTypeName, "convexHull")) {
			shape = MakeConvexHull(bodyPart);
		} else if (!strcmp(definition.m_shapeTypeName, "convexHullAggregate")) {
			shape = MakePalleteShape(bodyPart);
		} else {
			dAssert(0);
		}

		// calculate the bone matrix
		dMatrix matrix(bodyPart->CalculateGlobalMatrix());

		NewtonWorld* const world = GetWorld();

		// create the rigid body that will make this bone
		NewtonBody* const body = NewtonCreateDynamicBody(world, shape, &matrix[0][0]);

		// destroy the collision helper shape 
		NewtonDestroyCollision(shape);

		// get the collision from body
		NewtonCollision* const collision = NewtonBodyGetCollision(body);

		// calculate the moment of inertia and the relative center of mass of the solid
		NewtonBodySetMassProperties(body, definition.m_mass, collision);

		// save the user data with the bone body (usually the visual geometry)
		NewtonBodySetUserData(body, bodyPart);

		// assign a body part id
		NewtonCollisionSetUserID(collision, definition.m_bodyPartID);

		// set the bod part force and torque call back to the gravity force, skip the transform callback
		NewtonBodySetForceAndTorqueCallback(body, PhysicsApplyGravityForce);
		return body;
	}

	dCustomHingeActuator* LinkHingeActuator (NewtonBody* const parent, NewtonBody* const child)
	{
		dMatrix baseMatrix;
		NewtonBodyGetMatrix (child, &baseMatrix[0][0]);

		dFloat minAngleLimit = -20.0f * dDegreeToRad;
		dFloat maxAngleLimit =  20.0f * dDegreeToRad;
		dFloat angularRate = 15.0f * dDegreeToRad;
		return new dCustomHingeActuator (&baseMatrix[0][0], angularRate, minAngleLimit, maxAngleLimit, child, parent);
	}

	dCustomSliderActuator* LinkLiftActuator(NewtonBody* const parent, NewtonBody* const child)
	{
		dMatrix baseMatrix;
		NewtonBodyGetMatrix(child, &baseMatrix[0][0]);

		dFloat minLimit = -2.0f;
		dFloat maxLimit = 2.5f;
		dFloat linearRate = 0.20f;
		return new dCustomSliderActuator(&baseMatrix[0][0], linearRate, minLimit, maxLimit, child, parent);
	}

	dCustomWheel* LinkTireJoint(NewtonBody* const chassis, NewtonBody* const tire)
	{
		dMatrix tireMatrix;
		dMatrix chassisMatrix;

		// calculate the tire location matrix
		NewtonBodyGetMatrix(tire, &tireMatrix[0][0]);
		NewtonBodyGetMatrix(chassis, &chassisMatrix[0][0]);

		chassisMatrix = dRollMatrix(90.0f * dDegreeToRad) * chassisMatrix;
		chassisMatrix = dPitchMatrix(90.0f * dDegreeToRad) * chassisMatrix;
		chassisMatrix.m_posit = tireMatrix.m_posit;

		dFloat angularRate = 60.0f * dDegreeToRad;
		dCustomWheel* const wheel = new dCustomWheel(&chassisMatrix[0][0], tire, chassis);
		wheel->SetSteerRate(angularRate);
		wheel->EnableLimits(true);
		wheel->SetLimits(0.0f, 0.0f);
		return wheel;
	}

	dCustomWheel* LinkFrontTireJoint(dCustomTransformController* const controller, NewtonBody* const chassis, NewtonBody* const tire)
	{
		dCustomWheel* const wheel = LinkTireJoint(chassis, tire);

		// link traction tire to the engine using a differential gear
		dMatrix matrix;
		dMatrix engineMatrix;
		dMatrix chassisMatrix;
		
		NewtonBodyGetMatrix(tire, &matrix[0][0]);
		dMatrix tireHingeMatrix(dRollMatrix(0.0f * dDegreeToRad) * matrix);
		tireHingeMatrix = wheel->GetMatrix0() * tireHingeMatrix;

		ServoEntityModel* const vehicleModel = (ServoEntityModel*)controller->GetUserData();

		dAssert (chassis == vehicleModel->m_engineJoint->GetBody1());
		NewtonBody* const engine = vehicleModel->m_engineJoint->GetBody0();
		vehicleModel->m_engineJoint->CalculateGlobalMatrix(engineMatrix, chassisMatrix);

		dFloat sign = dSign(engineMatrix.m_up.DotProduct3(tireHingeMatrix.m_posit - engineMatrix.m_posit));
		new dCustomDifferentialGear(5.0f, tireHingeMatrix.m_up.Scale(-1.0f), engineMatrix.m_front.Scale(sign), chassisMatrix.m_up, tire, engine, chassis);

		return wheel;
	}

	void ConnectBodyPart(dCustomTransformController* const controller, NewtonBody* const parent, NewtonBody* const child, const dString& jointArticulation, dList<dCustomJoint*>& cycleLinks)
	{
		ServoEntityModel* const vehicleModel = (ServoEntityModel*) controller->GetUserData();

		if (jointArticulation == "") {
			// this is the root body do nothing
		} else if (jointArticulation == "frontTire") {
			dCustomWheel* const tire = LinkFrontTireJoint(controller, parent, child);
			if (!vehicleModel->m_frontTiresJoints[0]) {
				vehicleModel->m_frontTiresJoints[0] = tire;
			} else {
				dAssert(!vehicleModel->m_frontTiresJoints[1]);
				vehicleModel->m_frontTiresJoints[1] = tire;
			}
		} else if (jointArticulation == "rearTire") {
			dCustomWheel* const tire = LinkTireJoint(parent, child);
			if (!vehicleModel->m_rearTireJoints[0]) {
				vehicleModel->m_rearTireJoints[0] = tire;
			} else {
				dAssert(!vehicleModel->m_rearTireJoints[1]);
				vehicleModel->m_rearTireJoints[1] = tire;
			}
		} else if (jointArticulation == "hingeActuator") {
			vehicleModel->m_forkBase = LinkHingeActuator(parent, child);
		} else if (jointArticulation == "liftActuator") {
			dCustomSliderActuator* const lift = LinkLiftActuator(parent, child);;
			if (!vehicleModel->m_liftJoints[0]) {
				vehicleModel->m_liftJoints[0] = lift;
			} else if (!vehicleModel->m_liftJoints[1]) {
				dAssert(!vehicleModel->m_liftJoints[1]);
				vehicleModel->m_liftJoints[1] = lift;
			} else {
				dAssert(!vehicleModel->m_liftJoints[2]);
				vehicleModel->m_liftJoints[2] = lift;
			}

		} else if (jointArticulation == "paletteActuator") {
			dCustomSliderActuator* const lift = LinkLiftActuator(parent, child);;
			if (!vehicleModel->m_paletteJoints[0]) {
				vehicleModel->m_paletteJoints[0] = lift;
			} else {
				dAssert(!vehicleModel->m_paletteJoints[1]);
				vehicleModel->m_paletteJoints[1] = lift;
			}
		} else {
			dAssert(0);
		}
	}

	dCustomTransformController::dSkeletonBone* CreateEngineNode(dCustomTransformController* const controller, dCustomTransformController::dSkeletonBone* const chassisBone)
	{
		NewtonWorld* const world = GetWorld();
		NewtonCollision* const shape = NewtonCreateCylinder(world, 0.125f, 0.125f, 0.75f, 0, NULL);

		NewtonBody* const chassis = chassisBone->m_body;

		// create the rigid body that will make this bone
		dMatrix engineMatrix;
		NewtonBodyGetMatrix(chassis, &engineMatrix[0][0]);
		engineMatrix = dRollMatrix(0.5f * dPi) * engineMatrix;
		engineMatrix.m_posit.m_y += 1.0f;

		NewtonBody* const engineBody = NewtonCreateDynamicBody(world, shape, &engineMatrix[0][0]);

		// destroy the collision helper shape 
		NewtonDestroyCollision(shape);

		// get the collision from body
		NewtonCollision* const collision = NewtonBodyGetCollision(engineBody);
		NewtonCollisionSetMode(collision, 0);

		// calculate the moment of inertia and the relative center of mass of the solid
		//NewtonBodySetMassProperties(engineBody, 50.0f, collision);
		dFloat mass = 50.0f;
		dFloat radius = 1.0f;
		dFloat Inertia = 2.0f * mass * radius * radius / 5.0f;
		NewtonBodySetMassMatrix(engineBody, mass, Inertia, Inertia, Inertia);

		// set the bod part force and torque call back to the gravity force, skip the transform callback
		NewtonBodySetForceAndTorqueCallback(engineBody, PhysicsApplyGravityForce);

		// connect engine to chassis with a hinge
		dMatrix engineAxis;
		engineAxis.m_front = engineMatrix.m_front;
		engineAxis.m_up = engineMatrix.m_right;
		engineAxis.m_right = engineAxis.m_front.CrossProduct(engineAxis.m_up);
		engineAxis.m_posit = engineMatrix.m_posit;

		dCustomDoubleHinge* const engineJoint = new dCustomDoubleHinge(engineAxis, engineBody, chassis);
		engineJoint->EnableLimits(false);
		engineJoint->EnableLimits1(false);
		return controller->AddBone(engineBody, dGetIdentityMatrix(), chassisBone);
	}

	dCustomMotor* CreateEngineMotor(dCustomTransformController* const controller, dCustomDoubleHinge* const engineJoint)
	{
		dMatrix engineMatrix;
		dMatrix chassisMatrix;
		NewtonBody* const engine = engineJoint->GetBody0();
		NewtonBody* const chassis = engineJoint->GetBody1();
		engineJoint->CalculateGlobalMatrix(engineMatrix, chassisMatrix);
		return new dCustomMotor(engineMatrix.m_up, engine, chassis);
	}

	dCustomTransformController* CreateForklift(const dMatrix& location, const DemoEntity* const model, int bodyPartsCount, SERVO_VEHICLE_DEFINITION* const definition)
	{
		NewtonWorld* const world = GetWorld();
		DemoEntityManager* const scene = (DemoEntityManager*)NewtonWorldGetUserData(world);

		// make a clone of the mesh 
		ServoEntityModel* const vehicleModel = (ServoEntityModel*)model->CreateClone();
		scene->Append(vehicleModel);

		// plane the model at its location
		vehicleModel->ResetMatrix(*scene, location);

		dCustomTransformController* const controller = CreateTransformController();

		controller->SetUserData(vehicleModel);
		controller->SetCalculateLocalTransforms(true);

		DemoEntity* const rootEntity = (DemoEntity*)vehicleModel->Find(definition[0].m_boneName);

		NewtonBody* const rootBody = CreateBodyPart(rootEntity, definition[0]);

		// move the center of mass a lithe to the back, and lower
		dVector com(0.0f);
		NewtonBodyGetCentreOfMass(rootBody, &com[0]);
		//com.m_x -= 0.25f;
		com.m_y -= 0.25f;
		NewtonBodySetCentreOfMass(rootBody, &com[0]);

//new dCustom6dof (location, rootBody);
//NewtonBodySetMassMatrix(rootBody, 0.0f, 0.0f, 0.0f, 0.0f);

		// add the root bone to the articulation manager
		dCustomTransformController::dSkeletonBone* const chassisBone = controller->AddRoot(rootBody, dGetIdentityMatrix());

		// add engine
		dCustomTransformController::dSkeletonBone* const engineBone = CreateEngineNode(controller, chassisBone);
		vehicleModel->m_engineJoint = (dCustomDoubleHinge*)engineBone->FindJoint();
		vehicleModel->m_engineMotor = CreateEngineMotor(controller, vehicleModel->m_engineJoint);

		// set power parameter for a simple DC engine
		vehicleModel->m_maxEngineSpeed = 20.0f;
		vehicleModel->m_engineMotor->SetTorque(1000.0f);
		//vehicleModel->m_engineMotor->SetTorque1(1500.0f);

		// walk down the model hierarchy an add all the components 
		int stackIndex = 0;
		DemoEntity* childEntities[32];
		dCustomTransformController::dSkeletonBone* parentBones[32];
		for (DemoEntity* child = rootEntity->GetChild(); child; child = child->GetSibling()) {
			parentBones[stackIndex] = chassisBone;
			childEntities[stackIndex] = child;
			stackIndex++;
		}

		dList<dCustomJoint*> cycleLinks;
		while (stackIndex) {
			stackIndex--;
			DemoEntity* const entity = childEntities[stackIndex];
			dCustomTransformController::dSkeletonBone* parentBone = parentBones[stackIndex];

			const char* const name = entity->GetName().GetStr();
			for (int i = 0; i < bodyPartsCount; i++) {
				if (!strcmp(definition[i].m_boneName, name)) {
					NewtonBody* const bone = CreateBodyPart(entity, definition[i]);

					// connect this body part to its parent with a vehicle joint
					ConnectBodyPart(controller, parentBone->m_body, bone, definition[i].m_articulationName, cycleLinks);

					dMatrix bindMatrix(entity->GetParent()->CalculateGlobalMatrix((DemoEntity*)NewtonBodyGetUserData(parentBone->m_body)).Inverse());
					parentBone = controller->AddBone(bone, bindMatrix, parentBone);
					break;
				}
			}

			for (DemoEntity* child = entity->GetChild(); child; child = child->GetSibling()) {
				parentBones[stackIndex] = parentBone;
				childEntities[stackIndex] = child;
				stackIndex++;
			}
		}

		return controller;
	}
};


void ServoJoints (DemoEntityManager* const scene)
{
	// load the sky box
	scene->CreateSkyBox();
	NewtonBody* const floor = CreateLevelMesh (scene, "flatPlane.ngd", true);
	//CreateHeightFieldTerrain (scene, 9, 8.0f, 1.5f, 0.2f, 200.0f, -50.0f);
	NewtonCollision* const floorCollision = NewtonBodyGetCollision(floor);
	NewtonCollisionSetUserID(floorCollision, SERVO_VEHICLE_DEFINITION::m_terrain);

	NewtonWorld* const world = scene->GetNewton();
	dVector origin (FindFloor (world, dVector (-10.0f, 50.0f, 0.0f, 1.0f), 2.0f * 50.0f));

	// add an input Manage to manage the inputs and user interaction 
	ServoInputManager* const inputManager = new ServoInputManager (scene);

	//  create a skeletal transform controller for controlling rag doll
	ServoVehicleManagerManager* const vehicleManager = new ServoVehicleManagerManager (scene);

	dMatrix matrix (dGetIdentityMatrix());
	matrix.m_posit = FindFloor (world, origin, 100.0f);
	matrix.m_posit.m_y += 1.5f;

	// load a the mesh of the articulate vehicle
	ServoEntityModel forkliftModel(scene, "forklift.ngd");
	dCustomTransformController* const forklift = vehicleManager->CreateForklift(matrix, &forkliftModel, sizeof(forkliftDefinition) / sizeof (forkliftDefinition[0]), forkliftDefinition);
	inputManager->AddPlayer(forklift);

	// add some object to play with
	LoadLumberYardMesh (scene, dVector(5.0f, 0.0f, 0.0f, 0.0f), SERVO_VEHICLE_DEFINITION::m_landPart);
	LoadLumberYardMesh (scene, dVector(5.0f, 0.0f, 10.0f, 0.0f), SERVO_VEHICLE_DEFINITION::m_landPart);

//	origin.m_x -= 5.0f;
	origin.m_y += 2.0f;
	origin.m_z -= 4.0f;
	dQuaternion rot (dVector (0.0f, 1.0f, 0.0f, 0.0f), -45.0f * dDegreeToRad);  
	scene->SetCameraMatrix(rot, origin);
}



