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
#include "../GameEngine.h"
#include "IMapRendererContext.h"
#include "MapRendererManaAura.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "render/IImage.h"
#include "render/IRenderHandler.h"
#include <boost/container/small_vector.hpp>
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
	profileCompositionMs = 0;
	if(settings.profile && ++profileFrames == 30)
	{
		logGlobal->info("MGMA profile textures: fogUploadMs=%g fogUploads=%llu contactUploadMs=%g contactUploads=%llu samples=%d",
			profileFogUploadMs / profileFrames, static_cast<unsigned long long>(profileFogUploads),
			profileContactUploadMs / profileFrames, static_cast<unsigned long long>(profileContactUploads), profileFrames);
		profileFrames = 0;
		profileFogUploads = profileContactUploads = 0;
		profileFogUploadMs = profileContactUploadMs = 0;
		if(settings.asyncFrames || asyncDiscarded)
		{
			logGlobal->info("MGMA profile async: queued=%d adopted=%d discarded=%d fallback=%d backgroundMaxMs=%g",
				asyncQueued, asyncAdopted, asyncDiscarded, asyncFallback, asyncBackgroundMaxMs);
			asyncQueued = asyncAdopted = asyncDiscarded = asyncFallback = 0;
			asyncBackgroundMaxMs = 0;
		}
	}
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
			builder->retire(std::move(animationSpare));
		}
		fieldGeneration = 0;
		frameRoutes.reset();
		spareRoutes.reset();
		requestedGeneration = 0;
		routeImages.clear();
		routeOrder.clear();
		images.clear();
		imageOrder.clear();
		frame = -1;
		stamp = 0;
		frameStamp = 0;
		routeWindow = Rect();
		batchStamp = 0;
		batchReported = false;
	}
	if(current.empty() || bounds.w <= 0 || bounds.h <= 0)
	{
		active = false;
		return;
	}
	const int x0 = bounds.x, y0 = bounds.y, x1 = bounds.right(), y1 = bounds.bottom();

	auto mix = [](uint64_t & hash, uint64_t value)
	{
		hash = (hash ^ value) * 1099511628211ULL;
	};
	auto allowedTiles = [&](const Rect & area, uint64_t & hash)
	{
		std::vector<bool> result(area.w * area.h, false);
		if(!settings.pulseEnabled || !view.animated)
			return result;
		const Rect relevant = area.intersect(visible);
		for(int y = relevant.y; y < relevant.bottom(); ++y)
			for(int x = relevant.x; x < relevant.right(); ++x)
			{
				const int3 tile(x, y, newLevel);
				if(!context.isVisible(tile))
					continue;
				bool eligible = true;
				for(const auto & id : context.getObjects(tile))
					if(const auto * object = context.getObject(id); object && !object->isTile2Terrain())
					{
						eligible = false;
						break;
					}
				if(eligible)
				{
					const int index = (y - area.y) * area.w + x - area.x;
					result[index] = true;
					mix(hash, index + 1);
				}
			}
		return result;
	};
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
						builder->retire(std::move(animationSpare));
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
				builder->retire(std::move(animationSpare));
				field = std::move(ready->field);
				animationSpare = std::move(ready->animationSpare);
				fieldGeneration = ready->generation;
				spareRoutes.reset();
				if(settings.asyncFrames && !animationSpare)
					logGlobal->info("MGMA frame worker unavailable for this field, using synchronous composition");
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
			{
				builder->retire(std::move(ready->field));
				builder->retire(std::move(ready->animationSpare));
			}
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
		uint64_t hash = 1469598103934665603ULL;
		const auto allowed = allowedTiles(bounds, hash);
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
	const Rect relevant = visible.intersect(prepared);
	bool contributes = false;
	for(int y = relevant.y; y < relevant.bottom() && !contributes; ++y)
		for(int x = relevant.x; x < relevant.right() && !contributes; ++x)
			contributes = field->containsTile(x, y);
	if(!contributes)
	{
		active = false;
		return;
	}

	const auto alignedTiles = [](const Rect & area)
	{
		const int x = area.x / blockTiles * blockTiles, y = area.y / blockTiles * blockTiles;
		const int right = (area.right() + blockTiles - 1) / blockTiles * blockTiles;
		const int bottom = (area.bottom() + blockTiles - 1) / blockTiles * blockTiles;
		return Rect(x, y, right - x, bottom - y);
	};
	const auto contains = [](const Rect & outer, const Rect & inner)
	{
		return outer.x <= inner.x && outer.y <= inner.y && outer.right() >= inner.right() && outer.bottom() >= inner.bottom();
	};
	const Rect frameTiles = alignedTiles(relevant);
	contactTiles = frameTiles;
	const Rect nextFrame = frameTiles * 32;
	const Rect mapRect(0, 0, mapSize.x, mapSize.y);
	const Rect routeNeeded = frameTiles.resize(3).intersect(mapRect);
	const bool showRoads = context.showRoads(), showRivers = context.showRivers();
	const bool windowFits = routeWindow.x <= routeNeeded.x && routeWindow.y <= routeNeeded.y
		&& routeWindow.right() >= routeNeeded.right() && routeWindow.bottom() >= routeNeeded.bottom();
	const bool routesChanged = adopted || showRoads != routeShowsRoads || showRivers != routeShowsRivers
		|| ((showRoads || showRivers) && !windowFits);
	if(routesChanged)
	{
		const Rect routeBounds = showRoads || showRivers ? frameTiles.resize(7).intersect(mapRect) : Rect(0, 0, 0, 0);
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
					auto & tile = routeTiles[(y - routeBounds.y) * routeBounds.w + x - routeBounds.x];
					tile.route = it->second.pixels;
					tile.river = context.showRivers() && terrain.hasRiver();
				}
			}
		field->updateRoutes(routeBounds, routeTiles);
		if(settings.asyncFrames && (animationSpare || pendingFrameGeneration == fieldGeneration))
			frameRoutes = std::make_shared<ManaAuraFieldBuilder::Routes>(ManaAuraFieldBuilder::Routes{routeBounds, std::move(routeTiles)});
		routeWindow = routeBounds;
		routeShowsRoads = showRoads;
		routeShowsRivers = showRivers;
		++revision;
	}

	const int64_t currentFrame = view.animated ? static_cast<int64_t>(view.timeMs * settings.framesPerSecond / 1000) : 0;
	const auto stampFor = [&](int64_t animationFrame)
	{
		return static_cast<uint32_t>(animationFrame) * 2654435761U + revision * 2246822519U + 1;
	};
	if(pendingFrameGeneration)
		if(auto ready = builder->takeFrame(view.animated ? currentFrame : std::numeric_limits<int64_t>::max(), fieldGeneration, revision))
		{
			pendingFrameGeneration = 0;
			asyncBackgroundMaxMs = std::max(asyncBackgroundMaxMs, ready->backgroundMs);
			auto & request = ready->request;
			if(request.generation != fieldGeneration)
			{
				builder->retire(std::move(request.field));
				++asyncDiscarded;
			}
			else if(ready->error)
			{
				try
				{
					std::rethrow_exception(ready->error);
				}
				catch(const std::exception & error)
				{
					logGlobal->warn("MGMA frame preparation failed: %s; using synchronous composition", error.what());
				}
				builder->retire(std::move(request.field));
				settings.asyncFrames = false;
				++asyncDiscarded;
			}
			else if(view.animated && !adopted && !routesChanged && request.revision == revision
				&& request.frame > frame && request.frame <= currentFrame && currentFrame - request.frame <= 2
				&& contains(request.bounds, nextFrame))
			{
				animationSpare = std::move(field);
				spareRoutes = frameRoutes;
				field = std::move(request.field);
				eligibilitySignature = request.eligibilitySignature;
				frame = request.frame;
				stamp = frameStamp = stampFor(frame);
				frameBounds = request.bounds;
				framePixels = std::move(ready->pixels);
				opaqueTiles = std::move(ready->opaqueTiles);
				++asyncAdopted;
			}
			else
			{
				animationSpare = std::move(request.field);
				spareRoutes = std::move(request.appliedRoutes);
				++asyncDiscarded;
			}
		}
	const bool useAsync = settings.asyncFrames && view.animated && frameRoutes
		&& static_cast<int64_t>(nextFrame.w) * nextFrame.h <= settings.maxRasterPixels
		&& (animationSpare || pendingFrameGeneration == fieldGeneration);
	const uint32_t currentStamp = stampFor(currentFrame);
	const bool keepPublishedFrame = useAsync && stamp && !adopted && !routesChanged
		&& currentFrame >= frame && currentFrame - frame <= 2;
	if(!keepPublishedFrame && (frame != currentFrame || stamp != currentStamp))
	{
		if(settings.asyncFrames && view.animated)
			++asyncFallback;
		uint64_t hash = 1469598103934665603ULL;
		const auto allowed = allowedTiles(prepared, hash);
		if(adopted || routesChanged || hash != eligibilitySignature)
		{
			field->updateEligibility(allowed);
			eligibilitySignature = hash;
		}
		field->update(currentFrame / settings.framesPerSecond, view.animated);
		frame = currentFrame;
		stamp = currentStamp;
	}
	if(frameStamp != stamp || (useAsync ? !contains(frameBounds, nextFrame) : !frameBounds.operator==(nextFrame)))
	{
		const auto started = std::chrono::steady_clock::now();
		const Rect overlap = frameStamp == stamp ? nextFrame.intersect(frameBounds) : Rect();
		size_t composedPixels = 0;
		if(overlap.w > 0 && overlap.h > 0)
		{
			std::vector<ColorRGBA> next(static_cast<size_t>(nextFrame.w) * nextFrame.h), strip;
			auto destination = [&](int x, int y)
			{
				return next.data() + (y - nextFrame.y) * nextFrame.w + x - nextFrame.x;
			};
			for(int y = overlap.y; y < overlap.bottom(); ++y)
				std::copy_n(framePixels.data() + (y - frameBounds.y) * frameBounds.w + overlap.x - frameBounds.x,
					overlap.w, destination(overlap.x, y));
			auto composeStrip = [&](const Rect & area)
			{
				if(area.w <= 0 || area.h <= 0)
					return;
				strip.resize(static_cast<size_t>(area.w) * area.h);
				field->writePixels(area, strip);
				composedPixels += strip.size();
				for(int y = 0; y < area.h; ++y)
					std::copy_n(strip.data() + y * area.w, area.w, destination(area.x, area.y + y));
			};
			composeStrip(Rect(nextFrame.x, nextFrame.y, nextFrame.w, overlap.y - nextFrame.y));
			composeStrip(Rect(nextFrame.x, overlap.bottom(), nextFrame.w, nextFrame.bottom() - overlap.bottom()));
			composeStrip(Rect(nextFrame.x, overlap.y, overlap.x - nextFrame.x, overlap.h));
			composeStrip(Rect(overlap.right(), overlap.y, nextFrame.right() - overlap.right(), overlap.h));
			framePixels = std::move(next);
		}
		else
		{
			framePixels.resize(static_cast<size_t>(nextFrame.w) * nextFrame.h);
			field->writePixels(nextFrame, framePixels);
			composedPixels = framePixels.size();
		}
		if(adopted || (settings.debug && overlap.w > 0 && overlap.h > 0))
			logGlobal->info(
				"MGMA native composition: pixels=%llu reused=%llu cpuMs=%g",
				static_cast<unsigned long long>(composedPixels),
				static_cast<unsigned long long>(framePixels.size() - composedPixels),
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
		// Test the final alpha, including route openings, before omitting any lower layer.
		opaqueTiles = ManaAuraField::classifyOpaqueTiles(frameBounds, framePixels);
		if(settings.profile)
		{
			static int samples = 0;
			static double total = 0, maximum = 0, pixels = 0;
			const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
			profileCompositionMs = elapsed;
			total += elapsed;
			maximum = std::max(maximum, elapsed);
			pixels += static_cast<double>(composedPixels);
			if(++samples == 30)
			{
				logGlobal->info("MGMA profile composition: meanMs=%g maxMs=%g pixelsMean=%g samples=%d", total / samples, maximum, pixels / samples, samples);
				samples = 0;
				total = maximum = pixels = 0;
			}
		}
	}
	if(useAsync && animationSpare && !pendingFrameGeneration)
	{
		Rect workBounds = nextFrame;
		const Rect margin = frameTiles.resize(blockTiles).intersect(alignedTiles(prepared));
		const Rect marginPixels = margin * 32;
		if(contains(margin, frameTiles) && static_cast<int64_t>(marginPixels.w) * marginPixels.h <= settings.maxRasterPixels
			&& ((!showRoads && !showRivers) || contains(routeWindow, margin.resize(3).intersect(mapRect))))
			workBounds = marginPixels;
		uint64_t hash = 1469598103934665603ULL;
		auto allowed = allowedTiles(prepared, hash);
		animationSpare->inheritAnimation(*field);
		builder->requestFrame({fieldGeneration, revision, std::max(frame + 1, currentFrame), workBounds,
			std::move(animationSpare), frameRoutes, std::move(spareRoutes), std::move(allowed), hash});
		pendingFrameGeneration = fieldGeneration;
		++asyncQueued;
	}
}

