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
#include "render/IImage.h"
#include <chrono>
#include <cmath>

void MapRendererManaAura::load()
{
	loaded = true;
	try
	{
		settings = ManaAuraVisualSettings::fromJson(JsonNode(JsonPath::builtin("config/manaAuraVisuals.json")));
		logGlobal->info(
			"MGMA contact: routes=%d presence=%g transitionPx=%g objects=%d presence=%g",
			settings.routeContact,
			settings.routePresence,
			settings.routeTransition,
			settings.objectContact,
			settings.objectPresence
		);
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
		routeImages.clear();
		routeOrder.clear();
		images.clear();
		imageOrder.clear();
		frame = -1;
		stamp = 0;
		frameStamp = 0;
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
			const bool eligible = canPulse(x, y);
			allowed[(y - y0) * bounds.w + x - x0] = eligible;
			mix(eligibleHash, eligible);
		}
	const int rx = std::max(0, visible.x - 1), ry = std::max(0, visible.y - 1);
	const Rect routeBounds(rx, ry, std::max(0, std::min(mapSize.x, visible.right() + 1) - rx), std::max(0, std::min(mapSize.y, visible.bottom() + 1) - ry));
	mix(routeHash, routeBounds.x);
	mix(routeHash, routeBounds.y);
	mix(routeHash, routeBounds.w);
	mix(routeHash, routeBounds.h);
	for(int y = std::max(0, routeBounds.y - 1); y < std::min(mapSize.y, routeBounds.bottom() + 1); ++y)
		for(int x = std::max(0, routeBounds.x - 1); x < std::min(mapSize.x, routeBounds.right() + 1); ++x)
		{
			const auto & terrain = context.getMapTile(int3(x, y, newLevel));
			mix(routeHash, terrain.getRoadID().getNum());
			mix(routeHash, terrain.roadDir);
			mix(routeHash, terrain.getRiverID().getNum());
			mix(routeHash, terrain.riverDir);
			mix(routeHash, terrain.extTileFlags);
		}
	mix(routeHash, context.showRoads());
	mix(routeHash, context.showRivers());
	const bool rebuild = !requestedGeneration || !bounds.operator==(requestedBounds) || !sameSources;
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
						frameStamp = 0;
						return;
					}
				}
				if(field)
					ready->field->inheritAnimation(*field);
				builder->retire(std::move(field));
				field = std::move(ready->field);
				prepared = ready->bounds;
				logGlobal->info(
					"MGMA ready generation=%llu backgroundMs=%g controlStep=%d",
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
			frameStamp = 0;
		}
		std::vector<ManaAuraField::Tile> tiles(bounds.w * bounds.h);
		for(int y = y0; y < y1; ++y)
			for(int x = x0; x < x1; ++x)
			{
				auto & tile = tiles[(y - y0) * bounds.w + x - x0];
				tile.visible = allowed[(y - y0) * bounds.w + x - x0];
				tile.pulseAllowed = tile.visible;
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
	}

	if(!field)
		return;
	const bool routesChanged = adopted || routeSignature != routeHash;
	if(routesChanged)
	{
		std::vector<ManaAuraField::Tile> routeTiles(routeBounds.w * routeBounds.h);
		for(int y = routeBounds.y; y < routeBounds.bottom(); ++y)
			for(int x = routeBounds.x; x < routeBounds.right(); ++x)
			{
				const auto & terrain = context.getMapTile(int3(x, y, newLevel));
				const bool roadAbove = y > 0 && context.getMapTile(int3(x, y - 1, newLevel)).hasRoad();
				if((context.showRoads() && (terrain.hasRoad() || roadAbove)) || (context.showRivers() && terrain.hasRiver()))
				{
					uint64_t signature = 1469598103934665603ULL;
					for(int yy = std::max(0, y - 1); yy <= std::min(mapSize.y - 1, y + 1); ++yy)
						for(int xx = std::max(0, x - 1); xx <= std::min(mapSize.x - 1, x + 1); ++xx)
						{
							const auto & neighbour = context.getMapTile(int3(xx, yy, newLevel));
							mix(signature, neighbour.getRoadID().getNum());
							mix(signature, neighbour.roadDir);
							mix(signature, neighbour.getRiverID().getNum());
							mix(signature, neighbour.riverDir);
							mix(signature, neighbour.extTileFlags);
						}
					mix(signature, context.showRoads());
					mix(signature, context.showRivers());
					const std::pair key{x, y};
					auto it = routeImages.find(key);
					if(it == routeImages.end())
					{
						if(routeImages.size() >= 2048)
						{
							routeImages.erase(routeOrder.front());
							routeOrder.pop_front();
						}
						it = routeImages.emplace(key, RouteImage{}).first;
						routeOrder.push_back(key);
					}
					if(it->second.signature != signature || it->second.pixels.empty())
					{
						it->second.pixels = routePixels(int3(x, y, newLevel));
						it->second.signature = signature;
					}
					routeTiles[(y - routeBounds.y) * routeBounds.w + x - routeBounds.x].route = it->second.pixels;
				}
			}
		field->updateRoutes(routeBounds, routeTiles);
		routeSignature = routeHash;
		++revision;
	}

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
	if(adopted || routesChanged || eligibleHash != eligibilitySignature)
	{
		field->updateEligibility(allowed);
		eligibilitySignature = eligibleHash;
	}

	bool contributes = false;
	for(int y = std::max(0, visible.y); y < std::min(mapSize.y, visible.bottom()) && !contributes; ++y)
		for(int x = std::max(0, visible.x); x < std::min(mapSize.x, visible.right()) && !contributes; ++x)
			contributes = field->containsTile(x, y);
	if(!contributes)
	{
		active = false;
		return;
	}

	const int64_t currentFrame = view.animated ? static_cast<int64_t>(view.timeMs * settings.framesPerSecond / 1000) : 0;
	const uint32_t currentStamp = static_cast<uint32_t>(currentFrame) * 2654435761U + revision * 2246822519U + 1;
	if(frame != currentFrame || stamp != currentStamp)
	{
		field->update(currentFrame / settings.framesPerSecond, view.animated);
		frame = currentFrame;
		stamp = currentStamp;
	}
	constexpr int block = 128;
	const int bx = std::max(0, static_cast<int>(std::floor(visible.x / 4.)) * block);
	const int by = std::max(0, static_cast<int>(std::floor(visible.y / 4.)) * block);
	const int rightPixels = static_cast<int>(std::ceil(std::min(mapSize.x, visible.right()) / 4.)) * block;
	const int bottomPixels = static_cast<int>(std::ceil(std::min(mapSize.y, visible.bottom()) / 4.)) * block;
	const Rect nextFrame(bx, by, std::max(0, rightPixels - bx), std::max(0, bottomPixels - by));
	if(frameStamp != stamp || !frameBounds.operator==(nextFrame))
	{
		framePixels.resize(static_cast<size_t>(nextFrame.w) * nextFrame.h);
		const auto started = std::chrono::steady_clock::now();
		field->writePixels(nextFrame, framePixels);
		if(adopted)
			logGlobal->info(
				"MGMA first native composition: pixels=%llu cpuMs=%g",
				static_cast<unsigned long long>(framePixels.size()),
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
			);
		const size_t maxBlocks = std::clamp(static_cast<size_t>(nextFrame.w / 128) * (nextFrame.h / 128), size_t{128}, size_t{1024});
		while(images.size() > maxBlocks)
		{
			images.erase(imageOrder.front());
			imageOrder.pop_front();
		}
		frameBounds = nextFrame;
		frameStamp = stamp;
	}
}

