/*
 * ManaAuraField.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/Color.h"
#include "../../lib/Rect.h"
#include "../../lib/int3.h"

#include <array>
#include <condition_variable>
#include <mutex>
#include <random>
#include <span>
#include <thread>

class JsonNode;

struct ManaAuraVisualSettings
{
	bool enabled = true;
	bool debug = false;
	double framesPerSecond = 12;
	double rate = 1.5;
	double motionAmplitude = 1;
	double contrast = 22;
	double edgeWidth = 32;
	double edgeRetreat = 28;
	double edgeMotion = 1;
	double roadRelief = 1.5;
	int maxRasterPixels = 2097152;
	bool pulseEnabled = true;
	double pulseRate = 2;
	double pulseMinDuration = 2.5;
	double pulseMaxDuration = 3.5;
	double pulseAmplitude = 1;
	double pulseRadius = 46;
	double pulseReferenceArea = 90.43;
	int maxPulses = 32;
	uint32_t seed = 1398030674;

	static ManaAuraVisualSettings fromJson(const JsonNode & json);
};

/// Quantised, periodic material exported from the approved art source.
struct ManaAuraMaterial
{
	int size = 0;
	std::vector<float> signal;
	std::vector<float> coarse;
	std::vector<float> mid;
	static ManaAuraMaterial fromJson(const JsonNode & json);
	float sample(const std::vector<float> & plane, double x, double y) const;
};

/// CPU material field. World coordinates and padded sampling keep neighbouring tiles continuous.
class ManaAuraField
{
public:
	struct Pulse
	{
		double x, y, birth, duration;
		int variant;
	};

	struct Tile
	{
		bool visible = false;
		bool pulseAllowed = false;
		std::vector<ColorRGBA> route;
	};

	ManaAuraField(ManaAuraVisualSettings settings, ManaAuraMaterial material);
	void rebuild(
		const Rect & tiles,
		const int3 & mapSize,
		int level,
		std::span<const int3> sources,
		int radius,
		const std::vector<Tile> & tileInfo,
		std::stop_token stop = {}
	);
	void update(double seconds, bool animate);
	void inheritAnimation(const ManaAuraField & previous);
	void updateEligibility(const std::vector<bool> & allowed);
	ColorRGBA pixel(int worldX, int worldY) const;
	bool containsTile(int x, int y) const;
	bool covered(int x, int y) const;
	const std::vector<Pulse> & getPulses() const
	{
		return pulses;
	}
	double eligibleArea() const
	{
		return candidates.size() * step * step / 1024.;
	}
	double emissionRate() const;
	uint64_t emittedCount() const
	{
		return emitted;
	}
	int sampleStep() const
	{
		return step;
	}
	Rect bounds() const
	{
		return area;
	}
	const ManaAuraVisualSettings & configuration() const
	{
		return settings;
	}

private:
	struct Cell
	{
		float distance = -96, rawDistance = -96, intensity = 0, erosion = 0, width = 32, retreat = 0;
		float route = 0, routeMask = 0, gain = 1, envelope = 0, initial = 0, originalWidth = 32;
		std::array<float, 8> phaseSin{}, phaseCos{};
		std::array<uint8_t, 3> baseColour{};
		bool eligible = false;
	};
	ManaAuraVisualSettings settings;
	ManaAuraMaterial material;
	Rect area;
	Rect tileBounds;
	int level = -1;
	int radius = 0;
	int step = 1, width = 0, height = 0;
	std::vector<int3> sources;
	std::vector<Cell> cells;
	std::vector<ColorRGBA> pixels;
	std::vector<size_t> candidates;
	std::vector<size_t> visibleCells;
	std::vector<size_t> interiorCells;
	std::vector<bool> tileCoverage, tileVisual;
	std::vector<Pulse> pulses;
	std::mt19937 random;
	double lastTime = -1, nextBirth = 0;
	uint64_t emitted = 0;

	void render(double seconds);
	double randomUnit();
	double intensity(double x, double y) const;
	float luminance(int x, int y) const;
};

/// Latest-request worker. Only value snapshots cross the thread boundary.
class ManaAuraFieldBuilder
{
public:
	struct Request
	{
		Rect bounds;
		int3 mapSize;
		int level = 0, radius = 0;
		std::vector<int3> sources;
		std::vector<ManaAuraField::Tile> tiles;
	};
	struct Result
	{
		uint64_t generation = 0;
		double preparationMs = 0;
		Rect bounds;
		std::unique_ptr<ManaAuraField> field;
		std::exception_ptr error;
	};
	ManaAuraFieldBuilder(ManaAuraVisualSettings settings, std::shared_ptr<const ManaAuraMaterial> material);
	~ManaAuraFieldBuilder();
	uint64_t request(Request request);
	static Rect region(const Rect & visible, const Rect & coverage, const Rect & previous, int pixelBudget);
	void cancel();
	std::optional<Result> take();
	void retire(std::unique_ptr<ManaAuraField> field);

private:
	struct Work
	{
		uint64_t generation;
		Request request;
		std::stop_token stop;
	};
	ManaAuraVisualSettings settings;
	std::shared_ptr<const ManaAuraMaterial> material;
	std::mutex mutex;
	std::condition_variable_any changed;
	std::stop_source buildStop;
	uint64_t generation = 0;
	std::optional<Work> pending;
	std::optional<Result> ready;
	std::vector<std::unique_ptr<ManaAuraField>> retired;
	std::jthread worker;
	void run(std::stop_token stop);
};
