/*
 * MapRendererManaAura.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/CGObjectInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "IMapRendererContext.h"
#include "MapRendererManaAura.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include <cmath>

void MapRendererManaAura::load()
{
	loaded = true;
	try
	{
		settings = ManaAuraVisualSettings::fromJson(JsonNode(JsonPath::builtin("config/manaAuraVisuals.json")));
		if(settings.enabled)
			material = ManaAuraMaterial::fromJson(JsonNode(JsonPath::builtin("config/manaAuraMaterial.json")));
	}
	catch(const std::exception & error)
	{
		logGlobal->error("MGMA visuals: config/manaAuraVisuals.json or config/manaAuraMaterial.json invalid: %s; disabling visual layer", error.what());
		settings.enabled = false;
	}
}

void MapRendererManaAura::prepareFrame(
	IMapRendererContext & context,
	const Rect & visible,
	int newLevel,
	const std::function<std::vector<ColorRGBA>(const int3 &)> & routePixels
)
{
	const auto view = context.manaAuraVisualState();
	if(!loaded && !view.sources.empty())
		load();
	if(!settings.enabled || view.sources.empty() || visible.w <= 0 || visible.h <= 0)
	{
		field.reset();
		images.clear();
		imageOrder.clear();
		frame = -1;
		stamp = 0;
		return;
	}
	const auto mapSize = context.getMapSize();
	std::vector<int3> current;
	int minX = mapSize.x, minY = mapSize.y, maxX = 0, maxY = 0;
	for(const auto & p : view.sources)
		if(p.z == newLevel)
		{
			current.push_back(p);
			minX = std::min(minX, p.x - view.radius - 2);
			minY = std::min(minY, p.y - view.radius - 2);
			maxX = std::max(maxX, p.x + view.radius + 3);
			maxY = std::max(maxY, p.y + view.radius + 3);
		}
	const int x0 = std::max({0, static_cast<int>(std::floor((visible.x - 2) / 8.)) * 8, minX});
	const int y0 = std::max({0, static_cast<int>(std::floor((visible.y - 2) / 8.)) * 8, minY});
	const int x1 = std::min({mapSize.x, static_cast<int>(std::ceil((visible.right() + 2) / 8.)) * 8, maxX});
	const int y1 = std::min({mapSize.y, static_cast<int>(std::ceil((visible.bottom() + 2) / 8.)) * 8, maxY});
	if(current.empty() || x1 <= x0 || y1 <= y0)
	{
		field.reset();
		images.clear();
		imageOrder.clear();
		frame = -1;
		stamp = 0;
		return;
	}
	Rect bounds(x0, y0, x1 - x0, y1 - y0);
	uint64_t routeHash = 1469598103934665603ULL, eligibleHash = routeHash;
	auto mix = [](uint64_t & hash, uint64_t value)
	{
		hash = (hash ^ value) * 1099511628211ULL;
	};
	std::vector<bool> allowed(bounds.w * bounds.h);
	for(int y = y0; y < y1; ++y)
		for(int x = x0; x < x1; ++x)
		{
			const int3 tile(x, y, newLevel);
			const auto & terrain = context.getMapTile(tile);
			mix(routeHash, terrain.getRoadID().getNum());
			mix(routeHash, terrain.roadDir);
			mix(routeHash, terrain.getRiverID().getNum());
			mix(routeHash, terrain.riverDir);
			mix(routeHash, terrain.extTileFlags);
			bool eligible = context.isVisible(tile) && x >= visible.x && y >= visible.y && x < visible.right() && y < visible.bottom();
			for(const auto & id : context.getObjects(tile))
			{
				const auto * object = context.getObject(id);
				if(object && !object->isTile2Terrain())
					eligible = false;
			}
			allowed[(y - y0) * bounds.w + x - x0] = eligible;
			mix(eligibleHash, eligible);
		}
	mix(routeHash, context.showRoads());
	mix(routeHash, context.showRivers());
	const bool rebuild =
		!field || !bounds.operator==(prepared) || sources != current || level != newLevel || radius != view.radius || routeHash != routeSignature;
	if(rebuild)
	{
		if(!field)
			field = std::make_unique<ManaAuraField>(settings, *material);
		std::vector<ManaAuraField::Tile> tiles(bounds.w * bounds.h);
		for(int y = y0; y < y1; ++y)
			for(int x = x0; x < x1; ++x)
			{
				auto & tile = tiles[(y - y0) * bounds.w + x - x0];
				tile.visible = allowed[(y - y0) * bounds.w + x - x0];
				tile.pulseAllowed = tile.visible;
				const auto & terrain = context.getMapTile(int3(x, y, newLevel));
				const bool roadAbove = y > 0 && context.getMapTile(int3(x, y - 1, newLevel)).hasRoad();
				if((context.showRoads() && (terrain.hasRoad() || roadAbove)) || (context.showRivers() && terrain.hasRiver()))
					tile.route = routePixels(int3(x, y, newLevel));
			}
		field->rebuild(bounds, mapSize, newLevel, current, view.radius, tiles);
		prepared = bounds;
		sources = current;
		level = newLevel;
		radius = view.radius;
		routeSignature = routeHash;
		++revision;
		if(settings.debug)
			logGlobal->info(
				"MGMA balanced: sources=%zu radius=%d visible=(%d,%d,%d,%d,%d) rasterStep=%d rate=%g duration=[%g,%g]",
				current.size(),
				radius,
				bounds.x,
				bounds.y,
				bounds.w,
				bounds.h,
				level,
				field->sampleStep(),
				settings.pulseRate,
				settings.pulseMinDuration,
				settings.pulseMaxDuration
			);
	}
	if(rebuild || eligibleHash != eligibilitySignature)
	{
		field->updateEligibility(allowed);
		eligibilitySignature = eligibleHash;
	}
	const int64_t currentFrame = view.animated ? static_cast<int64_t>(view.timeMs * settings.framesPerSecond / 1000) : 0;
	const uint32_t currentStamp = static_cast<uint32_t>(currentFrame) * 2654435761U + revision * 2246822519U + 1;
	if(frame != currentFrame || stamp != currentStamp)
	{
		field->update(currentFrame / settings.framesPerSecond, view.animated);
		frame = currentFrame;
		stamp = currentStamp;
	}
}

uint32_t MapRendererManaAura::checksum(const int3 & position) const
{
	return field && position.z == level && field->containsTile(position.x, position.y) ? (stamp ? stamp : 1) : 0;
}

void MapRendererManaAura::renderTile(Canvas & target, const int3 & position)
{
	if(!checksum(position))
		return;
	const int scale = target.getScalingFactor();
	if(scaling != scale)
	{
		scaling = scale;
		images.clear();
		imageOrder.clear();
	}
	constexpr int blockTiles = 4;
	const std::pair key{position.x / blockTiles, position.y / blockTiles};
	auto it = images.find(key);
	if(it == images.end())
	{
		constexpr size_t maxBlocks = 128;
		if(images.size() >= maxBlocks)
		{
			images.erase(imageOrder.front());
			imageOrder.pop_front();
		}
		it = images.emplace(key, Block{}).first;
		imageOrder.push_back(key);
	}
	auto & block = it->second;
	if(!block.image || block.stamp != stamp)
	{
		const int native = blockTiles * 32, physical = native * scale;
		if(!block.image)
			block.image = std::make_shared<CanvasImage>(Point(physical, physical), CanvasScalingPolicy::IGNORE);
		uploadPixels.resize(static_cast<size_t>(physical) * physical * 4);
		for(int y = 0; y < native; ++y)
		{
			auto * row = uploadPixels.data() + y * scale * physical * 4;
			for(int x = 0; x < native; ++x)
			{
				const auto colour = field->pixel(key.first * native + x, key.second * native + y);
				for(int dx = 0; dx < scale; ++dx)
				{
					auto * pixel = row + (x * scale + dx) * 4;
					pixel[0] = colour.r;
					pixel[1] = colour.g;
					pixel[2] = colour.b;
					pixel[3] = colour.a;
				}
			}
			for(int dy = 1; dy < scale; ++dy)
				std::memcpy(row + dy * physical * 4, row, physical * 4);
		}
		block.image->writePixelsRGBA(uploadPixels);
		block.stamp = stamp;
	}
	target.draw(block.image, Point(0, 0), Rect((position.x % blockTiles) * 32, (position.y % blockTiles) * 32, 32, 32));

	if(settings.debug)
		for(const auto & pulse : field->getPulses())
			target.drawBorder(
				Rect(
					pulse.x - settings.pulseRadius - position.x * 32,
					pulse.y - settings.pulseRadius - position.y * 32,
					settings.pulseRadius * 2,
					settings.pulseRadius * 2
				),
				ColorRGBA(210, 100, 210, 180)
			);
}