bool MapRendererManaAura::coversObject(const CGObjectInstance & object) const
{
	const auto anchor = object.isVisitable() ? object.visitablePos() : object.anchorPos();
	return anchor.z == level && field->covered(anchor.x, anchor.y);
}

bool MapRendererManaAura::shadowsBelowMist(const int3 & position) const
{
	return settings.objectContact && settings.objectPresence > 0 && checksum(position);
}

uint32_t MapRendererManaAura::checksum(const int3 & position, const IMapRendererContext * context) const
{
	if(!active || !field || position.z != level)
		return 0;
	if(field->containsTile(position.x, position.y))
		return stamp ? stamp : 1;
	if(settings.objectContact && settings.objectPresence > 0 && context)
		for(const auto & id : context->getObjects(position))
			if(const auto * object = context->getObject(id); object && coversObject(*object))
				return stamp ? stamp : 1;
	return 0;
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
		const size_t maxBlocks = std::clamp(static_cast<size_t>(frameBounds.w / 128) * (frameBounds.h / 128), size_t{128}, size_t{1024});
		while(images.size() >= maxBlocks)
		{
			images.erase(imageOrder.front());
			imageOrder.pop_front();
		}
		it = images.emplace(key, Block{}).first;
		imageOrder.push_back(key);
		it->second.order = std::prev(imageOrder.end());
	}
	else
		imageOrder.splice(imageOrder.end(), imageOrder, it->second.order);
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
				const auto colour = framePixels.at((key.second * native + y - frameBounds.y) * frameBounds.w + key.first * native + x - frameBounds.x);
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

