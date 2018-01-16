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



// dCustomSlider.h: interface for the dCustomSlider class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _CUSTOM_SLIDER_H__
#define _CUSTOM_SLIDER_H__

#include "dCustom6dof.h"

class dCustomSlider: public dCustom6dof
{
	public:
	CUSTOM_JOINTS_API dCustomSlider (const dMatrix& pinAndPivotFrame, NewtonBody* const child, NewtonBody* const parent = NULL);
	CUSTOM_JOINTS_API dCustomSlider (const dMatrix& pinAndPivotFrameChild, const dMatrix& pinAndPivotFrameParent, NewtonBody* const child, NewtonBody* const parent = NULL);
	CUSTOM_JOINTS_API virtual ~dCustomSlider();

	CUSTOM_JOINTS_API void EnableLimits(bool state);
	CUSTOM_JOINTS_API void SetLimits(dFloat mindist, dFloat maxdist);
	CUSTOM_JOINTS_API void SetAsSpringDamper(bool state, dFloat springDamperRelaxation, dFloat spring, dFloat damper);

	CUSTOM_JOINTS_API dFloat GetJointPosit () const;
	CUSTOM_JOINTS_API dFloat GetJointSpeed () const;
	
	protected:
	CUSTOM_JOINTS_API virtual void Deserialize (NewtonDeserializeCallback callback, void* const userData); 
	CUSTOM_JOINTS_API virtual void Serialize (NewtonSerializeCallback callback, void* const userData) const; 
	CUSTOM_JOINTS_API virtual void SubmitConstraintsFreeDof(int freeDof, const dMatrix& matrix0, const dMatrix& matrix1, dFloat timestep, int threadIndex);

	dFloat m_speed;
	dFloat m_posit;
	dFloat m_spring;
	dFloat m_damper;
	dFloat m_springDamperRelaxation;
	union {
		int m_options;
		struct {
			unsigned m_limitsOn			 : 1;
			unsigned m_setAsSpringDamper : 1;
			unsigned m_actuatorFlag		 : 1;
		};
	};
	DECLARE_CUSTOM_JOINT(dCustomSlider, dCustom6dof)
};

#endif

