#include "rsector.h"
#include "rwall.h"
#include "robject.h"
#include "level.h"
#include <TFE_System/system.h>
#include <TFE_DarkForces/player.h>
#include <TFE_Jedi/Collision/collision.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
#include <TFE_Jedi/InfSystem/message.h>
// TODO: Find a better way to handle this.
#include <TFE_Jedi/InfSystem/infTypesInternal.h>

using namespace TFE_DarkForces;

namespace TFE_Jedi
{
	// Internal Forward Declarations
	void sector_computeWallDirAndLength(RWall* wall);
	void sector_moveWallVertex(RWall* wall, fixed16_16 offsetX, fixed16_16 offsetZ);
	u32  sector_objOverlapsWall(RWall* wall, SecObject* obj, s32* objSide);
	u32  sector_canWallMove(RWall* wall, fixed16_16 offsetX, fixed16_16 offsetZ);
	void sector_moveObjects(RSector* sector, u32 flags, fixed16_16 offsetX, fixed16_16 offsetZ);

	f32 isLeft(Vec2f p0, Vec2f p1, Vec2f p2);
	
	/////////////////////////////////////////////////
	// API Implementation
	/////////////////////////////////////////////////
	void sector_clear(RSector* sector)
	{
		sector->vertexCount = 0;
		sector->wallCount = 0;
		sector->objectCount = 0;
		sector->secHeight = 0;
		sector->collisionFrame = 0;
		sector->id = 0;
		sector->prevDrawFrame = 0;
		sector->infLink = 0;
		sector->objectCapacity = 0;
		sector->verticesWS = nullptr;
		sector->verticesVS = nullptr;
		sector->self = sector;
	}

	void sector_setupWallDrawFlags(RSector* sector)
	{
		RWall* wall = sector->walls;
		for (s32 w = 0; w < sector->wallCount; w++, wall++)
		{
			if (wall->nextSector)
			{
				RSector* wSector = wall->sector;
				fixed16_16 wFloorHeight = wSector->floorHeight;
				fixed16_16 wCeilHeight = wSector->ceilingHeight;

				RWall* mirror = wall->mirrorWall;
				RSector* mSector = mirror->sector;
				fixed16_16 mFloorHeight = mSector->floorHeight;
				fixed16_16 mCeilHeight = mSector->ceilingHeight;

				wall->drawFlags = WDF_MIDDLE;
				mirror->drawFlags = WDF_MIDDLE;

				if (wCeilHeight < mCeilHeight)
				{
					wall->drawFlags |= WDF_TOP;
				}
				if (wFloorHeight > mFloorHeight)
				{
					wall->drawFlags |= WDF_BOT;
				}
				if (mCeilHeight < wCeilHeight)
				{
					mirror->drawFlags |= WDF_TOP;
				}
				if (mFloorHeight > wFloorHeight)
				{
					mirror->drawFlags |= WDF_BOT;
				}
				wall_computeTexelHeights(wall->mirrorWall);
			}
			wall_computeTexelHeights(wall);
		}
	}
		
	void sector_adjustHeights(RSector* sector, fixed16_16 floorOffset, fixed16_16 ceilOffset, fixed16_16 secondHeightOffset)
	{
		sector->dirtyFlags |= SDF_HEIGHTS;

		// Adjust objects.
		if (sector->objectCount)
		{
			fixed16_16 heightOffset = secondHeightOffset + floorOffset;
			for (s32 i = 0; i < sector->objectCapacity; i++)
			{
				SecObject* obj = sector->objectList[i];
				if (!obj) { continue; }
				
				if (obj->posWS.y == sector->floorHeight)
				{
					obj->posWS.y += floorOffset;
					if (obj->entityFlags & ETFLAG_PLAYER)
					{
						s_playerHeight += floorOffset;
					}
				}

				if (obj->posWS.y == sector->ceilingHeight)
				{
					obj->posWS.y += ceilOffset;
					if (obj->entityFlags & ETFLAG_PLAYER)
					{
						// Why not ceilingOffset?
						s_playerHeight += floorOffset;
					}
				}

				fixed16_16 secHeight = sector->floorHeight + sector->secHeight;
				if (obj->posWS.y == secHeight)
				{
					obj->posWS.y += secondHeightOffset;
					if (obj->entityFlags & ETFLAG_PLAYER)
					{
						// Why not secHeightOffset?
						s_playerHeight += floorOffset;
					}
				}
			}
		}
		// Adjust sector heights.
		sector->ceilingHeight += ceilOffset;
		sector->floorHeight += floorOffset;
		sector->secHeight += secondHeightOffset;

		// Update wall data.
		s32 wallCount = sector->wallCount;
		RWall* wall = sector->walls;
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			if (wall->nextSector)
			{
				wall_setupAdjoinDrawFlags(wall);
				wall_computeTexelHeights(wall->mirrorWall);
			}
			wall_computeTexelHeights(wall);
		}

