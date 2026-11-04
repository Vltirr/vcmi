/*
 * MapRendererManaAura.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "ManaAuraField.h"
#include <functional>

class IMapRendererContext;
class Canvas;
class CanvasImage;

class MapRendererManaAura
{
public:
	void prepareFrame(IMapRendererContext & context, const Rect & visible, int level, const std::function<std::vector<ColorRGBA>(const int3 &)> & routePixels);
	uint32_t checksum(const int3 & position) const;
	void renderTile(Canvas & target, const int3 & position);

private:
	bool loaded = false;
	ManaAuraVisualSettings settings;
	std::optional<ManaAuraMaterial> material;
	std::unique_ptr<ManaAuraField> field;
	Rect prepared;
	std::vector<int3> sources;
	int radius = 0, level = -1, scaling = 0;
	uint64_t routeSignature = 0, eligibilitySignature = 0;
	uint32_t revision = 0, stamp = 0;
	int64_t frame = -1;
	std::map<std::pair<int, int>, std::shared_ptr<CanvasImage>> images;
	std::list<std::pair<int, int>> imageOrder;

	void load();
};
