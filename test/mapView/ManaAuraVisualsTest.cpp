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
		EXPECT_NEAR(actual.r, sample.rgba[0], 2) << sample.x << ',' << sample.y << " time=" << current;
		EXPECT_NEAR(actual.g, sample.rgba[1], 2);
		EXPECT_NEAR(actual.b, sample.rgba[2], 2);
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