bool MapRendererManaAura::coversObject(const CGObjectInstance & object) const
{
	if(!active || !field)
		return false;
	const auto anchor = object.isVisitable() ? object.visitablePos() : object.anchorPos();
	return anchor.z == level && field->covered(anchor.x, anchor.y);
}

bool MapRendererManaAura::shadowsBelowMist(const int3 & position) const
{
	return settings.objectContact && settings.objectPresence > 0 && checksum(position);
}

bool MapRendererManaAura::hidesTerrain(const int3 & position) const
{
	return opaqueStamp(position) != 0;
}

uint32_t MapRendererManaAura::opaqueStamp(const int3 & position) const
{
	const auto currentStamp = checksum(position);
	if(!currentStamp || frameStamp != stamp
		|| !frameBounds.isInside(position.x * 32, position.y * 32))
		return 0;
	const int x = position.x - frameBounds.x / 32, y = position.y - frameBounds.y / 32;
	return opaqueTiles.at(y * (frameBounds.w / 32) + x) ? currentStamp : 0;
}

uint32_t MapRendererManaAura::checksum(const int3 & position, const IMapRendererContext * context) const
{
	if(!active || !field || position.z != level)
		return 0;
	if(field->containsTile(position.x, position.y))
		return stamp ? stamp : 1;
	if(settings.objectContact && settings.objectPresence > 0 && context && prepared.isInside(position.x, position.y))
		for(const auto & id : context->getObjects(position))
			if(const auto * object = context->getObject(id); object && coversObject(*object))
				return stamp ? stamp : 1;
	return 0;
}

