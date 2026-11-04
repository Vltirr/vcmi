/*
 * ManaAuraField.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ManaAuraField.h"

#include "../../lib/CThreadHelper.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/json/JsonNode.h"
#include <chrono>
#include <cmath>
#include <numbers>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

namespace
{
constexpr double tau = 2 * std::numbers::pi;
double smooth(double x)
{
	x = std::clamp(x, 0., 1.);
	return x * x * (3 - 2 * x);
}
double smooth(double low, double high, double x)
{
	return smooth((x - low) / (high - low));
}
uint8_t byte(double value)
{
	return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

class CachedNoise
{
	double scale;
	int left, top, width;
	std::vector<double> values;

public:
	CachedNoise(const Rect & area, double scale, int seed) : scale(scale)
	{
		left = static_cast<int>(std::floor((area.x - 20) / scale));
		top = static_cast<int>(std::floor((area.y - 20) / scale));
		width = static_cast<int>(std::floor((area.right() + 20) / scale)) - left + 2;
		const int height = static_cast<int>(std::floor((area.bottom() + 20) / scale)) - top + 2;
		values.resize(width * height);
		for(int y = 0; y < height; ++y)
			for(int x = 0; x < width; ++x)
			{
				double z = std::sin((x + left) * 127.1 + (y + top) * 311.7 + seed * 74.7) * 43758.5453;
				values[y * width + x] = z - std::floor(z);
			}
	}
	double operator()(double x, double y) const
	{
		x /= scale;
		y /= scale;
		int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
		double u = smooth(x - ix), v = smooth(y - iy);
		auto at = [&](int a, int b)
		{
			return values.at((b - top) * width + a - left);
		};
		return at(ix, iy) * (1 - u) * (1 - v) + at(ix + 1, iy) * u * (1 - v) + at(ix, iy + 1) * (1 - u) * v + at(ix + 1, iy + 1) * u * v;
	}
};
struct Segment
{
	double x0, y0, x1, y1;
};
}

ManaAuraVisualSettings ManaAuraVisualSettings::fromJson(const JsonNode & json)
{
	ManaAuraVisualSettings out;
	auto number = [&](const JsonNode & node, const char * key, double fallback, double low, double high)
	{
		const auto & value = node[key];
		if(value.isNull())
			return fallback;
		if(!value.isNumber() || !std::isfinite(value.Float()) || value.Float() < low || value.Float() > high)
		{
			logGlobal->warn("config/manaAuraVisuals.json: %s invalid; using %g", key, fallback);
			return fallback;
		}
		return value.Float();
	};
	auto boolean = [&](const char * key, bool fallback)
	{
		const auto & value = json[key];
		if(value.isNull())
			return fallback;
		if(value.isBool())
			return value.Bool();
		logGlobal->warn("config/manaAuraVisuals.json: %s invalid; using default", key);
		return fallback;
	};
	out.enabled = boolean("enabled", true);
	out.debug = boolean("debug", false);
	out.framesPerSecond = number(json, "framesPerSecond", 12, 1, 30);
	out.rate = number(json, "rate", 1.5, 0, 4);
	out.motionAmplitude = number(json, "motionAmplitude", 1, 0, 4);
	out.contrast = number(json, "contrast", 22, 0, 64);
	out.edgeWidth = number(json, "edgeWidth", 32, 4, 48);
	out.edgeRetreat = number(json, "edgeRetreat", 28, 0, 40);
	out.edgeMotion = number(json, "edgeMotion", 1, 0, 2);
	out.roadRelief = number(json, "roadRelief", 1.5, 0, 4);
	const double controlStep = number(json, "controlStep", 4, 1, 8);
	if(controlStep == 1 || controlStep == 2 || controlStep == 4 || controlStep == 8)
		out.controlStep = static_cast<int>(controlStep);
	else
		logGlobal->warn("config/manaAuraVisuals.json: controlStep must be 1, 2, 4 or 8; using 4");
	out.parallelism = static_cast<int>(number(json, "parallelism", 8, 1, 16));
	out.maxRasterPixels = static_cast<int>(number(json, "maxRasterPixels", 2097152, 65536, 4194304));
	out.pulseEnabled = boolean("pulseEnabled", true);
	const auto & pulse = json["pulse"];
	out.pulseRate = number(pulse, "eventsPerSecond", 2, 0, 20);
	out.pulseMinDuration = number(pulse, "minDuration", 2.5, .2, 10);
	out.pulseMaxDuration = number(pulse, "maxDuration", std::max(3.5, out.pulseMinDuration), out.pulseMinDuration, 10);
	out.pulseAmplitude = number(pulse, "amplitude", 1, 0, 3);
	out.pulseRadius = number(pulse, "radiusPixels", 46, 8, 64);
	out.pulseReferenceArea = number(pulse, "referenceAreaTiles", 90.43, 1, 10000);
	out.maxPulses = static_cast<int>(number(pulse, "maxActive", 32, 1, 64));
	out.seed = static_cast<uint32_t>(number(pulse, "seed", out.seed, 0, UINT32_MAX));
	return out;
}

ManaAuraMaterial ManaAuraMaterial::fromJson(const JsonNode & json)
{
	ManaAuraMaterial out;
	if(!json["size"].isNumber() || json["size"].Float() != 256)
		throw std::runtime_error("config/manaAuraMaterial.json: expected a 256x256 material");
	out.size = 256;
	for(const auto & [key, destination] : {
			std::pair{"signal", &out.signal},
            {"coarse", &out.coarse},
            {"mid",    &out.mid   }
    })
	{
		const auto & values = json[key];
		if(!values.isVector() || values.Vector().size() != 65536)
			throw std::runtime_error(std::string("config/manaAuraMaterial.json: invalid plane ") + key);
		destination->reserve(65536);
		for(const auto & value : values.Vector())
		{
			if(!value.isNumber() || value.Float() < 0 || value.Float() > 65535 || value.Float() != std::floor(value.Float()))
				throw std::runtime_error(std::string("config/manaAuraMaterial.json: invalid sample in ") + key);
			destination->push_back(static_cast<float>(value.Float() / 32767.5 - 1));
		}
	}
	return out;
}

float ManaAuraMaterial::sample(const std::vector<float> & plane, double x, double y) const
{
	const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
	const double u = x - ix, v = y - iy;
	auto at = [&](int a, int b)
	{
		return plane.at((b & 255) * 256 + (a & 255));
	};
	return at(ix, iy) * (1 - u) * (1 - v) + at(ix + 1, iy) * u * (1 - v) + at(ix, iy + 1) * (1 - u) * v + at(ix + 1, iy + 1) * u * v;
}

ManaAuraField::ManaAuraField(ManaAuraVisualSettings settings, ManaAuraMaterial material)
	: settings(std::move(settings)), material(std::move(material)), random(this->settings.seed)
{
}

double ManaAuraField::intensity(double x, double y) const
{
	double result = 0;
	for(const auto & source : sources)
		result = std::max(result, std::clamp((radius - std::hypot(x - source.x, y - source.y)) / (radius - 1), 0., 1.));
	return result;
}

bool ManaAuraField::covered(int x, int y) const
{
	for(const auto & source : sources)
		if(static_cast<int64_t>(x - source.x) * (x - source.x) + static_cast<int64_t>(y - source.y) * (y - source.y) <= static_cast<int64_t>(radius) * radius)
			return true;
	return false;
}

void ManaAuraField::rebuild(
	const Rect & tiles,
	const int3 & mapSize,
	int newLevel,
	std::span<const int3> newSources,
	int newRadius,
	const std::vector<Tile> & tileInfo,
	std::stop_token stop
)
{
	if(level != newLevel)
	{
		pulses.clear();
		lastTime = -1;
	}
	if(stop.stop_requested())
		return;
	tileBounds = tiles;
	level = newLevel;
	radius = newRadius;
	sources.clear();
	for(const auto & source : newSources)
		if(source.z == level)
			sources.push_back(source);
	step = settings.controlStep;
	while(step < 32 && static_cast<int64_t>(tiles.w * 32 / step + 160 / step) * (tiles.h * 32 / step + 160 / step) > settings.maxRasterPixels)
		step *= 2;
	const int pad = (80 + step - 1) / step;
	area = Rect(tiles.x * 32 - pad * step, tiles.y * 32 - pad * step, tiles.w * 32 + 2 * pad * step, tiles.h * 32 + 2 * pad * step);
	width = area.w / step;
	height = area.h / step;
	cells.assign(static_cast<size_t>(width) * height, {});
	motion.assign(cells.size(), {});
	routes.clear();
	corners.resize((tiles.w + 1) * (tiles.h + 1));
	for(int y = 0; y <= tiles.h; ++y)
		for(int x = 0; x <= tiles.w; ++x)
			corners[y * (tiles.w + 1) + x] = std::round(intensity(tiles.x + x - .5, tiles.y + y - .5) * 254) / 254;
	candidates.clear();
	interiorCells.clear();
	tileVisual.assign(tiles.w * tiles.h, false);
	tileCoverage.assign(tiles.w * tiles.h, false);
	const Rect coverage(tiles.x - 6, tiles.y - 6, tiles.w + 12, tiles.h + 12);
	std::vector<bool> coveredTiles(coverage.w * coverage.h);
	auto has = [&](int x, int y)
	{
		if(x < coverage.x || y < coverage.y || x >= coverage.right() || y >= coverage.bottom())
			return false;
		return static_cast<bool>(coveredTiles[(y - coverage.y) * coverage.w + x - coverage.x]);
	};
	for(int y = coverage.y; y < coverage.bottom(); ++y)
		for(int x = coverage.x; x < coverage.right(); ++x)
			coveredTiles[(y - coverage.y) * coverage.w + x - coverage.x] = x >= 0 && y >= 0 && x < mapSize.x && y < mapSize.y && covered(x, y);
	std::vector<std::vector<Segment>> nearby(coverage.w * coverage.h);
	for(int y = coverage.y; y < coverage.bottom(); ++y)
		for(int x = coverage.x; x < coverage.right(); ++x)
		{
			if(!has(x, y))
				continue;
			std::vector<Segment> edges;
			if(!has(x, y - 1))
				edges.push_back({x * 32., y * 32., (x + 1) * 32., y * 32.});
			if(!has(x, y + 1))
				edges.push_back({x * 32., (y + 1) * 32., (x + 1) * 32., (y + 1) * 32.});
			if(!has(x - 1, y))
				edges.push_back({x * 32., y * 32., x * 32., (y + 1) * 32.});
			if(!has(x + 1, y))
				edges.push_back({(x + 1) * 32., y * 32., (x + 1) * 32., (y + 1) * 32.});
			for(int yy = std::max(coverage.y, y - 4); yy <= std::min(coverage.bottom() - 1, y + 4); ++yy)
				for(int xx = std::max(coverage.x, x - 4); xx <= std::min(coverage.right() - 1, x + 4); ++xx)
				{
					auto & list = nearby[(yy - coverage.y) * coverage.w + xx - coverage.x];
					list.insert(list.end(), edges.begin(), edges.end());
				}
		}
	std::vector<float> distance(cells.size());
	constexpr std::array<double, 8> phaseScale{173, 91, 47, 151, 103, 53, 139, 67};
	constexpr std::array<int, 8> phaseSeed{13, 37, 71, 23, 59, 83, 31, 97};

	std::vector<CachedNoise> phaseNoise;
	for(size_t k = 0; k < 8; ++k)
		phaseNoise.emplace_back(area, phaseScale[k], phaseSeed[k]);
	CachedNoise warpX(area, 173, 19), warpY(area, 197, 41), broadNoise(area, 211, 7), midNoise(area, 87, 29), smallNoise(area, 37, 53);

	tbb::task_arena arena(settings.parallelism);
	arena.execute(
		[&]
		{
			tbb::parallel_for(
				tbb::blocked_range<int>(0, height, 8),
				[&](const auto & range)
				{
					for(int y = range.begin(); y < range.end(); ++y)
						for(int x = 0; x < width; ++x)
						{
							if(x == 0 && stop.stop_requested())
								return;
							const size_t i = y * width + x;
							auto & cell = cells[i];
							const int wx = area.x + x * step, wy = area.y + y * step;
							const int tx = static_cast<int>(std::floor(wx / 32.)), ty = static_cast<int>(std::floor(wy / 32.));
							const auto & segments = nearby.at((ty - coverage.y) * coverage.w + tx - coverage.x);
							double squared = 96. * 96.;
							for(const auto & e : segments)
							{
								double dx = std::max({e.x0 - wx - .5, wx + .5 - e.x1, 0.}), dy = std::max({e.y0 - wy - .5, wy + .5 - e.y1, 0.});
								squared = std::min(squared, dx * dx + dy * dy);
							}
							const double d = std::sqrt(squared);
							cell.rawDistance = has(tx, ty) ? d : -d;
							distance[i] = cell.rawDistance;
							if(cell.rawDistance < -64)
								continue;
							const double c = material.sample(material.coarse, wx, wy), n = material.sample(material.mid, wx, wy);
							cell.erosion = 6 * (1 - c);
							cell.width = settings.edgeWidth + 4 * n;
							cell.originalWidth = cell.width;
							const double nx = wx + 34 * (warpX(wx, wy) - .5), ny = wy + 34 * (warpY(wx, wy) - .5);
							const double broad = broadNoise(nx, ny), mid = midNoise(nx, ny), small = smallNoise(nx, ny);
							const double bank = smooth((broad - .48) / .3), pocket = smooth((mid - .4) / .3);
							cell.retreat = settings.edgeRetreat * std::max(.65 * bank, pocket * (.35 + .65 * broad));
							cell.width *= 1 - .25 * bank * small;
							cell.gain = 1 + .25 * std::exp(-std::max(0.f, cell.rawDistance) / 36.);
							for(size_t k = 0; k < cell.phaseSin.size(); ++k)
							{
								const double phase = tau * phaseNoise[k](wx, wy);
								cell.phaseSin[k] = std::sin(phase);
								cell.phaseCos[k] = std::cos(phase);
							}
							if(tx >= tiles.x && ty >= tiles.y && tx < tiles.right() && ty < tiles.bottom())
							{
								const auto & tile = tileInfo.at((ty - tiles.y) * tiles.w + tx - tiles.x);
								cell.eligible = tile.visible && tile.pulseAllowed;
							}
						}
				}
			);
		}
	);
	// Gaussian smoothing only retracts the functional tile union; the exterior remains bounded.
	auto blur = [&](std::vector<float> input, double sigma, int radius)
	{
		radius = std::max(1, radius / step);
		std::vector<double> kernel(2 * radius + 1);
		double total = 0;
		for(int k = -radius; k <= radius; ++k)
			total += kernel[k + radius] = std::exp(-.5 * std::pow(k * step / sigma, 2));
		for(auto & value : kernel)
			value /= total;
		std::vector<double> sums(width);
		for(int axis : {0, 1})
		{
			std::vector<float> output(input.size());
			for(int y = 0; y < height; ++y)
			{
				if(stop.stop_requested())
					return input;
				std::fill(sums.begin(), sums.end(), 0.);
				for(int k = -radius; k <= radius; ++k)
				{
					const double weight = kernel[k + radius];
					const auto * row = input.data() + std::clamp(y + (axis ? k : 0), 0, height - 1) * width;
					if(axis)
						for(int x = 0; x < width; ++x)
							sums[x] += row[x] * weight;
					else
					{
						const int begin = std::max(0, -k), end = std::min(width, width - k);
						for(int x = 0; x < begin; ++x)
							sums[x] += row[0] * weight;
						for(int x = begin; x < end; ++x)
							sums[x] += row[x + k] * weight;
						for(int x = end; x < width; ++x)
							sums[x] += row[width - 1] * weight;
					}
				}
				for(int x = 0; x < width; ++x)
					output[y * width + x] = sums[x];
			}
			input.swap(output);
		}
		return input;
	};
	const auto smoothed = blur(distance, 20, 60);
	if(stop.stop_requested())
		return;
	for(size_t i = 0; i < cells.size(); ++i)
	{
		if(i % 4096 == 0 && stop.stop_requested())
			return;
		auto & cell = cells[i];
		cell.distance = std::min(distance[i], smoothed[i]);
		const int x = i % width, y = i / width, wx = area.x + x * step, wy = area.y + y * step;
		if(wx < tiles.x * 32 || wy < tiles.y * 32 || wx >= tiles.right() * 32 || wy >= tiles.bottom() * 32)
			continue;
		const int index = (wy / 32 - tiles.y) * tiles.w + wx / 32 - tiles.x;
		const double exterior = smooth((cell.distance - cell.erosion + cell.originalWidth) / cell.originalWidth);
		if(exterior > .001)
		{
			for(int dy = -1; dy <= 1; ++dy)
				for(int dx = -1; dx <= 1; ++dx)
					if(wx / 32 + dx >= tiles.x && wy / 32 + dy >= tiles.y && wx / 32 + dx < tiles.right() && wy / 32 + dy < tiles.bottom())
						tileVisual[(wy / 32 + dy - tiles.y) * tiles.w + wx / 32 + dx - tiles.x] = true;
		}
		tileCoverage[index] = has(wx / 32, wy / 32);
		if(cell.rawDistance > 58 && exterior > .98)
		{
			interiorCells.push_back(i);
			if(cell.eligible)
				candidates.push_back(i);
		}
	}
	updateRoutes(tiles, tileInfo);
	pulses.erase(
		std::remove_if(
			pulses.begin(),
			pulses.end(),
			[&](const Pulse & p)
			{
				return p.x < tiles.x * 32 || p.y < tiles.y * 32 || p.x >= tiles.right() * 32 || p.y >= tiles.bottom() * 32 || !covered(p.x / 32, p.y / 32);
			}
		),
		pulses.end()
	);
}

void ManaAuraField::updateEligibility(const std::vector<bool> & allowed)
{
	candidates.clear();
	for(const size_t i : interiorCells)
	{
		const int wx = area.x + (i % width) * step, wy = area.y + (i / width) * step;
		auto & cell = cells[i];
		cell.eligible = allowed.at((wy / 32 - tileBounds.y) * tileBounds.w + wx / 32 - tileBounds.x);
		if(cell.eligible && !onRoute(wx, wy))
			candidates.push_back(i);
	}

	pulses.erase(
		std::remove_if(
			pulses.begin(),
			pulses.end(),
			[&](const Pulse & p)
			{
				const int x = (p.x - area.x) / step, y = (p.y - area.y) / step;
				return x < 0 || y < 0 || x >= width || y >= height || !cells.at(y * width + x).eligible;
			}
		),
		pulses.end()
	);
}

double ManaAuraField::randomUnit()
{
	return static_cast<double>(random()) / (static_cast<double>(std::mt19937::max()) + 1);
}
double ManaAuraField::emissionRate() const
{
	return settings.pulseRate * std::min(1., eligibleArea() / settings.pulseReferenceArea);
}

void ManaAuraField::update(double seconds, bool animate)
{
	if(lastTime < 0 || seconds < lastTime)
	{
		pulses.clear();
		nextBirth = seconds + .5;
	}
	lastTime = seconds;
	pulses.erase(
		std::remove_if(
			pulses.begin(),
			pulses.end(),
			[&](const Pulse & p)
			{
				return seconds >= p.birth + p.duration;
			}
		),
		pulses.end()
	);
	if(!animate || !settings.pulseEnabled)
		pulses.clear();
	const double rate = emissionRate();
	if(animate && settings.pulseEnabled && !candidates.empty() && rate > 0 && seconds >= nextBirth)
	{
		if(pulses.size() < settings.maxPulses)
		{
			const size_t index = candidates.at(std::min(candidates.size() - 1, static_cast<size_t>(randomUnit() * candidates.size())));
			Pulse pulse{
				static_cast<double>(area.x + index % width * step),
				static_cast<double>(area.y + index / width * step),
				seconds,
				settings.pulseMinDuration + randomUnit() * (settings.pulseMaxDuration - settings.pulseMinDuration),
				static_cast<int>(randomUnit() * 3)
			};
			pulses.push_back(pulse);
			++emitted;
			if(settings.debug)
				logGlobal->info(
					"MGMA Pulse Soft #%llu: (%g,%g,%d) radius=%g duration=%g eligibleTiles=%g rate=%g",
					static_cast<unsigned long long>(emitted),
					pulse.x,
					pulse.y,
					level,
					settings.pulseRadius,
					pulse.duration,
					eligibleArea(),
					rate
				);
		}
		nextBirth = seconds + (.75 + .5 * randomUnit()) / rate;
	}
	else if(rate <= 0 || !animate || !settings.pulseEnabled)
		nextBirth = seconds + .5;
	render(animate ? seconds : 0);
}

void ManaAuraField::render(double seconds)
{
	const double angle = tau * seconds * settings.rate / 24;
	std::array<double, 6> sine{}, cosine{};
	for(int harmonic : {2, 3, 5})
	{
		sine[harmonic] = std::sin(harmonic * angle);
		cosine[harmonic] = std::cos(harmonic * angle);
	}
	tbb::task_arena arena(settings.parallelism);
	arena.execute(
		[&]
		{
			tbb::parallel_for(
				tbb::blocked_range<size_t>(0, cells.size(), 4096),
				[&](const auto & range)
				{
					for(size_t entry = range.begin(); entry != range.end(); ++entry)
					{
						const size_t i = entry;
						const int x = i % width, y = i / width;
						const auto & cell = cells[i];
						if(cell.distance < -64)
							continue;
						auto wave = [&](int harmonic, int phase)
						{
							return sine[harmonic] * cell.phaseCos[phase] + (cosine[harmonic] - 1) * cell.phaseSin[phase];
						};
						const double dx = settings.motionAmplitude * cell.gain * (2.3 * wave(2, 0) + 1.3 * wave(3, 1) + .45 * wave(5, 2));
						const double dy = settings.motionAmplitude * cell.gain * (2.1 * wave(2, 3) + 1.2 * wave(3, 4) + .4 * wave(5, 5));
						const double pulse = settings.edgeMotion
										   * (5.5 * (sine[2] * cell.phaseCos[6] + cosine[2] * cell.phaseSin[6])
											  + 2.5 * (sine[3] * cell.phaseCos[7] + cosine[3] * cell.phaseSin[7]));
						const double a = smooth((cell.distance - cell.erosion - cell.retreat - pulse + cell.width) / cell.width);
						const double oldWidth = cell.originalWidth;
						const double cap = smooth((cell.distance - cell.erosion + oldWidth) / oldWidth);
						motion[i] = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(std::min(a, cap))};
					}
				}
			);
		}
	);
}

void ManaAuraField::updateRoutes(const Rect & bounds, const std::vector<Tile> & tiles)
{
	auto previous = std::move(routes);
	auto previousInputs = std::move(routeInputs);
	routes.clear();
	routeInputs.clear();
	for(int y = bounds.y; y < bounds.bottom(); ++y)
		for(int x = bounds.x; x < bounds.right(); ++x)
			if(const auto & route = tiles.at((y - bounds.y) * bounds.w + x - bounds.x).route; !route.empty())
				routeInputs[{x, y}] = route;
	std::array<double, 9> kernel{};
	double total = 0;
	for(int k = -4; k <= 4; ++k)
		total += kernel[k + 4] = std::exp(-.5 * std::pow(k / 1.2, 2));
	for(auto & value : kernel)
		value /= total;
	for(int ty = bounds.y; ty < bounds.bottom(); ++ty)
		for(int tx = bounds.x; tx < bounds.right(); ++tx)
		{
			if(tiles.at((ty - bounds.y) * bounds.w + tx - bounds.x).route.empty())
				continue;
			bool unchanged = previous.contains({tx, ty});
			for(int dy = -1; dy <= 1 && unchanged; ++dy)
				for(int dx = -1; dx <= 1 && unchanged; ++dx)
				{
					const std::pair key{tx + dx, ty + dy};
					const auto old = previousInputs.find(key), current = routeInputs.find(key);
					unchanged = (old == previousInputs.end() && current == routeInputs.end())
							 || (old != previousInputs.end() && current != routeInputs.end() && old->second == current->second);
				}
			if(unchanged)
			{
				routes.emplace(std::pair{tx, ty}, std::move(previous.at({tx, ty})));
				continue;
			}
			std::array<float, 1600> lum{}, mask{}, horizontalLum{}, horizontalMask{};
			for(int y = 0; y < 40; ++y)
				for(int x = 0; x < 40; ++x)
				{
					const int wx = tx * 32 + x - 4, wy = ty * 32 + y - 4;
					const int a = static_cast<int>(std::floor(wx / 32.)), b = static_cast<int>(std::floor(wy / 32.));
					if(a < bounds.x || b < bounds.y || a >= bounds.right() || b >= bounds.bottom())
						continue;
					const auto & source = tiles.at((b - bounds.y) * bounds.w + a - bounds.x).route;
					if(source.empty())
						continue;
					const auto c = source.at((wy - b * 32) * 32 + wx - a * 32);
					mask[y * 40 + x] = c.a / 255.f;
					lum[y * 40 + x] = (.2126 * c.r + .7152 * c.g + .0722 * c.b) * mask[y * 40 + x];
				}
			for(int y = 0; y < 40; ++y)
				for(int x = 4; x < 36; ++x)
				{
					double l = 0, m = 0;
					for(int k = -4; k <= 4; ++k)
					{
						l += lum[y * 40 + x + k] * kernel[k + 4];
						m += mask[y * 40 + x + k] * kernel[k + 4];
					}
					horizontalLum[y * 40 + x] = l;
					horizontalMask[y * 40 + x] = m;
				}
			auto & route = routes[{tx, ty}];
			for(int y = 4; y < 36; ++y)
				for(int x = 4; x < 36; ++x)
				{
					const float alpha = mask[y * 40 + x];
					if(alpha <= 0)
						continue;
					double l = 0, m = 0;
					for(int k = -4; k <= 4; ++k)
					{
						l += horizontalLum[(y + k) * 40 + x] * kernel[k + 4];
						m += horizontalMask[(y + k) * 40 + x] * kernel[k + 4];
					}
					const float mean = l, weight = m;
					const size_t index = (y - 4) * 32 + x - 4;
					route.relief[index] =
						settings.roadRelief * alpha * (-5 + 8 * std::clamp((lum[y * 40 + x] / alpha - mean / std::max(weight, .000001f)) / 14., -1., 1.));
					route.mask[index] = byte(alpha * 255);
				}
		}
	candidates.clear();
	for(const auto i : interiorCells)
		if(cells[i].eligible && !onRoute(area.x + i % width * step, area.y + i / width * step))
			candidates.push_back(i);
}

bool ManaAuraField::onRoute(int worldX, int worldY) const
{
	const int x = static_cast<int>(std::floor(worldX / 32.)), y = static_cast<int>(std::floor(worldY / 32.));
	const auto it = routes.find({x, y});
	return it != routes.end() && it->second.mask[(worldY - y * 32) * 32 + worldX - x * 32] != 0;
}

ColorRGBA ManaAuraField::basePixel(int worldX, int worldY) const
{
	if(worldX < tileBounds.x * 32 || worldY < tileBounds.y * 32 || worldX >= tileBounds.right() * 32 || worldY >= tileBounds.bottom() * 32)
		return ColorRGBA(0, 0, 0, 0);
	const int x = (worldX - area.x) / step, y = (worldY - area.y) / step;
	const double u = static_cast<double>((worldX - area.x) % step) / step, v = static_cast<double>((worldY - area.y) % step) / step;
	const auto & a = motion.at(y * width + x);
	const auto & b = motion.at(y * width + x + 1);
	const auto & c = motion.at((y + 1) * width + x);
	const auto & d = motion.at((y + 1) * width + x + 1);
	auto interpolate = [&](float Motion::* member)
	{
		return a.*member * (1 - u) * (1 - v) + b.*member * u * (1 - v) + c.*member * (1 - u) * v + d.*member * u * v;
	};
	const double alpha = interpolate(&Motion::alpha);
	if(alpha < .001)
		return ColorRGBA(0, 0, 0, 0);
	const double dx = interpolate(&Motion::x), dy = interpolate(&Motion::y);
	const int tx = worldX / 32, ty = worldY / 32, ci = (ty - tileBounds.y) * (tileBounds.w + 1) + tx - tileBounds.x;
	const double ux = (worldX - tx * 32 + .5) / 32, uy = (worldY - ty * 32 + .5) / 32;
	const float influence = corners.at(ci) * (1 - ux) * (1 - uy) + corners.at(ci + 1) * ux * (1 - uy) + corners.at(ci + tileBounds.w + 1) * (1 - ux) * uy
						  + corners.at(ci + tileBounds.w + 2) * ux * uy;
	const float initial = material.signal.at((worldY & 255) * 256 + (worldX & 255));
	const double moved = material.sample(material.signal, worldX + dx, worldY + dy);
	double relief = 0;
	if(const auto it = routes.find({tx, ty}); it != routes.end())
		relief = it->second.relief[(worldY - ty * 32) * 32 + worldX - tx * 32];
	constexpr std::array<double, 3> edge{105, 155, 235}, center{15, 41, 157}, colour{.7, .85, 1};
	std::array<uint8_t, 3> rgb{};
	for(size_t k = 0; k < 3; ++k)
	{
		const auto base =
			static_cast<uint8_t>(std::clamp(edge[k] + influence * (center[k] - edge[k]) + (initial * settings.contrast + relief) * colour[k], 0., 255.));
		rgb[k] = byte(base + (moved - initial) * settings.contrast * colour[k]);
	}
	return ColorRGBA(rgb[0], rgb[1], rgb[2], byte(alpha * 255));
}

float ManaAuraField::exposure(const Pulse & p, int worldX, int worldY, double lum, double wide, double near) const
{
	const double px = (worldX - p.x) / settings.pulseRadius, py = (worldY - p.y) / settings.pulseRadius, r2 = px * px + py * py;
	if(r2 >= 1)
		return 0;
	const double u = (lastTime - p.birth) / p.duration, phase = p.variant * 1.731;
	const double env = smooth(0, .35, u) * (1 - smooth(.55, 1, u));
	const double relief = std::clamp((.75 * (lum - wide) + .25 * (lum - near)) / 11, -1., 1.);
	const double fall = (1 - smooth(.42, 1, std::sqrt(r2))) * std::exp(-r2 * 1.8);
	return settings.pulseAmplitude * env * fall * (.35 * relief * (.84 + .16 * std::cos(tau * u + phase)) + .11 * std::sin(tau * u - .7 + phase * .2));
}

ColorRGBA ManaAuraField::expose(ColorRGBA c, double value) const
{
	const double ev = std::clamp(value, -.28, .28);
	c.r = byte(c.r * (1 + ev * (ev >= 0 ? .82 : 1.10)));
	c.g = byte(c.g * (1 + ev * (ev >= 0 ? 1.08 : 1.02)));
	c.b = byte(c.b * (1 + ev * (ev >= 0 ? .72 : .79)));
	return c;
}

ColorRGBA ManaAuraField::pixel(int worldX, int worldY) const
{
	auto colour = basePixel(worldX, worldY);
	if(pulses.empty() || colour.a < 245 || onRoute(worldX, worldY))
		return colour;
	auto luminance = [&](int x, int y)
	{
		const auto c = basePixel(x, y);
		return .23f * c.r + .64f * c.g + .13f * c.b;
	};
	const double lum = luminance(worldX, worldY);
	float value = 0;
	for(const auto & pulse : pulses)
	{
		if(std::abs(worldX - pulse.x) >= settings.pulseRadius || std::abs(worldY - pulse.y) >= settings.pulseRadius)
			continue;
		const double wide =
			(luminance(worldX - 10, worldY) + luminance(worldX + 10, worldY) + luminance(worldX, worldY - 10) + luminance(worldX, worldY + 10)) * .25;
		const double near =
			(luminance(worldX - 4, worldY) + luminance(worldX + 4, worldY) + luminance(worldX, worldY - 4) + luminance(worldX, worldY + 4)) * .25;
		value += exposure(pulse, worldX, worldY, lum, wide, near);
	}
	return expose(colour, value);
}

void ManaAuraField::writePixels(const Rect & bounds, std::span<ColorRGBA> output) const
{
	if(output.size() != static_cast<size_t>(bounds.w) * bounds.h)
		throw std::invalid_argument("ManaAuraField::writePixels: invalid output dimensions");
	constexpr int pad = 10;
	const int stride = bounds.w + 2 * pad;
	std::vector<ColorRGBA> base(static_cast<size_t>(stride) * (bounds.h + 2 * pad));
	tbb::task_arena arena(settings.parallelism);
	arena.execute(
		[&]
		{
			tbb::parallel_for(
				tbb::blocked_range<int>(0, bounds.h + 2 * pad, 8),
				[&](const auto & range)
				{
					for(int y = range.begin(); y < range.end(); ++y)
						for(int x = 0; x < stride; ++x)
							base[y * stride + x] = basePixel(bounds.x + x - pad, bounds.y + y - pad);
				}
			);
			tbb::parallel_for(
				tbb::blocked_range<int>(0, bounds.h, 8),
				[&](const auto & range)
				{
					for(int y = range.begin(); y < range.end(); ++y)
						for(int x = 0; x < bounds.w; ++x)
						{
							const int index = (y + pad) * stride + x + pad;
							auto c = base[index];
							const int wx = bounds.x + x, wy = bounds.y + y;
							float value = 0;
							if(!pulses.empty() && c.a >= 245 && !onRoute(wx, wy))
							{
								auto lum = [&](int offset)
								{
									const auto sample = base[index + offset];
									return .23f * sample.r + .64f * sample.g + .13f * sample.b;
								};
								for(const auto & pulse : pulses)
								{
									if(std::abs(wx - pulse.x) >= settings.pulseRadius || std::abs(wy - pulse.y) >= settings.pulseRadius)
										continue;
									value += exposure(
										pulse,
										wx,
										wy,
										lum(0),
										(lum(-10) + lum(10) + lum(-10 * stride) + lum(10 * stride)) * .25,
										(lum(-4) + lum(4) + lum(-4 * stride) + lum(4 * stride)) * .25
									);
								}
								c = expose(c, value);
							}
							output[y * bounds.w + x] = c;
						}
				}
			);
		}
	);
}

bool ManaAuraField::containsTile(int x, int y) const
{
	if(x < tileBounds.x || y < tileBounds.y || x >= tileBounds.right() || y >= tileBounds.bottom())
		return false;
	return tileVisual.at((y - tileBounds.y) * tileBounds.w + x - tileBounds.x);
}

ManaAuraFieldBuilder::ManaAuraFieldBuilder(ManaAuraVisualSettings settings, std::shared_ptr<const ManaAuraMaterial> material)
	: settings(std::move(settings))
	, material(std::move(material))
	, worker(
		  [this](std::stop_token stop)
		  {
			  run(stop);
		  }
	  )
{
}

ManaAuraFieldBuilder::~ManaAuraFieldBuilder()
{
	worker.request_stop();
	cancel();
	changed.notify_all();
	worker.join();
}

uint64_t ManaAuraFieldBuilder::request(Request request)
{
	std::lock_guard lock(mutex);
	buildStop.request_stop();
	buildStop = std::stop_source();
	if(ready && ready->field)
		retired.push_back(std::move(ready->field));
	ready.reset();
	pending = Work{++generation, std::move(request), buildStop.get_token()};
	changed.notify_one();
	return generation;
}

void ManaAuraFieldBuilder::cancel()
{
	std::lock_guard lock(mutex);
	++generation;
	buildStop.request_stop();
	pending.reset();
	if(ready && ready->field)
		retired.push_back(std::move(ready->field));
	ready.reset();
	changed.notify_one();
}

std::optional<ManaAuraFieldBuilder::Result> ManaAuraFieldBuilder::take()
{
	std::lock_guard lock(mutex);
	auto result = std::move(ready);
	ready.reset();
	return result;
}

void ManaAuraFieldBuilder::retire(std::unique_ptr<ManaAuraField> field)
{
	if(!field)
		return;
	std::lock_guard lock(mutex);
	retired.push_back(std::move(field));
	changed.notify_one();
}

void ManaAuraFieldBuilder::run(std::stop_token stop)
{
	setThreadName("Mana aura preparation");
	while(!stop.stop_requested())
	{
		std::optional<Work> work;
		std::vector<std::unique_ptr<ManaAuraField>> garbage;
		{
			std::unique_lock lock(mutex);
			changed.wait(
				lock,
				stop,
				[this]
				{
					return pending.has_value() || !retired.empty();
				}
			);
			if(stop.stop_requested())
				return;
			work = std::move(pending);
			pending.reset();
			garbage.swap(retired);
		}
		garbage.clear();
		if(!work)
			continue;
		const auto started = std::chrono::steady_clock::now();
		Result result;
		result.generation = work->generation;
		result.bounds = work->request.bounds;
		try
		{
			auto & request = work->request;
			if(!material)
				material = std::make_shared<ManaAuraMaterial>(ManaAuraMaterial::fromJson(JsonNode(JsonPath::builtin("config/manaAuraMaterial.json"))));
			if(work->stop.stop_requested())
				continue;
			result.field = std::make_unique<ManaAuraField>(settings, *material);
			result.field->rebuild(request.bounds, request.mapSize, request.level, request.sources, request.radius, request.tiles, work->stop);
		}
		catch(...)
		{
			result.error = std::current_exception();
		}
		result.preparationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		if(work->stop.stop_requested())
			continue;
		{
			std::lock_guard lock(mutex);
			if(result.generation == generation)
				ready = std::move(result);
		}
	}
}

void ManaAuraField::inheritAnimation(const ManaAuraField & previous)
{
	if(level != previous.level)
		return;
	pulses = previous.pulses;
	random = previous.random;
	lastTime = previous.lastTime;
	nextBirth = previous.nextBirth;
	emitted = previous.emitted;
}

Rect ManaAuraFieldBuilder::region(const Rect & visible, const Rect & coverage, const Rect &, int)
{
	if(visible.right() <= coverage.x || visible.bottom() <= coverage.y || visible.x >= coverage.right() || visible.y >= coverage.bottom())
		return Rect(0, 0, 0, 0);
	return coverage;
}
