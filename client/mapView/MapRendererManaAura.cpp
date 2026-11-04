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
	}
	catch(const std::exception & error)
	{
		logGlobal->error("MGMA visuals: config/manaAuraVisuals.json invalid: %s; disabling visual layer", error.what());
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
	active = settings.enabled && !view.sources.empty() && visible.w > 0 && visible.h > 0;
	if(!active)
		return;

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
	const bool sameSources = sources == current && level == newLevel && radius == view.radius;
	const int left = std::max(0, minX), top = std::max(0, minY);
	const int right = std::min(mapSize.x, maxX), bottom = std::min(mapSize.y, maxY);
	Rect coverage(left, top, std::max(0, right - left), std::max(0, bottom - top));
	Rect bounds = ManaAuraFieldBuilder::region(visible, coverage, sameSources ? requestedBounds : Rect(0, 0, 0, 0), settings.maxRasterPixels);
	if(current.empty() || !sameSources)
	{
		if(builder)
		{
			builder->cancel();
			builder->retire(std::move(field));
		}
		requestedGeneration = 0;
		images.clear();
		imageOrder.clear();
		frame = -1;
		stamp = 0;
	}
	if(current.empty() || bounds.w <= 0 || bounds.h <= 0)
		return;
	const int x0 = bounds.x, y0 = bounds.y, x1 = bounds.right(), y1 = bounds.bottom();

	uint64_t routeHash = 1469598103934665603ULL, eligibleHash = routeHash;
	auto mix = [](uint64_t & hash, uint64_t value)
	{
		hash = (hash ^ value) * 1099511628211ULL;
	};
	auto canPulse = [&](int x, int y)
	{
		if(x < visible.x || y < visible.y || x >= visible.right() || y >= visible.bottom())
			return false;
		const int3 tile(x, y, newLevel);
		if(!context.isVisible(tile))
			return false;
		for(const auto & id : context.getObjects(tile))
			if(const auto * object = context.getObject(id); object && !object->isTile2Terrain())
				return false;
		return true;
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
			const bool eligible = canPulse(x, y);
			allowed[(y - y0) * bounds.w + x - x0] = eligible;
			mix(eligibleHash, eligible);
		}
	mix(routeHash, context.showRoads());
	mix(routeHash, context.showRivers());
	const bool rebuild = !requestedGeneration || !bounds.operator==(requestedBounds) || !sameSources || routeHash != routeSignature;
	bool adopted = false;
	if(builder)
		if(auto ready = builder->take())
		{
			if(ready->generation == requestedGeneration)
			{
				if(ready->error)
				{
					try
					{
						std::rethrow_exception(ready->error);
					}
					catch(const std::runtime_error & error)
					{
						logGlobal->error("MGMA visual material preparation failed: %s; disabling visual layer", error.what());
						settings.enabled = false;
						builder->cancel();
						builder->retire(std::move(field));
						images.clear();
						imageOrder.clear();
						stamp = 0;
						return;
					}
				}
				if(field)
					ready->field->inheritAnimation(*field);
				builder->retire(std::move(field));
				field = std::move(ready->field);
				prepared = ready->bounds;
				if(settings.debug)
					logGlobal->info(
						"MGMA ready generation=%llu backgroundMs=%g rasterStep=%d",
						static_cast<unsigned long long>(ready->generation),
						ready->preparationMs,
						field->sampleStep()
					);
				eligibilitySignature = 0;
				++revision;
				adopted = true;
			}
			else
				builder->retire(std::move(ready->field));
		}
	if(rebuild)
	{
		if(!builder)
			builder = std::make_unique<ManaAuraFieldBuilder>(settings, nullptr);
		if(!sameSources)
		{
			builder->retire(std::move(field));
			images.clear();
			imageOrder.clear();
			stamp = 0;
		}
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
		requestedGeneration = builder->request({bounds, mapSize, newLevel, view.radius, current, std::move(tiles)});
		requestedBounds = bounds;
		if(settings.debug)
			logGlobal->info(
				"MGMA queued generation=%llu bounds=(%d,%d,%d,%d) level=%d",
				static_cast<unsigned long long>(requestedGeneration),
				bounds.x,
				bounds.y,
				bounds.w,
				bounds.h,
				newLevel
			);
		sources = current;
		level = newLevel;
		radius = view.radius;
		routeSignature = routeHash;
	}

	if(!field)
		return;
	if(!bounds.operator==(prepared))
	{
		allowed.assign(prepared.w * prepared.h, false);
		eligibleHash = 1469598103934665603ULL;
		for(int y = prepared.y; y < prepared.bottom(); ++y)
			for(int x = prepared.x; x < prepared.right(); ++x)
			{
				const bool eligible = canPulse(x, y);
				allowed[(y - prepared.y) * prepared.w + x - prepared.x] = eligible;
				mix(eligibleHash, eligible);
			}
	}
	if(adopted || eligibleHash != eligibilitySignature)
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
	return active && field && position.z == level && field->containsTile(position.x, position.y) ? (stamp ? stamp : 1) : 0;
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
