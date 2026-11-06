/*
 * ManaAuraFrameProfile.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

namespace ManaAuraFrameProfile
{
struct MapSample
{
	int cameraX = 0, cameraY = 0, level = 0;
	int zoom = 0, viewWidth = 0, viewHeight = 0, scale = 0;
	int visibleTiles = 0, redrawn = 0;
	bool aura = false;
	double fieldMs = 0, compositionMs = 0, contactsMs = 0, tilesMs = 0;
};

void recordMapUpdate(MapSample sample);
}
