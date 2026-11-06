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
#include <optional>

class IMapRendererContext;
class Canvas;
class CanvasImage;
class IImage;
class CGObjectInstance;

class MapRendererManaAura
{
public:
	static constexpr int blockTiles = 4;
	bool profiling() const { return settings.profile; }
	bool isActive() const { return active; }
	double compositionMs() const { return profileCompositionMs; }
	bool batchesOpaqueTiles() const { return active && field && settings.batchOpaqueTiles && !settings.debug; }
	void prepareFrame(IMapRendererContext & context, const Rect & visible, int level, const std::function<std::vector<ColorRGBA>(const int3 &)> & routePixels);
	uint32_t checksum(const int3 & position, const IMapRendererContext * context = nullptr) const;
	bool shadowsBelowMist(const int3 & position) const;
	bool hidesTerrain(const int3 & position) const;
	uint32_t opaqueStamp(const int3 & position) const;
	std::optional<Rect> beginObjectBatch(int scale);
	void batchObject(IMapRendererContext & context, const int3 & position, const CGObjectInstance & object, const std::shared_ptr<IImage> & body);
	void finishObjectBatch();
	bool coversObject(const CGObjectInstance & object) const;
	void renderTile(Canvas & target, const int3 & position);
	void renderStrip(Canvas & target, const int3 & position, int width);
	void renderObject(
		IMapRendererContext & context,
		Canvas & target,
		const int3 & position,
		const CGObjectInstance & object,
		const std::shared_ptr<IImage> & body,
		double transparency
	);

private:
	std::shared_ptr<CanvasImage> blockImage(const int3 & position);
	bool loaded = false;
	bool active = false;
	ManaAuraVisualSettings settings;
	std::unique_ptr<ManaAuraFieldBuilder> builder;
	Rect requestedBounds;
	uint64_t requestedGeneration = 0;
	std::unique_ptr<ManaAuraField> field;
	std::unique_ptr<ManaAuraField> animationSpare;
	uint64_t fieldGeneration = 0, pendingFrameGeneration = 0;
	std::shared_ptr<const ManaAuraFieldBuilder::Routes> frameRoutes, spareRoutes;
	int asyncQueued = 0, asyncAdopted = 0, asyncDiscarded = 0, asyncFallback = 0;
	double asyncBackgroundMaxMs = 0;
	Rect prepared;
	std::vector<int3> sources;
	int radius = 0, level = -1;
	uint64_t eligibilitySignature = 0;
	Rect routeWindow;
	bool routeShowsRoads = false, routeShowsRivers = false;
	uint32_t revision = 0, stamp = 0;
	int profileFrames = 0;
	double profileCompositionMs = 0;
	uint64_t profileFogUploads = 0, profileContactUploads = 0;
	double profileFogUploadMs = 0, profileContactUploadMs = 0;
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
	std::vector<uint8_t> opaqueTiles;
	Rect frameBounds;
	Rect contactTiles;
	uint32_t frameStamp = 0;
	std::list<std::pair<int, int>> imageOrder;

	struct ObjectMask
	{
		std::shared_ptr<IImage> source;
		Rect bounds;
		std::vector<uint8_t> opaque;
		std::vector<int> foot;
		std::list<std::pair<const IImage *, int>>::iterator order;
	};
	struct ObjectOverlay
	{
		std::shared_ptr<CanvasImage> image;
		const IImage * body = nullptr;
		Point worldTop;
		int maskScale = 0;
		uint32_t stamp = 0;
		Rect band;
		std::list<int32_t>::iterator order;
	};
	std::map<std::pair<const IImage *, int>, ObjectMask> objectMasks;
	std::list<std::pair<const IImage *, int>> maskOrder;
	std::map<int32_t, ObjectOverlay> objectOverlays;
	std::list<int32_t> objectOrder;
	std::vector<uint8_t> objectPixels;
	struct ContactPage
	{
		std::shared_ptr<CanvasImage> image;
		std::vector<uint8_t> pixels;
		int x = 0, y = 0, rowHeight = 0, height = 0;
	};
	struct BatchedObject
	{
		std::shared_ptr<IImage> body;
		Point worldTop;
		Rect band;
		size_t page;
		Point origin;
	};
	std::vector<ContactPage> contactPages;
	std::map<int32_t, BatchedObject> batchedObjects;
	Rect batchBounds;
	uint32_t batchStamp = 0;
	int batchScale = 0;
	bool batchReported = false;

	void load();
	const ObjectMask * objectMask(const std::shared_ptr<IImage> & body, uint8_t alpha, int scale);
	Rect objectBand(const ObjectMask & mask, const CGObjectInstance & object) const;
	void writeObjectBand(const ObjectMask & mask, const Rect & band, Point worldTop, int32_t key, uint8_t * pixels, size_t stride) const;
};