std::shared_ptr<CanvasImage> MapRendererManaAura::blockImage(const int3 & position)
{
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
		const auto uploadStart = settings.profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		const int native = blockTiles * 32;
		if(!block.image)
			block.image = std::make_shared<CanvasImage>(Point(native, native));
		static_assert(sizeof(ColorRGBA) == 4);
		uploadPixels.resize(native * native * 4);
		for(int y = 0; y < native; ++y)
		{
			const auto * source = framePixels.data() + (key.second * native + y - frameBounds.y) * frameBounds.w + key.first * native - frameBounds.x;
			std::memcpy(uploadPixels.data() + y * native * 4, source, native * 4);
		}
		block.image->writePixelsRGBA(uploadPixels);
		if(settings.profile)
		{
			profileFogUploadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
			++profileFogUploads;
		}
		block.stamp = stamp;
	}
	return block.image;
}

void MapRendererManaAura::renderStrip(Canvas & target, const int3 & position, int width)
{
	assert(width > 0 && position.x % blockTiles + width <= blockTiles);
	target.draw(blockImage(position), Point(0, 0), Rect((position.x % blockTiles) * 32, (position.y % blockTiles) * 32, width * 32, 32));
}

void MapRendererManaAura::renderTile(Canvas & target, const int3 & position)
{
	if(!checksum(position))
		return;
	renderStrip(target, position, 1);

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

const MapRendererManaAura::ObjectMask * MapRendererManaAura::objectMask(const std::shared_ptr<IImage> & body, uint8_t alpha, int scale)
{
	const std::pair key{body.get(), scale};
	auto maskIt = objectMasks.find(key);
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
			return nullptr;
		CanvasImage snapshot(size, scale > 1 ? CanvasScalingPolicy::AUTO : CanvasScalingPolicy::IGNORE);
		auto canvas = snapshot.getCanvas();
		canvas.drawColor(Rect(0, 0, size.x, size.y), ColorRGBA(0, 0, 0, 0));
		const auto placeholders = ENGINE->renderHandler().getPlaceholderDrawCount();
		body->setAlpha(255);
		canvas.draw(body, Point(0, 0));
		body->setAlpha(alpha);
		if(placeholders != ENGINE->renderHandler().getPlaceholderDrawCount())
			return nullptr;
		std::vector<uint8_t> rgba;
		snapshot.readPixelsRGBA(rgba);
		const int captureScale = canvas.getScalingFactor();
		const int pitch = size.x * captureScale;
		std::vector<uint8_t> silhouette(size.x * size.y);
		int left = size.x, top = size.y, right = 0, bottom = 0;
		for(int y = 0; y < size.y; ++y)
			for(int x = 0; x < size.x; ++x)
			{
				auto & pixel = silhouette[y * size.x + x];
				for(int sy = 0; sy < captureScale; ++sy)
					for(int sx = 0; sx < captureScale; ++sx)
						pixel = std::max(pixel, rgba[((y * captureScale + sy) * pitch + x * captureScale + sx) * 4 + 3]);
				if(!pixel)
					continue;
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x + 1);
				bottom = std::max(bottom, y + 1);
			}
		if(right <= left || bottom <= top)
			return nullptr;
		mask.bounds = Rect(left, top, right - left, bottom - top);
		mask.opaque.resize(static_cast<size_t>(mask.bounds.w) * mask.bounds.h);
		mask.foot.assign(mask.bounds.w, -1);
		for(int y = 0; y < mask.bounds.h; ++y)
			for(int x = 0; x < mask.bounds.w; ++x)
			{
				const auto spriteAlpha = silhouette[(y + top) * size.x + x + left];
				mask.opaque[y * mask.bounds.w + x] = spriteAlpha != 0;
				if(spriteAlpha >= 240)
					mask.foot[x] = y + top;
			}
		maskOrder.push_back(key);
		mask.order = std::prev(maskOrder.end());
		maskIt = objectMasks.emplace(key, std::move(mask)).first;
	}
	else
		maskOrder.splice(maskOrder.end(), maskOrder, maskIt->second.order);
	return &maskIt->second;
}