		// Update collision data.
		fixed16_16 floorHeight = sector->floorHeight;
		if (sector->flags1 & SEC_FLAGS1_PIT)
		{
			floorHeight += SEC_SKY_HEIGHT;
		}
		fixed16_16 ceilHeight = sector->ceilingHeight;
		if (sector->flags1 & SEC_FLAGS1_EXTERIOR)
		{
			ceilHeight -= SEC_SKY_HEIGHT;
		}
		fixed16_16 secHeight = sector->floorHeight + sector->secHeight;
		if (sector->secHeight >= 0 && floorHeight > secHeight)
		{
			secHeight = floorHeight;
		}

		sector->colFloorHeight = floorHeight;
		sector->colCeilHeight = ceilHeight;
		sector->colSecHeight = secHeight;
		sector->colMinHeight = ceilHeight;
	}

	void sector_computeBounds(RSector* sector)
	{
		RWall* wall = sector->walls;
		vec2_fixed* w0 = wall->w0;
		fixed16_16 maxX = w0->x;
		fixed16_16 maxZ = w0->z;
		fixed16_16 minX = maxX;
		fixed16_16 minZ = maxZ;

		wall++;
		for (s32 i = 1; i < sector->wallCount; i++, wall++)
		{
			w0 = wall->w0;

			minX = min(minX, w0->x);
			minZ = min(minZ, w0->z);

			maxX = max(maxX, w0->x);
			maxZ = max(maxZ, w0->z);
		}

		sector->boundsMin.x = minX;
		sector->boundsMax.x = maxX;
		sector->boundsMin.z = minZ;
		sector->boundsMax.z = maxZ;

		// Setup when needed.
		//s_minX = minX;
		//s_maxX = maxX;
		//s_minZ = minZ;
		//s_maxZ = maxZ;
	}

	fixed16_16 sector_getMaxObjectHeight(RSector* sector)
	{
		s32 maxObjHeight = 0;
		s32 count = sector->objectCount;
		SecObject** objectList = sector->objectList;

		if (!sector->objectCount)
		{
			return 0;
		}

		for (; count > 0; objectList++)
		{
			SecObject* obj = *objectList;
			if (obj)
			{
				maxObjHeight = max(maxObjHeight, obj->worldHeight + ONE_16);
				count--;
			}
		}
		return maxObjHeight;
	}
			
	u32 sector_moveWalls(RSector* sector, fixed16_16 delta, fixed16_16 dirX, fixed16_16 dirZ, u32 flags)
	{
		sector->dirtyFlags |= SDF_VERTICES;

		fixed16_16 offsetX = mul16(delta, dirX);
		fixed16_16 offsetZ = mul16(delta, dirZ);

		u32 sectorBlocked = 0;
		s32 wallCount = sector->wallCount;
		RWall* wall = sector->walls;
		for (s32 i = 0; i < wallCount && !sectorBlocked; i++, wall++)
		{
			if (wall->flags1 & WF1_WALL_MORPHS)
			{
				sectorBlocked |= sector_canWallMove(wall, offsetX, offsetZ);

				RWall* mirror = wall->mirrorWall;
				if (wall->mirrorWall && (mirror->flags1 & WF1_WALL_MORPHS))
				{
					sectorBlocked |= sector_canWallMove(mirror, offsetX, offsetZ);
				}
			}
		}

		if (!sectorBlocked)
		{
			wall = sector->walls;
			for (s32 i = 0; i < wallCount; i++, wall++)
			{
				if (wall->flags1 & WF1_WALL_MORPHS)
				{
					sector_moveWallVertex(wall, offsetX, offsetZ);
					RWall* mirror = wall->mirrorWall;
					if (mirror && (mirror->flags1 & WF1_WALL_MORPHS))
					{
						sector_moveWallVertex(mirror, offsetX, offsetZ);
					}
				}
			}
			sector_moveObjects(sector, flags, offsetX, offsetZ);
			sector_computeBounds(sector);
		}

		return !sectorBlocked ? 0xffffffff : 0;
	}

	void sector_changeWallLight(RSector* sector, fixed16_16 delta)
	{
		sector->dirtyFlags |= SDF_WALL_LIGHT;

		RWall* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			if (wall->flags1 & WF1_CHANGE_WALL_LIGHT)
			{
				wall->wallLight += delta;
			}
		}
	}

	void sector_scrollWalls(RSector* sector, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
		RWall* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		sector->dirtyFlags |= SDF_WALL_OFFSETS;

		const u32 scrollFlags = WF1_SCROLL_SIGN_TEX | WF1_SCROLL_BOT_TEX | WF1_SCROLL_MID_TEX | WF1_SCROLL_TOP_TEX;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			if (wall->flags1 & scrollFlags)
			{
				if (wall->flags1 & WF1_SCROLL_TOP_TEX)
				{
					wall->topOffset.x += offsetX;
					wall->topOffset.z += offsetZ;
				}
				if (wall->flags1 & WF1_SCROLL_MID_TEX)
				{
					wall->midOffset.x += offsetX;
					wall->midOffset.z += offsetZ;
				}
				if (wall->flags1 & WF1_SCROLL_BOT_TEX)
				{
					wall->botOffset.x += offsetX;
					wall->botOffset.z += offsetZ;
				}
				if (wall->flags1 & WF1_SCROLL_SIGN_TEX)
				{
					wall->signOffset.x += offsetX;
					wall->signOffset.z += offsetZ;
				}
			}
		}
	}

	void sector_adjustTextureWallOffsets_Floor(RSector* sector, fixed16_16 floorDelta)
	{
		sector->dirtyFlags |= SDF_WALL_OFFSETS;

		RWall* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			fixed16_16 textureOffset = floorDelta * 8;
			if (wall->flags1 & WF1_TEX_ANCHORED)
			{
				if (wall->nextSector)
				{
					wall->botOffset.z -= textureOffset;
				}
				else
				{
					wall->midOffset.z -= textureOffset;
				}
			}
			if (wall->flags1 & WF1_SIGN_ANCHORED)
			{
				wall->signOffset.z -= textureOffset;
			}
		}
	}

	void sector_adjustTextureMirrorOffsets_Floor(RSector* sector, fixed16_16 floorDelta)
	{
		RWall* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			RWall* mirror = wall->mirrorWall;
			if (mirror)
			{
				mirror->sector->dirtyFlags |= SDF_WALL_OFFSETS;

				fixed16_16 textureOffset = -floorDelta * 8;
				if (mirror->flags1 & WF1_TEX_ANCHORED)
				{
					if (mirror->nextSector)
					{
						mirror->botOffset.z -= textureOffset;
					}
					else
					{
						mirror->midOffset.z -= textureOffset;
					}
				}
				if (mirror->flags1 & WF1_SIGN_ANCHORED)
				{
					mirror->signOffset.z -= textureOffset;
				}
			}
		}
	}

	void sector_addObject(RSector* sector, SecObject* obj)
	{
		if (sector != obj->sector)
		{
			// Remove the object from its current sector (if it has one).
			if (obj->sector)
			{
				sector_removeObject(obj);
			}
			
			// TODO(Core Game Loop Release): Sending this message is skipped for the player if 'pickupFlags' is set to a specific value, I need to look into this more.
			// Send a message to the INF system that the object entered the sector.
			message_sendToSector(sector, obj, INF_EVENT_ENTER_SECTOR, MSG_TRIGGER);

			// The sector containing the player has a special flag.
			if (obj->entityFlags & ETFLAG_PLAYER)
			{
				sector->flags1 |= SEC_FLAGS1_PLAYER;
			}

			// Grow the object list if necessary.
			s32 objCount = sector->objectCount;
			s32 objectCapacity = sector->objectCapacity;
			if (objCount == objectCapacity)
			{
				SecObject** list;
				if (!objectCapacity)
				{
					list = (SecObject**)malloc(sizeof(SecObject*) * 5);
					sector->objectList = list;
				}
				else
				{
					sector->objectList = (SecObject**)realloc(sector->objectList, sizeof(SecObject*) * (objectCapacity + 5));
					list = sector->objectList + objectCapacity;
				}
				memset(list, 0, sizeof(SecObject*) * 5);
				sector->objectCapacity += 5;
			}

			// Then add the object to the first free slot.
			// Note that this scheme is optimized for deletion rather than addition.
			SecObject** list = sector->objectList;
			for (s32 i = 0; i < sector->objectCapacity; i++, list++)
			{
				if (!(*list))
				{
					*list = obj;
					obj->index = i;
					obj->sector = sector;
					sector->objectCount++;
					break;
				}
			}
		}
	}

	void sector_removeObject(SecObject* obj)
	{
		if (!obj || !obj->sector) { return; }

		RSector* sector = obj->sector;
		obj->sector = nullptr;

		// Remove the object from the object list.
		SecObject** objList = sector->objectList;
		objList[obj->index] = nullptr;
		sector->objectCount--;

		// Handle the player leaving.
		// TODO(Core Game Loop Release): The original had additional flags to look into.
		if (obj->entityFlags & ETFLAG_PLAYER)
		{
			message_sendToSector(sector, obj, INF_EVENT_LEAVE_SECTOR, MSG_TRIGGER);
			sector->flags1 &= ~SEC_FLAGS1_PLAYER;
		}
	}

	void sector_changeGlobalLightLevel()
	{
		RSector* sector = s_sectors;
		for (s32 i = 0; i < s_sectorCount; i++, sector++)
		{
			fixed16_16 newLightLevel = intToFixed16(sector->flags3);
			sector->flags3 = floor16(sector->ambient);
			sector->ambient = newLightLevel;
		}
	}
	
	RSector* sector_which3D(fixed16_16 dx, fixed16_16 dy, fixed16_16 dz)
	{
		fixed16_16 ix = dx;
		fixed16_16 iz = dz;
		fixed16_16 y = dy;
		
		RSector* sector = s_sectors;
		RSector* foundSector = nullptr;
		s32 sectorUnitArea = 0;
		s32 prevSectorUnitArea = INT_MAX;

		for (u32 i = 0; i < s_sectorCount; i++, sector++)
		{
			if (y >= sector->ceilingHeight && y <= sector->floorHeight)
			{
				const fixed16_16 sectorMaxX = sector->boundsMax.x;
				const fixed16_16 sectorMinX = sector->boundsMin.x;
				const fixed16_16 sectorMaxZ = sector->boundsMax.z;
				const fixed16_16 sectorMinZ = sector->boundsMin.z;

				const s32 dxInt = floor16(sectorMaxX - sectorMinX) + 1;
				const s32 dzInt = floor16(sectorMaxZ - sectorMinZ) + 1;
				sectorUnitArea = dzInt * dxInt;
				
				s32 insideBounds = 0;
				if (ix >= sectorMinX && ix <= sectorMaxX && iz >= sectorMinZ && iz <= sectorMaxZ)
				{
					// pick the containing sector with the smallest area.
					if (sectorUnitArea < prevSectorUnitArea && sector_pointInsideDF(sector, ix, iz))
					{
						prevSectorUnitArea = sectorUnitArea;
						foundSector = sector;
					}
				}
			}
		}

		return foundSector;
	}

	RSector* sector_which3D_Map(fixed16_16 dx, fixed16_16 dz, s32 layer)
	{
		fixed16_16 ix = dx;
		fixed16_16 iz = dz;

		RSector* sector = s_sectors;
		RSector* foundSector = nullptr;
		s32 sectorUnitArea = 0;
		s32 prevSectorUnitArea = INT_MAX;

		for (u32 i = 0; i < s_sectorCount; i++, sector++)
		{
			if (sector->layer == layer)
			{
				const fixed16_16 sectorMaxX = sector->boundsMax.x;
				const fixed16_16 sectorMinX = sector->boundsMin.x;
				const fixed16_16 sectorMaxZ = sector->boundsMax.z;
				const fixed16_16 sectorMinZ = sector->boundsMin.z;

				const s32 dxInt = floor16(sectorMaxX - sectorMinX) + 1;
				const s32 dzInt = floor16(sectorMaxZ - sectorMinZ) + 1;
				sectorUnitArea = dzInt * dxInt;

				s32 insideBounds = 0;
				if (ix >= sectorMinX && ix <= sectorMaxX && iz >= sectorMinZ && iz <= sectorMaxZ)
				{
					// pick the containing sector with the smallest area.
					if (sectorUnitArea < prevSectorUnitArea && sector_pointInsideDF(sector, ix, iz))
					{
						prevSectorUnitArea = sectorUnitArea;
						foundSector = sector;
					}
				}
			}
		}

		return foundSector;
	}

	enum PointSegSide
	{
		PS_INSIDE = -1,
		PS_ON_LINE = 0,
		PS_OUTSIDE = 1
	};

	PointSegSide lineSegmentSide(fixed16_16 x, fixed16_16 z, fixed16_16 x0, fixed16_16 z0, fixed16_16 x1, fixed16_16 z1)
	{
		fixed16_16 dx = x0 - x1;
		fixed16_16 dz = z0 - z1;
		if (dx == 0)
		{
			if (dz > 0)
			{
				if (z < z1 || z > z0 || x > x0) { return PS_INSIDE; }
			}
			else
			{
				if (z < z0 || z > z1 || x > x0) { return PS_INSIDE; }
			}
			return (x == x0) ? PS_ON_LINE : PS_OUTSIDE;
		}
		else if (dz == 0)
		{
			if (z != z0)
			{
				// I believe this should be -1 or +1 depending on if z is less than or greater than z0.
				// Otherwise flat lines always give the same answer.
				return PS_INSIDE;
			}
			if (dx > 0)
			{
				return (x > x0) ? PS_INSIDE : (x < x1) ? PS_OUTSIDE : PS_ON_LINE;
			}
			return (x > x1) ? PS_INSIDE : (x < x0) ? PS_OUTSIDE : PS_ON_LINE;
		}
		else if (dx > 0)
		{
			if (x > x0) { return PS_INSIDE; }

			x -= x1;
			if (dz > 0)
			{
				if (z < z1 || z > z0) { return PS_INSIDE; }
				z -= z1;
			}
			else
			{
				if (z < z0 || z > z1) { return PS_INSIDE; }
				dz = -dz;
				z1 -= z;
				z = z1;
			}
		}
		else // dx <= 0
		{
			if (x > x1) { return PS_INSIDE; }

			x -= x0;
			dx = -dx;
			if (dz > 0)
			{
				if (z < z1 || z > z0) { return PS_INSIDE; }
				z0 -= z;
				z = z0;
			}
			else  // dz <= 0
			{
				if (z < z0 || z > z1) { return PS_INSIDE; }
				dz = -dz;
				z -= z0;
			}
		}
		fixed16_16 zDx = mul16(z, dx);
		fixed16_16 xDz = mul16(x, dz);
		if (xDz == zDx)
		{
			return PS_ON_LINE;
		}
		return (xDz > zDx) ? PS_INSIDE : PS_OUTSIDE;
	}

	// The original DF algorithm.
	JBool sector_pointInsideDF(RSector* sector, fixed16_16 x, fixed16_16 z)
	{
		const fixed16_16 xFrac = fract16(x);
		const fixed16_16 zFrac = fract16(z);
		const s32 xInt = floor16(x);
		const s32 zInt = floor16(z);
		const s32 wallCount = sector->wallCount;

		RWall* wall = sector->walls;
		RWall* last = &sector->walls[wallCount - 1];
		vec2_fixed* w1 = last->w1;
		vec2_fixed* w0 = last->w0;
		fixed16_16 dzLast = w1->z - w0->z;
		s32 crossings = 0;

		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			w0 = wall->w0;
			w1 = wall->w1;

			fixed16_16 x0 = w0->x;
			fixed16_16 x1 = w1->x;
			fixed16_16 z0 = w0->z;
			fixed16_16 z1 = w1->z;
			fixed16_16 dz = z1 - z0;

			if (dz != 0)
			{
				if (z == z0 && x == x0)
				{
					TFE_System::logWrite(LOG_ERROR, "Sector", "Sector_Which3D: Object at (%d.%d, %d.%d) lies on a vertex of Sector #%d", xInt, xFrac, zInt, zFrac, sector->id);
					return JTRUE;
				}
				else if (z != z0)
				{
					if (z != z1)
					{
						PointSegSide side = lineSegmentSide(x, z, x0, z0, x1, z1);
						if (side == PS_OUTSIDE)
						{
							crossings++;
						}
						else if (side == PS_ON_LINE)
						{
							TFE_System::logWrite(LOG_ERROR, "Sector", "Sector_Which3D: Object at (%d.%d, %d.%d) lies on wall of Sector #%d", xInt, xFrac, zInt, zFrac, sector->id);
							return JTRUE;
						}
					}
					dzLast = dz;
				}
				else if (x != x0)
				{
					if (x < x0)
					{
						fixed16_16 dzSignMatches = dz ^ dzLast;	// dzSignMatches >= 0 if dz and dz0 have the same sign.
						if (dzSignMatches >= 0 || dzLast == 0)  // the signs match OR dz or dz0 are positive OR dz0 EQUALS 0.
						{
							crossings++;
						}
					}
					dzLast = dz;
				}
			}
			else if (lineSegmentSide(x, z, x0, z0, x1, z1) == PS_ON_LINE)
			{
				TFE_System::logWrite(LOG_ERROR, "Sector", "Sector_Which3D: Object at (%d.%d, %d.%d) lies on wall of Sector #%d", xInt, xFrac, zInt, zFrac, sector->id);
				return JTRUE;
			}
		}

		return (crossings & 1) ? JTRUE : JFALSE;
	}

	// Uses the "Winding Number" test for a point in polygon.
	// The point is considered inside if the winding number is greater than 0.
	// Note that this is different than DF's "crossing" algorithm.
	bool sector_pointInside(RSector* sector, fixed16_16 x, fixed16_16 z)
	{
		RWall* wall = sector->walls;
		s32 wallCount = sector->wallCount;
		s32 wn = 0;

		const Vec2f point = { fixed16ToFloat(x), fixed16ToFloat(z) };
		for (s32 w = 0; w < wallCount; w++, wall++)
		{
			vec2_fixed* w1 = wall->w0;
			vec2_fixed* w0 = wall->w1;

			Vec2f p0 = { fixed16ToFloat(w0->x), fixed16ToFloat(w0->z) };
			Vec2f p1 = { fixed16ToFloat(w1->x), fixed16ToFloat(w1->z) };

			if (p0.z <= z)
			{
				// Upward crossing, if the point is left of the edge than it intersects.
				if (p1.z > z && isLeft(p0, p1, point) > 0)
				{
					wn++;
				}
			}
			else
			{
				// Downward crossing, if point is right of the edge it intersects.
				if (p1.z <= z && isLeft(p0, p1, point) < 0)
				{
					wn--;
				}
			}
		}

		// The point is only outside if the winding number is less than or equal to 0.
		return wn > 0;
	}

	JBool sector_rotatingWallCollidesWithPlayer(RWall* wall, fixed16_16 cosAngle, fixed16_16 sinAngle, fixed16_16 centerX, fixed16_16 centerZ)
	{
		fixed16_16 objSide0;
		SecObject* player = s_playerObject;
		JBool overlap0 = sector_objOverlapsWall(wall, player, &objSide0);

		fixed16_16 localX = wall->worldPos0.x - centerX;
		fixed16_16 localZ = wall->worldPos0.z - centerZ;

		fixed16_16 dirZ = wall->wallDir.z;
		fixed16_16 dirX = wall->wallDir.x;
		fixed16_16 prevX = wall->w0->x;
		fixed16_16 prevZ = wall->w0->z;

		wall->w0->x = mul16(localX, cosAngle) - mul16(localZ, sinAngle) + centerX;
		wall->w0->z = mul16(localX, sinAngle) + mul16(localZ, cosAngle) + centerZ;

		vec2_fixed* w0 = wall->w0;
		vec2_fixed* w1 = wall->w1;
		fixed16_16 dx = w1->x - w0->x;
		fixed16_16 dz = w1->z - w0->z;
		computeDirAndLength(dx, dz, &wall->wallDir.x, &wall->wallDir.z);

		s32 objSide1;
		JBool overlap1 = sector_objOverlapsWall(wall, player, &objSide1);

		// Restore
		wall->wallDir.x = dirX;
		wall->wallDir.z = dirZ;
		wall->w0->x = prevX;
		wall->w0->z = prevZ;

		// Test the results.
		JBool canRotateWalls = JFALSE;
		if (!overlap1)	// no overlap.
		{
			if (objSide0 == 0 || objSide1 == 0 || objSide0 == objSide1)
			{
				return JFALSE;
			}
		}
		return JTRUE;
	}

	// Returns JTRUE if the walls can rotate.
	JBool sector_canRotateWalls(RSector* sector, angle14_32 angle, fixed16_16 centerX, fixed16_16 centerZ)
	{
		fixed16_16 cosAngle, sinAngle;
		sinCosFixed(angle, &sinAngle, &cosAngle);

		// Loop through the walls and determine if the player is in the sector of any affected wall.
		s32 wallCount = sector->wallCount;
		RWall* wall = sector->walls;
		JBool playerCollides = JFALSE;
		for (s32 i = 0; i < sector->wallCount; i++, wall++)
		{
			if (wall->flags1 & WF1_WALL_MORPHS)
			{
				if (wall->sector->flags1 & SEC_FLAGS1_PLAYER)
				{
					playerCollides = JTRUE;
					break;
				}
				RWall* mirror = wall->mirrorWall;
				if (mirror && (mirror->flags1 & WF1_WALL_MORPHS))
				{
					if (mirror->sector->flags1 & SEC_FLAGS1_PLAYER)
					{
						playerCollides = JTRUE;
						break;
					}
				}
			}
		}

		if (playerCollides)	// really this says that the player *might* collide and then gets definite here.
		{
			playerCollides = JFALSE;
			wallCount = sector->wallCount;
			wall = sector->walls;

			for (s32 i = 0; i < wallCount; i++, wall++)
			{
				if (wall->flags1 & WF1_WALL_MORPHS)
				{
					playerCollides |= sector_rotatingWallCollidesWithPlayer(wall, cosAngle, sinAngle, centerX, centerZ);
					RWall* mirror = wall->mirrorWall;
					if (mirror && (mirror->flags1 & WF1_WALL_MORPHS))
					{
						playerCollides |= sector_rotatingWallCollidesWithPlayer(mirror, cosAngle, sinAngle, centerX, centerZ);
					}
				}
			}
		}
		return playerCollides ? JFALSE : JTRUE;
	}

	void sector_rotateWall(RWall* wall, fixed16_16 cosAngle, fixed16_16 sinAngle, fixed16_16 centerX, fixed16_16 centerZ)
	{
		fixed16_16 x0 = wall->worldPos0.x - centerX;
		fixed16_16 z0 = wall->worldPos0.z - centerZ;
		wall->w0->x = mul16(x0, cosAngle) - mul16(z0, sinAngle) + centerX;
		wall->w0->z = mul16(x0, sinAngle) + mul16(z0, cosAngle) + centerZ;

		vec2_fixed* w1 = wall->w1;
		vec2_fixed* w0 = wall->w0;
		fixed16_16 dx = w1->x - w0->x;
		fixed16_16 dz = w1->z - w0->z;
		wall->length = computeDirAndLength(dx, dz, &wall->wallDir.x, &wall->wallDir.z);
		wall->angle = vec2ToAngle(dx, dz);

		RSector* sector = wall->sector;
		if (sector->flags1 & SEC_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}

		RWall* prevWall;
		if (wall->id == 0)
		{
			prevWall = wall + (sector->wallCount - 1);
		}
		else
		{
			prevWall = wall - 1;
		}

		w0 = prevWall->w0;
		w1 = prevWall->w1;
		dx = w1->x - w0->x;
		dz = w1->z - w0->z;
		prevWall->length = computeDirAndLength(dx, dz, &prevWall->wallDir.x, &prevWall->wallDir.z);
		prevWall->angle = vec2ToAngle(dx, dz);

		sector = prevWall->sector;
		if (sector->flags1 & SEC_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}
	}

	void sector_rotateWalls(RSector* sector, fixed16_16 centerX, fixed16_16 centerZ, angle14_32 angle)
	{
		s32 cosAngle, sinAngle;
		sinCosFixed(angle, &sinAngle, &cosAngle);

		s32 wallCount = sector->wallCount;
		RWall* wall = sector->walls;
		for (s32 i = 0; i < wallCount; i++, wall++)
		{
			if (wall->flags1 & WF1_WALL_MORPHS)
			{
				sector_rotateWall(wall, cosAngle, sinAngle, centerX, centerZ);

				RWall* mirror = wall->mirrorWall;
				if (mirror && (mirror->flags1 & WF1_WALL_MORPHS))
				{
					sector_rotateWall(mirror, cosAngle, sinAngle, centerX, centerZ);
				}
			}
		}
		sector_computeBounds(sector);
	}

	void sector_rotateObj(SecObject* obj, angle14_32 deltaAngle, fixed16_16 cosdAngle, fixed16_16 sindAngle, fixed16_16 centerX, fixed16_16 centerZ)
	{
		if (obj->flags & OBJ_FLAG_HAS_COLLISION)
		{
			if (obj->entityFlags & ETFLAG_PLAYER)
			{
				s_playerYaw -= deltaAngle;
			}
			else
			{
				obj->yaw -= deltaAngle;
			}

			fixed16_16 relX = obj->posWS.x - centerX;
			fixed16_16 relZ = obj->posWS.z - centerZ;

			fixed16_16 offsetY = 0;
			fixed16_16 offsetX = mul16(relX, cosdAngle) - mul16(relZ, sindAngle) + centerX - obj->posWS.x;
			fixed16_16 yPos = FIXED(9999);
			fixed16_16 offsetZ = mul16(relX, sindAngle) + mul16(relZ, cosdAngle) + centerZ - obj->posWS.z;
			fixed16_16 height = obj->worldHeight + HALF_16;

			CollisionInfo info =
			{
				obj,
				offsetX, offsetY, offsetZ,
				0, yPos, height,
				0, 0, 0,	// to be filled in later.
				obj->worldWidth,
			};
			handleCollision(&info);

			// Collision reponse with a single iteration.
			if (info.responseStep && info.wall)
			{
				handleCollisionResponseSimple(info.responseDir.x, info.responseDir.z, &info.offsetX, &info.offsetZ);
				handleCollision(&info);
			}

			RSector* finalSector = sector_which3D(obj->posWS.x, obj->posWS.y, obj->posWS.z);
			if (finalSector)
			{
				// Adds the object to the new sector and removes it from the previous.
				sector_addObject(finalSector, obj);
			}
			if (obj->type == OBJ_TYPE_3D)
			{
				obj3d_computeTransform(obj);
			}
		}
	}

	void sector_rotateObjects(RSector* sector, angle14_32 deltaAngle, fixed16_16 centerX, fixed16_16 centerZ, u32 flags)
	{
		fixed16_16 cosdAngle, sindAngle;
		sinCosFixed(deltaAngle, &sindAngle, &cosdAngle);
		JBool moveCeil = JFALSE;
		JBool moveSecHgt = JFALSE;
		JBool moveFloor = JFALSE;
		if (flags & INF_EFLAG_MOVE_FLOOR)
		{
			moveFloor = JTRUE;
		}
		if (flags & INF_EFLAG_MOVE_SECHT)
		{
			moveSecHgt = JTRUE;
		}
		if (flags & INF_EFLAG_MOVE_CEIL)
		{
			moveCeil = JTRUE;
		}

		SecObject** objList = sector->objectList;
		s32 objCount = sector->objectCount;
		for (s32 i = 0; i < objCount; i++, objList++)
		{
			SecObject* obj = *objList;
			while (!obj)
			{
				objList++;
				obj = *objList;
			}
			// The first 3 conditionals can be collapsed since the resulting values are the same.
			if ((moveFloor && obj->posWS.y == sector->floorHeight) ||
				(moveSecHgt && sector->secHeight && sector->floorHeight + sector->secHeight == obj->posWS.y) ||
				(moveCeil && sector->ceilingHeight == obj->posWS.y))
			{
				sector_rotateObj(obj, deltaAngle, cosdAngle, sindAngle, centerX, centerZ);
			}
		}
	}

	//////////////////////////////////////////////////////////
	// Internal
	//////////////////////////////////////////////////////////
	void sector_computeWallDirAndLength(RWall* wall)
	{
		vec2_fixed* w0 = wall->w0;
		vec2_fixed* w1 = wall->w1;
		fixed16_16 dx = w1->x - w0->x;
		fixed16_16 dz = w1->z - w0->z;
		wall->length = computeDirAndLength(dx, dz, &wall->wallDir.x, &wall->wallDir.z);
		wall->angle = vec2ToAngle(dx, dz);
	}

	void sector_moveWallVertex(RWall* wall, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
		// Offset vertex 0.
		wall->w0->x += offsetX;
		wall->w0->z += offsetZ;
		// Update the wall direction and length.
		sector_computeWallDirAndLength(wall);

		// Set the appropriate game value if the player is inside the sector.
		RSector* sector = wall->sector;
		if (sector->flags1 & SEC_FLAGS1_PLAYER)
		{
			s_playerSecMoved = JTRUE;
		}

		// Update the previous wall, since it would have changed as well.
		RWall* nextWall = nullptr;
		if (wall->id == 0)
		{
			s32 last = sector->wallCount - 1;
			nextWall = &sector->walls[last];
		}
		else
		{
			nextWall = wall - 1;
		}
		// Compute the wall direction and length of the previous wall.
		sector_computeWallDirAndLength(nextWall);
	}

	// returns 0 if the object does NOT overlap, otherwise non-zero.
	// objSide: 0 = no overlap, -1/+1 front or behind.
	JBool sector_objOverlapsWall(RWall* wall, SecObject* obj, s32* objSide)
	{
		fixed16_16 halfWidth = (obj->worldWidth - SIGN_BIT(obj->worldWidth)) >> 1;
		fixed16_16 quarterWidth = (obj->worldWidth - SIGN_BIT(obj->worldWidth)) >> 2;
		fixed16_16 threeQuartWidth = halfWidth + quarterWidth;
		*objSide = 0;

		RSector* next = wall->nextSector;
		if (next)
		{
			RSector* sector = wall->sector;
			if (obj->posWS.y <= sector->floorHeight)
			{
				fixed16_16 objTop = obj->posWS.y - obj->worldHeight;
				if (objTop > sector->ceilingHeight)
				{
					return 0;
				}
			}
		}

		vec2_fixed* w0 = wall->w0;
		fixed16_16 x0 = w0->x;
		fixed16_16 z0 = w0->z;
		fixed16_16 dirX = wall->wallDir.z;
		fixed16_16 dirZ = wall->wallDir.x;
		fixed16_16 len = wall->length;
		fixed16_16 dx = obj->posWS.x - x0;
		fixed16_16 dz = obj->posWS.z - z0;

		fixed16_16 proj = mul16(dx, dirZ) + mul16(dz, dirX);
		fixed16_16 maxS = threeQuartWidth * 2 + len;	// 1.5 * width + length
		fixed16_16 s = threeQuartWidth + proj;
		if (s <= maxS)
		{
			s32 diff = mul16(dx, dirX) - mul16(dz, dirZ);
			s32 side = (diff >= 0) ? 1 : -1;

			*objSide = side;
			if (side < 0)
			{
				diff = -diff;
			}
			if (diff <= threeQuartWidth)
			{
				return JTRUE;
			}
		}
		return JFALSE;
	}

	// returns 0 if the wall is free to move, else non-zero.
	JBool sector_canWallMove(RWall* wall, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
		s32 objSide0;
		// Test the initial position, the code assumes there is no collision at this point.
		sector_objOverlapsWall(wall, s_playerObject, &objSide0);

		vec2_fixed* w0 = wall->w0;
		fixed16_16 z0 = w0->z;
		fixed16_16 x0 = w0->x;
		w0->x += offsetX;
		w0->z += offsetZ;

		s32 objSide1;
		// Then move the wall and test the new position.
		JBool col = sector_objOverlapsWall(wall, s_playerObject, &objSide1);

		// Restore the wall.
		w0->x = x0;
		w0->z = z0;

		if (!col)
		{
			if (objSide0 == 0 || objSide1 == 0 || objSide0 == objSide1)
			{
				return JFALSE;
			}
		}
		return JTRUE;
	}

	void sector_getFloorAndCeilHeight(RSector* sector, fixed16_16* floorHeight, fixed16_16* ceilHeight)
	{
		*floorHeight = sector->floorHeight;
		*ceilHeight = sector->ceilingHeight;

		if (sector->flags1 & SEC_FLAGS1_PIT)
		{
			*floorHeight += SEC_SKY_HEIGHT;
		}
		if (sector->flags1 & SEC_FLAGS1_EXTERIOR)
		{
			*ceilHeight -= SEC_SKY_HEIGHT;
		}
	}

	void sector_getObjFloorAndCeilHeight(RSector* sector, fixed16_16 y, fixed16_16* floorHeight, fixed16_16* ceilHeight)
	{
		fixed16_16 secHeight = sector->secHeight;
		fixed16_16 bottom = y - FIXED(2);
		if (secHeight < 0)
		{
			fixed16_16 height = sector->floorHeight + sector->secHeight;
			if (bottom <= height)
			{
				*floorHeight = height;
				*ceilHeight = sector->ceilingHeight;
			}
			else
			{
				*floorHeight = sector->floorHeight;
				*ceilHeight = height;
			}
		}
		else // secHeight >= 0
		{
			*floorHeight = sector->floorHeight + sector->secHeight;
			*ceilHeight = sector->ceilingHeight;
		}
		if (sector->flags1 & SEC_FLAGS1_PIT)
		{
			*floorHeight += SEC_SKY_HEIGHT;
		}
		if (sector->flags1 & SEC_FLAGS1_EXTERIOR)
		{
			*ceilHeight -= SEC_SKY_HEIGHT;
		}
	}
		
	void sector_moveObjects(RSector* sector, u32 flags, fixed16_16 offsetX, fixed16_16 offsetZ)
	{
		// TODO(Core Game Loop Release)
		// As far as I can tell, no objects are actually affected in-game by this.
		// So I'm going to leave this empty for now and look deeper into it later once I have 
		// more information.
	}
		
	// Tests if a point (p2) is to the left, on or right of an infinite line (p0 -> p1).
	// Return: >0 p2 is on the left of the line.
	//         =0 p2 is on the line.
	//         <0 p2 is on the right of the line.
	f32 isLeft(Vec2f p0, Vec2f p1, Vec2f p2)
	{
		return (p1.x - p0.x) * (p2.z - p0.z) - (p2.x - p0.x) * (p1.z - p0.z);
	}
} // namespace TFE_Jedi