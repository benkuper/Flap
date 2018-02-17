/*
  ==============================================================================

    CVGroup.h
    Created: 15 Feb 2018 3:49:35pm
    Author:  Ben

  ==============================================================================
*/

#pragma once

#include "CustomVariable.h"

class CVGroup :
	public BaseItem
{
public:
	CVGroup(const String &name = "CVGroup");
	~CVGroup();

	BaseManager<CustomVariable> manager;
};