Rect MapRendererManaAura::objectBand(const ObjectMask & mask, const CGObjectInstance & object) const
{
	double fraction = settings.obstacleDepth;
	if(object.ID == Obj::TOWN || object.ID == Obj::RANDOM_TOWN)
		fraction = settings.townDepth;
	else if(object.ID == Obj::HERO || object.ID == Obj::BOAT)
		fraction = settings.heroDepth;
	else if(object.ID == Obj::RESOURCE || object.ID == Obj::ARTIFACT || object.ID == Obj::TREASURE_CHEST || object.ID == Obj::CAMPFIRE)
		fraction = settings.resourceDepth;
	const int depth = static_cast<int>(std::round(mask.bounds.h * fraction));
	return Rect(mask.bounds.x, mask.bounds.bottom() - depth, mask.bounds.w, depth);
}

void MapRendererManaAura::writeObjectBand(
	const ObjectMask & mask, const Rect & band, Point worldTop, int32_t key, uint8_t * pixels, size_t stride
) const
{
	const double seconds = std::max<int64_t>(0, frame) / settings.framesPerSecond;
	struct Column
	{
		double height;
		double inverseDepth;
		int foot;
	};
	boost::container::small_vector<Column, 256> columns;
	columns.reserve(band.w);
	for(int x = 0; x < band.w; ++x)
	{
		const int foot = std::clamp(mask.foot[x], band.bottom() - 1 - band.h / 3, band.bottom() - 1) - band.y;
		columns.push_back({ManaAuraContact::objectHeight(worldTop.x + band.x + x, seconds, key + 1., settings.objectPresence), 1. / (foot + 1), foot});
	}
	const int worldX = worldTop.x + band.x;
	const Rect contactFrame = contactTiles * 32;
	for(int y = 0; y < band.h; ++y)
	{
		const int wy = worldTop.y + band.y + y;
		const auto * frameRow = wy >= contactFrame.y && wy < contactFrame.bottom()
			? framePixels.data() + (wy - frameBounds.y) * frameBounds.w : nullptr;
		const auto * spriteRow = mask.opaque.data() + (band.y + y - mask.bounds.y) * mask.bounds.w;
		auto * row = pixels + y * stride;
		for(int x = 0; x < band.w; ++x)
		{
			ColorRGBA colour;
			if(spriteRow[x])
			{
				const auto & column = columns[x];
				const double coverage = ManaAuraContact::objectCoverage((column.foot - y) * column.inverseDepth, column.height);
				const auto opacity = static_cast<uint8_t>(255 * coverage + .5);
				if(opacity)
				{
					const int wx = worldX + x;
					if(frameRow && wx >= contactFrame.x && wx < contactFrame.right())
						colour = frameRow[wx - frameBounds.x];
					if(colour.a == 0)
						colour = field->objectPixel(wx, wy);
					if(colour.a)
						colour.a = opacity;
				}
			}
			auto * pixel = row + x * 4;
			pixel[0] = colour.r;
			pixel[1] = colour.g;
			pixel[2] = colour.b;
			pixel[3] = colour.a;
		}
	}
}

