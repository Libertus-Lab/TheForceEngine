#pragma once
//////////////////////////////////////////////////////////////////////
// Level
// Classic Dark Forces (DOS) Jedi derived Level structures and shared
// functionality used by systems such as collision detection,
// rendering, and INF.
//
// Copyright note:
// While the project as a whole is licensed under GPL-2.0, some of the
// code under TFE_Level/ was derived from reverse-engineered
// code from "Dark Forces" (DOS) which is copyrighted by LucasArts.
//
// I consider the reverse-engineering to be "Fair Use" - a means of 
// supporting the games on other platforms and to improve support on
// existing platforms without claiming ownership of the games
// themselves or their IPs.
//
// That said using this code in a commercial project is risky without
// permission of the original copyright holders (LucasArts).
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/memoryPool.h>
#include <TFE_JediSound/soundSystem.h>
#include "core_math.h"
#include "fixedPoint.h"
#include "rsector.h"

namespace TFE_Level
{
	void initMission();
	bool loadGeometry(const char* levelName);
	bool loadObjects(const char* levelName);

	void setObjPos_AddToSector(SecObject* obj, s32 x, s32 y, s32 z, RSector* sector);

	extern u32 s_sectorCount;
	extern RSector* s_sectors;
}