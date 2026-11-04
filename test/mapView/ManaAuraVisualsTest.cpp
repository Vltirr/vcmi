/*
 * ManaAuraVisualsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../client/mapView/ManaAuraField.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/json/JsonNode.h"
#include "ManaAuraApprovedSamples.h"
#include <chrono>

namespace
{
ManaAuraMaterial approvedMaterial()
{
	return ManaAuraMaterial::fromJson(JsonNode(JsonPath::builtin("config/manaAuraMaterial.json")));
}
std::vector<ManaAuraField::Tile> visibleTiles(const Rect & bounds)
{
	std::vector<ManaAuraField::Tile> tiles(bounds.w * bounds.h);
	for(auto & tile : tiles)
	{
		tile.visible = true;
		tile.pulseAllowed = true;
	}
	return tiles;
}
void build(ManaAuraField & field, const Rect & bounds, const std::vector<int3> & sources = {int3(10, 10, 0)})
{
	field.rebuild(bounds, int3(64, 64, 2), 0, sources, 8, visibleTiles(bounds));
}
}

TEST(ManaAuraVisuals, ApprovedBalancedMaterialMatchesSelectedMotion)
{
	ManaAuraVisualSettings config;
	config.pulseEnabled = false;
	config.controlStep = 1;
	ManaAuraField field(config, approvedMaterial());
	const auto started = std::chrono::steady_clock::now();
	build(field, Rect(0, 0, 22, 22));
	RecordProperty("fieldBuildMs", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
	double current = -1;
	for(const auto & sample : manaAuraApprovedSamples)
	{
		if(sample.seconds != current)
		{
			current = sample.seconds;
			field.update(current, true);
		}
		const auto actual = field.pixel(sample.x, sample.y);
		// Transparent RGB does not contribute to the map.
		EXPECT_NEAR(actual.r * actual.a / 255., sample.rgba[0] * sample.rgba[3] / 255., 2) << sample.x << ',' << sample.y << " time=" << current;
		EXPECT_NEAR(actual.g * actual.a / 255., sample.rgba[1] * sample.rgba[3] / 255., 2);
		EXPECT_NEAR(actual.b * actual.a / 255., sample.rgba[2] * sample.rgba[3] / 255., 2);
		EXPECT_NEAR(actual.a, sample.rgba[3], 2) << sample.x << ',' << sample.y << " time=" << current;
	}
}

TEST(ManaAuraVisuals, AdjacentViewportSamplesAreContinuousInWorldCoordinates)
{
	ManaAuraVisualSettings config;
	config.pulseEnabled = false;
	ManaAuraField left(config, approvedMaterial()), right(config, approvedMaterial());
	build(left, Rect(0, 0, 20, 20));
	build(right, Rect(4, 4, 20, 20));
	left.update(5.375, true);
	right.update(5.375, true);
	for(int y = 160; y < 600; y += 13)
		for(int x = 160; x < 600; x += 13)
			EXPECT_EQ(left.pixel(x, y), right.pixel(x, y)) << x << ',' << y;
}

TEST(ManaAuraVisuals, OverlappingSourcesFormOneFieldWithoutAddingBrightness)
{
	ManaAuraVisualSettings config;
	config.pulseEnabled = false;
	ManaAuraField single(config, approvedMaterial()), duplicate(config, approvedMaterial()), forward(config, approvedMaterial()),
		reverse(config, approvedMaterial());
	const Rect bounds(0, 0, 28, 22);
	build(single, bounds);
	build(duplicate, bounds, {int3(10, 10, 0), int3(10, 10, 0)});
	build(forward, bounds, {int3(10, 10, 0), int3(18, 10, 0)});
	build(reverse, bounds, {int3(18, 10, 0), int3(10, 10, 0)});
	for(auto * f : {&single, &duplicate, &forward, &reverse})
		f->update(1.25, true);
	for(int y = 32; y < 650; y += 23)
		for(int x = 32; x < 850; x += 23)
		{
			EXPECT_EQ(single.pixel(x, y), duplicate.pixel(x, y));
			EXPECT_EQ(forward.pixel(x, y), reverse.pixel(x, y));
		}
	EXPECT_EQ(forward.pixel(14 * 32 + 16, 10 * 32 + 16).a, 255);
}

TEST(ManaAuraVisuals, PulseHasBothSignsAndRespectsDurationsAndInactiveViews)
{
	ManaAuraVisualSettings config;
	ManaAuraField active(config, approvedMaterial());
	config.pulseEnabled = false;
	ManaAuraField control(config, approvedMaterial());
	const Rect bounds(0, 0, 22, 22);
	build(active, bounds);
	build(control, bounds);
	bool lighter = false, darker = false;
	double updateMilliseconds = 0;
	for(int frame = 0; frame <= 60; ++frame)
	{
		const double seconds = frame / 12.;
		const auto started = std::chrono::steady_clock::now();
		active.update(seconds, true);
		updateMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		control.update(seconds, true);
		for(const auto & p : active.getPulses())
		{
			EXPECT_GE(p.duration, 2.5);
			EXPECT_LE(p.duration, 3.5);
			EXPECT_TRUE(active.covered(p.x / 32, p.y / 32));
		}
		if(frame % 6)
			continue;
		for(int y = 160; y < 560; y += 5)
			for(int x = 160; x < 560; x += 5)
			{
				const auto a = active.pixel(x, y), b = control.pixel(x, y);
				lighter |= a.g > b.g;
				darker |= a.g < b.g;
				EXPECT_EQ(a.a, b.a);
			}
	}
	EXPECT_TRUE(lighter);
	EXPECT_TRUE(darker);
	RecordProperty("meanUpdateMs", updateMilliseconds / 61);
	EXPECT_GE(active.emittedCount(), 7);
	EXPECT_LE(active.emittedCount(), 11);
	active.rebuild(bounds, int3(64, 64, 2), 1, {std::array{int3(10, 10, 1)}}, 8, visibleTiles(bounds));
	EXPECT_TRUE(active.getPulses().empty());
	const auto before = active.emittedCount();
	active.update(1000, true);
	EXPECT_LE(active.emittedCount() - before, 1);
	active.update(1001, false);
	EXPECT_TRUE(active.getPulses().empty());
	active.updateEligibility(std::vector<bool>(bounds.w * bounds.h, false));
	active.update(1002, true);
	EXPECT_TRUE(active.getPulses().empty());
}

TEST(ManaAuraVisuals, InvalidOptionalSettingsFallbackAndMaterialIsValidated)
{
	JsonNode json;
	json["rate"].Float() = -1;
	json["pulse"]["minDuration"].Float() = 8;
	json["pulse"]["maxDuration"].Float() = 1;
	const auto settings = ManaAuraVisualSettings::fromJson(json);
	EXPECT_EQ(settings.rate, 1.5);
	EXPECT_GE(settings.pulseMaxDuration, settings.pulseMinDuration);
	json["size"].Integer() = 256;
	EXPECT_THROW(ManaAuraMaterial::fromJson(json), std::runtime_error);
}

TEST(ManaAuraVisuals, CameraRequestsDoNotWaitAndPublishOnlyTheLatestSnapshot)
{
	ManaAuraVisualSettings config;
	config.pulseEnabled = false;
	auto material = std::make_shared<ManaAuraMaterial>(approvedMaterial());
	ManaAuraFieldBuilder builder(config, nullptr);
	uint64_t latest = 0;
	double maxRequestMs = 0;
	for(int camera = 0; camera < 48; ++camera)
	{
		ManaAuraFieldBuilder::Request request;
		request.bounds = Rect(camera % 8, 0, 22, 22);
		request.mapSize = int3(64, 64, 2);
		request.sources = {int3(10, 10, 0)};
		request.radius = 8;
		request.tiles = visibleTiles(request.bounds);
		const auto started = std::chrono::steady_clock::now();
		latest = builder.request(std::move(request));
		maxRequestMs = std::max(maxRequestMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
	}
	RecordProperty("maxCameraRequestMs", maxRequestMs);
	std::optional<ManaAuraFieldBuilder::Result> ready;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while(!ready && std::chrono::steady_clock::now() < deadline)
	{
		ready = builder.take();
		if(!ready)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	ASSERT_TRUE(ready);
	ASSERT_FALSE(ready->error);
	ASSERT_EQ(ready->generation, latest);
	RecordProperty("backgroundBuildMs", ready->preparationMs);
	ASSERT_TRUE(ready->field);
	ManaAuraField expected(config, *material);
	build(expected, Rect(7, 0, 22, 22));
	expected.update(1.25, true);
	ready->field->update(1.25, true);
	for(int y = 160; y < 600; y += 13)
		for(int x = 250; x < 600; x += 13)
			EXPECT_EQ(ready->field->pixel(x, y), expected.pixel(x, y));
	builder.retire(std::move(ready->field));
	builder.cancel();
	EXPECT_FALSE(builder.take());
}

TEST(ManaAuraVisuals, ReplacingCameraGeometryPreservesPulseTiming)
{
	ManaAuraVisualSettings config;
	ManaAuraField previous(config, approvedMaterial()), next(config, approvedMaterial());
	build(previous, Rect(0, 0, 22, 22));
	for(int frame = 0; frame < 30; ++frame)
		previous.update(frame / 12., true);
	ASSERT_FALSE(previous.getPulses().empty());
	build(next, Rect(4, 0, 22, 22));
	next.inheritAnimation(previous);
	next.updateEligibility(std::vector<bool>(22 * 22, true));
	ASSERT_FALSE(next.getPulses().empty());
	EXPECT_EQ(next.emittedCount(), previous.emittedCount());
	EXPECT_EQ(next.getPulses().front().birth, previous.getPulses().front().birth);
	EXPECT_EQ(next.getPulses().front().duration, previous.getPulses().front().duration);
}

TEST(ManaAuraVisuals, SmallAurasStayCachedAcrossPixelCameraMoves)
{
	const Rect coverage(4, 4, 35, 35);
	Rect prepared(0, 0, 0, 0);
	int builds = 0;
	for(int pixel = 0; pixel < 600; ++pixel)
	{
		const Rect visible(4 + pixel / 32, 10, 14, 14);
		auto next = ManaAuraFieldBuilder::region(visible, coverage, prepared, 2097152);
		if(!next.operator==(prepared))
			++builds;
		prepared = next;
	}
	EXPECT_EQ(builds, 1);
	EXPECT_EQ(prepared.w, 35);
	EXPECT_EQ(prepared.h, 35);
}

TEST(ManaAuraVisuals, EntireInfluenceStaysPreparedAcrossDistantCameraMoves)
{
	const Rect coverage(0, 0, 252, 252);
	Rect prepared;
	int builds = 0;
	for(const auto & visible : {Rect(0, 0, 20, 20), Rect(200, 200, 20, 20), Rect(40, 40, 20, 20), Rect(220, 0, 20, 20)})
	{
		auto next = ManaAuraFieldBuilder::region(visible, coverage, prepared, 2097152);
		if(!next.operator==(prepared))
			++builds;
		prepared = next;
		EXPECT_EQ(prepared.x, coverage.x);
		EXPECT_EQ(prepared.y, coverage.y);
		EXPECT_EQ(prepared.w, coverage.w);
		EXPECT_EQ(prepared.h, coverage.h);
	}
	EXPECT_EQ(builds, 1);
	EXPECT_EQ(ManaAuraFieldBuilder::region(Rect(280, 280, 20, 20), coverage, prepared, 2097152).w, 0);
}

TEST(ManaAuraVisuals, NativeBlocksMatchPointSamplesAcrossPulseAndRoadBoundaries)
{
	ManaAuraVisualSettings config;
	const Rect tiles(0, 0, 22, 22);
	auto info = visibleTiles(tiles);
	for(int x = 0; x < tiles.w; ++x)
	{
		auto & route = info[12 * tiles.w + x].route;
		route.assign(1024, ColorRGBA(0, 0, 0, 0));
		for(int y = 12; y <= 20; ++y)
			for(int px = 0; px < 32; ++px)
				route[y * 32 + px] = ColorRGBA(80 + px, 65 + y, 30, 255);
	}
	ManaAuraField field(config, approvedMaterial());
	field.rebuild(tiles, int3(64, 64, 2), 0, std::array{int3(10, 10, 0)}, 8, info);
	for(int frame = 0; frame < 60; ++frame)
		field.update(frame / 12., true);
	ASSERT_FALSE(field.getPulses().empty());
	for(const Rect & bounds : {Rect(256, 256, 128, 128), Rect(384, 256, 128, 128), Rect(256, 384, 128, 128)})
	{
		std::vector<ColorRGBA> block(bounds.w * bounds.h);
		field.writePixels(bounds, block);
		for(int y = 0; y < bounds.h; y += 3)
			for(int x = 0; x < bounds.w; x += 3)
				EXPECT_EQ(block[y * bounds.w + x], field.pixel(bounds.x + x, bounds.y + y));
	}
	field.updateRoutes(Rect(8, 8, 4, 4), visibleTiles(Rect(8, 8, 4, 4)));
	field.update(5.5, true);
	EXPECT_NO_THROW(field.pixel(390, 400));
}