namespace
{
constexpr int contactPageSize = 512;
}

std::optional<Rect> MapRendererManaAura::beginObjectBatch(int scale)
{
	if(!active || !field || !stamp || !settings.objectContact || settings.objectPresence <= 0)
		return std::nullopt;
	const Rect visible = contactTiles;
	if(batchStamp == stamp && batchBounds.operator==(visible) && batchScale == scale)
		return std::nullopt;
	batchStamp = stamp;
	batchBounds = visible;
	batchScale = scale;
	batchedObjects.clear();
	for(auto & page : contactPages)
		page.x = page.y = page.rowHeight = page.height = 0;
	return visible;
}

void MapRendererManaAura::batchObject(
	IMapRendererContext & context, const int3 & position, const CGObjectInstance & object, const std::shared_ptr<IImage> & body
)
{
	if(!body)
		return;
	const auto alpha = static_cast<uint8_t>(std::round(255 * std::clamp(context.objectTransparency(object.id, position), 0., 1.)));
	if(!alpha)
		return;
	const auto * mask = objectMask(body, alpha, batchScale);
	if(!mask)
		return;
	const Rect band = objectBand(*mask, object);
	if(band.h <= 0 || band.w > contactPageSize || band.h > contactPageSize)
		return;

	size_t pageIndex = 0;
	for(; pageIndex < contactPages.size(); ++pageIndex)
	{
		auto & page = contactPages[pageIndex];
		if(page.x + band.w > contactPageSize)
		{
			page.x = 0;
			page.y += page.rowHeight;
			page.rowHeight = 0;
		}
		if(page.y + band.h <= contactPageSize)
			break;
	}
	if(pageIndex == contactPages.size())
	{
		if(pageIndex >= 8)
			return;
		contactPages.emplace_back();
	}
	auto & page = contactPages[pageIndex];
	const Point origin(page.x, page.y);
	page.x += band.w;
	page.rowHeight = std::max(page.rowHeight, band.h);
	page.height = std::max(page.height, ((page.y + band.h + 63) / 64) * 64);
	const size_t stride = contactPageSize * 4;
	page.pixels.resize(stride * page.height);
	const Point source = body->dimensions() - context.objectImageOffset(object.id, position) - Point(32, 32);
	const Point worldTop(position.x * 32 - source.x, position.y * 32 - source.y);
	const auto key = object.id.getNum();
	writeObjectBand(*mask, band, worldTop, key, page.pixels.data() + origin.y * stride + origin.x * 4, stride);
	batchedObjects.emplace(key, BatchedObject{body, worldTop, band, pageIndex, origin});
}

