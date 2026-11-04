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
	bool active = false;
	ManaAuraVisualSettings settings;
	std::unique_ptr<ManaAuraFieldBuilder> builder;
	Rect requestedBounds;
	uint64_t requestedGeneration = 0;
	std::unique_ptr<ManaAuraField> field;
	Rect prepared;
	std::vector<int3> sources;
	int radius = 0, level = -1, scaling = 0;
	uint64_t routeSignature = 0, eligibilitySignature = 0;
	uint32_t revision = 0, stamp = 0;
	int64_t frame = -1;
	struct Block
	{
		std::shared_ptr<CanvasImage> image;
		uint32_t stamp = 0;
		std::list<std::pair<int, int>>::iterator order;
	};
	std::map<std::pair<int, int>, Block> images;
	std::vector<uint8_t> uploadPixels;
	struct RouteImage
	{
		uint64_t signature = 0;
		std::vector<ColorRGBA> pixels;
	};
	std::map<std::pair<int, int>, RouteImage> routeImages;
	std::list<std::pair<int, int>> routeOrder;
	std::vector<ColorRGBA> framePixels;
	Rect frameBounds;
	uint32_t frameStamp = 0;
	std::list<std::pair<int, int>> imageOrder;

	void load();
};