void MapRendererManaAura::renderObject(
	IMapRendererContext & context,
	Canvas & target,
	const int3 & position,
	const CGObjectInstance & object,
	const std::shared_ptr<IImage> & body,
	double transparency
)
{
	if(!active || !stamp || !field || !settings.objectContact || settings.objectPresence <= 0 || !body || position.z != level || transparency <= 0)
		return;
	if(!coversObject(object))
		return;
	const double alpha = std::clamp(context.objectTransparency(object.id, position) * transparency, 0., 1.);
	if(alpha <= 0)
		return;
	auto maskIt = objectMasks.find(body.get());
	if(maskIt == objectMasks.end())
	{
		if(objectMasks.size() >= 256)
		{
			objectMasks.erase(maskOrder.front());
			maskOrder.pop_front();
		}
		ObjectMask mask;
		mask.source = body;
		const Point size = body->dimensions();
		if(size.x <= 0 || size.y <= 0)
			return;
		CanvasImage snapshot(size, CanvasScalingPolicy::IGNORE);
		auto canvas = snapshot.getCanvas();
		canvas.drawColor(Rect(0, 0, size.x, size.y), ColorRGBA(0, 0, 0, 0));
		body->setAlpha(255);
		canvas.draw(body, Point(0, 0));
		body->setAlpha(static_cast<uint8_t>(std::round(255 * alpha)));
		std::vector<uint8_t> rgba;
		snapshot.readPixelsRGBA(rgba);
		int left = size.x, top = size.y, right = 0, bottom = 0;
		for(int y = 0; y < size.y; ++y)
			for(int x = 0; x < size.x; ++x)
				if(rgba[(y * size.x + x) * 4 + 3])
				{
					left = std::min(left, x);
					top = std::min(top, y);
					right = std::max(right, x + 1);
					bottom = std::max(bottom, y + 1);
				}
		if(right <= left || bottom <= top)
			return;
		mask.bounds = Rect(left, top, right - left, bottom - top);
		mask.opaque.resize(static_cast<size_t>(mask.bounds.w) * mask.bounds.h);
		mask.foot.assign(mask.bounds.w, -1);
		for(int y = 0; y < mask.bounds.h; ++y)
			for(int x = 0; x < mask.bounds.w; ++x)
			{
				const auto spriteAlpha = rgba[((y + top) * size.x + x + left) * 4 + 3];
				mask.opaque[y * mask.bounds.w + x] = spriteAlpha != 0;
				if(spriteAlpha >= 240)
					mask.foot[x] = y + top;
			}
		maskOrder.push_back(body.get());
		mask.order = std::prev(maskOrder.end());
		maskIt = objectMasks.emplace(body.get(), std::move(mask)).first;
	}
	else
		maskOrder.splice(maskOrder.end(), maskOrder, maskIt->second.order);
	const auto & mask = maskIt->second;
	double fraction = settings.obstacleDepth;
	if(object.ID == Obj::TOWN || object.ID == Obj::RANDOM_TOWN)
		fraction = settings.townDepth;
	else if(object.ID == Obj::HERO || object.ID == Obj::BOAT)
		fraction = settings.heroDepth;
	else if(object.ID == Obj::RESOURCE || object.ID == Obj::ARTIFACT || object.ID == Obj::TREASURE_CHEST || object.ID == Obj::CAMPFIRE)
		fraction = settings.resourceDepth;
	const int depth = static_cast<int>(std::round(mask.bounds.h * fraction));
	if(depth <= 0)
		return;
	const Rect band(mask.bounds.x, mask.bounds.bottom() - depth, mask.bounds.w, depth);
	const Point source = body->dimensions() - context.objectImageOffset(object.id, position) - Point(32, 32);
	const int sx = std::max(source.x, band.x), sy = std::max(source.y, band.y);
	const int ex = std::min(source.x + 32, band.right()), ey = std::min(source.y + 32, band.bottom());
	if(ex <= sx || ey <= sy)
		return;
	const Point worldTop(position.x * 32 - source.x, position.y * 32 - source.y);
	const int scale = target.getScalingFactor();
	const int32_t key = object.id.getNum();
	auto overlayIt = objectOverlays.find(key);
	if(overlayIt == objectOverlays.end())
	{
		if(objectOverlays.size() >= 512)
		{
			objectOverlays.erase(objectOrder.front());
			objectOrder.pop_front();
		}
		overlayIt = objectOverlays.emplace(key, ObjectOverlay{}).first;
		objectOrder.push_back(key);
		overlayIt->second.order = std::prev(objectOrder.end());
	}
	else
		objectOrder.splice(objectOrder.end(), objectOrder, overlayIt->second.order);
	auto & overlay = overlayIt->second;
	if(overlay.stamp != stamp || overlay.body != body.get() || overlay.worldTop != worldTop || overlay.scale != scale || !overlay.band.operator==(band))
	{
		if(!overlay.image || overlay.scale != scale || overlay.band.w != band.w || overlay.band.h != band.h)
			overlay.image = std::make_shared<CanvasImage>(Point(band.w * scale, band.h * scale), CanvasScalingPolicy::IGNORE);
		const int stride = band.w * scale;
		objectPixels.assign(static_cast<size_t>(stride) * band.h * scale * 4, 0);
		const double seconds = std::max<int64_t>(0, frame) / settings.framesPerSecond;
		std::vector<double> heights(band.w);
		for(int x = 0; x < band.w; ++x)
			heights[x] = ManaAuraContact::objectHeight(worldTop.x + band.x + x, seconds, key + 1., settings.objectPresence);
		for(int y = 0; y < band.h; ++y)
		{
			for(int x = 0; x < band.w; ++x)
			{
				if(!mask.opaque[(band.y + y - mask.bounds.y) * mask.bounds.w + x])
					continue;
				const int foot = std::clamp(mask.foot[x], band.bottom() - 1 - depth / 3, band.bottom() - 1);
				const double coverage = ManaAuraContact::objectCoverage(
					static_cast<double>(foot - band.y - y) / (foot - band.y + 1), heights[x]
				);
				const auto opacity = static_cast<uint8_t>(std::round(255 * coverage));
				if(!opacity)
					continue;
				const int wx = worldTop.x + band.x + x, wy = worldTop.y + band.y + y;
				ColorRGBA colour(0, 0, 0, 0);
				if(wx >= frameBounds.x && wx < frameBounds.right() && wy >= frameBounds.y && wy < frameBounds.bottom())
					colour = framePixels[(wy - frameBounds.y) * frameBounds.w + wx - frameBounds.x];
				if(colour.a == 0)
					colour = field->objectPixel(wx, wy);
				if(colour.a == 0)
					continue;
				colour.a = opacity;
				for(int dx = 0; dx < scale; ++dx)
				{
					auto * pixel = objectPixels.data() + ((y * scale) * stride + x * scale + dx) * 4;
					pixel[0] = colour.r;
					pixel[1] = colour.g;
					pixel[2] = colour.b;
					pixel[3] = colour.a;
				}
			}
			for(int dy = 1; dy < scale; ++dy)
				std::memcpy(objectPixels.data() + (y * scale + dy) * stride * 4, objectPixels.data() + y * scale * stride * 4, stride * 4);
		}
		overlay.image->writePixelsRGBA(objectPixels);
		overlay.stamp = stamp;
		overlay.body = body.get();
		overlay.worldTop = worldTop;
		overlay.scale = scale;
		overlay.band = band;
	}
	overlay.image->setAlpha(static_cast<uint8_t>(std::round(255 * alpha)));
	target.draw(overlay.image, Point(sx - source.x, sy - source.y), Rect(sx - band.x, sy - band.y, ex - sx, ey - sy));
}