void MapRendererManaAura::finishObjectBatch()
{
	const auto uploadStart = settings.profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
	size_t uploads = 0, bytes = 0;
	for(auto & page : contactPages)
	{
		if(!page.height)
			continue;
		const Point size(contactPageSize, page.height);
		if(!page.image || page.image->dimensions() != size)
			page.image = std::make_shared<CanvasImage>(size);
		page.image->writePixelsRGBA(page.pixels);
		++uploads;
		bytes += page.pixels.size();
	}
	if(settings.profile)
	{
		profileContactUploadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count();
		profileContactUploads += uploads;
	}
	if(!batchReported && !batchedObjects.empty())
	{
		logGlobal->info("MGMA contact batch: objects=%llu pages=%llu bytes=%llu scale=%d",
			static_cast<unsigned long long>(batchedObjects.size()), static_cast<unsigned long long>(uploads),
			static_cast<unsigned long long>(bytes), batchScale);
		batchReported = true;
	}
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
	const auto alpha = static_cast<uint8_t>(std::round(255 * std::clamp(context.objectTransparency(object.id, position) * transparency, 0., 1.)));
	if(!alpha)
		return;
	const Point source = body->dimensions() - context.objectImageOffset(object.id, position) - Point(32, 32);
	const Point worldTop(position.x * 32 - source.x, position.y * 32 - source.y);
	const int scale = target.getScalingFactor();
	const int32_t key = object.id.getNum();
	auto draw = [&](const std::shared_ptr<CanvasImage> & image, const Rect & band, Point origin)
	{
		const int sx = std::max(source.x, band.x), sy = std::max(source.y, band.y);
		const int ex = std::min(source.x + 32, band.right()), ey = std::min(source.y + 32, band.bottom());
		if(ex <= sx || ey <= sy)
			return;
		image->setAlpha(alpha);
		target.draw(image, Point(sx - source.x, sy - source.y), Rect(origin.x + sx - band.x, origin.y + sy - band.y, ex - sx, ey - sy));
	};
	if(batchStamp == stamp && batchScale == scale)
	{
		const auto it = batchedObjects.find(key);
		if(it != batchedObjects.end() && it->second.body == body && it->second.worldTop == worldTop)
		{
			const auto & object = it->second;
			draw(contactPages[object.page].image, object.band, object.origin);
			return;
		}
	}

	const auto * mask = objectMask(body, alpha, scale);
	if(!mask)
		return;
	const Rect band = objectBand(*mask, object);
	if(band.h <= 0 || source.x + 32 <= band.x || source.x >= band.right() || source.y + 32 <= band.y || source.y >= band.bottom())
		return;
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
	if(overlay.stamp != stamp || overlay.body != body.get() || overlay.worldTop != worldTop || overlay.maskScale != scale || !overlay.band.operator==(band))
	{
		if(!overlay.image || overlay.band.w != band.w || overlay.band.h != band.h)
			overlay.image = std::make_shared<CanvasImage>(Point(band.w, band.h));
		const size_t stride = band.w * 4;
		objectPixels.resize(stride * band.h);
		writeObjectBand(*mask, band, worldTop, key, objectPixels.data(), stride);
		overlay.image->writePixelsRGBA(objectPixels);
		overlay.stamp = stamp;
		overlay.body = body.get();
		overlay.worldTop = worldTop;
		overlay.maskScale = scale;
		overlay.band = band;
	}
	draw(overlay.image, band, Point(0, 0));
}
