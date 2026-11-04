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

#include "../../lib/json/JsonNode.h"
#include <cmath>
#include <numbers>

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
double noise(double x, double y, double scale, int seed)
{
	x /= scale;
	y /= scale;
	const double ix = std::floor(x), iy = std::floor(y), u = smooth(x - ix), v = smooth(y - iy);
	auto value = [seed](double a, double b)
	{
		const double z = std::sin(a * 127.1 + b * 311.7 + seed * 74.7) * 43758.5453;
		return z - std::floor(z);
	};
	return value(ix, iy) * (1 - u) * (1 - v) + value(ix + 1, iy) * u * (1 - v) + value(ix, iy + 1) * (1 - u) * v + value(ix + 1, iy + 1) * u * v;
}
uint8_t byte(double value)
{
	return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}
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
	const std::vector<Tile> & tileInfo
)
{
	if(level != newLevel)
	{
		pulses.clear();
		lastTime = -1;
	}
	tileBounds = tiles;
	level = newLevel;
	radius = newRadius;
	sources.clear();
	for(const auto & source : newSources)
		if(source.z == level)
			sources.push_back(source);
	step = 1;
	while(step < 32 && static_cast<int64_t>(tiles.w * 32 / step + 160 / step) * (tiles.h * 32 / step + 160 / step) > settings.maxRasterPixels)
		step *= 2;
	const int pad = (80 + step - 1) / step;
	area = Rect(tiles.x * 32 - pad * step, tiles.y * 32 - pad * step, tiles.w * 32 + 2 * pad * step, tiles.h * 32 + 2 * pad * step);
	width = area.w / step;
	height = area.h / step;
	cells.assign(static_cast<size_t>(width) * height, {});
	pixels.assign(cells.size(), ColorRGBA(0, 0, 0, 0));
	candidates.clear();
	visibleCells.clear();
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
	std::vector<float> distance(cells.size()), routeLum(cells.size()), routeAlpha(cells.size());
	constexpr std::array<double, 8> phaseScale{173, 91, 47, 151, 103, 53, 139, 67};
	constexpr std::array<int, 8> phaseSeed{13, 37, 71, 23, 59, 83, 31, 97};
	for(int y = 0; y < height; ++y)
		for(int x = 0; x < width; ++x)
		{
			const size_t i = y * width + x;
			auto & cell = cells[i];
			const int wx = area.x + x * step, wy = area.y + y * step;
			const int tx = static_cast<int>(std::floor(wx / 32.)), ty = static_cast<int>(std::floor(wy / 32.));
			const auto & segments = nearby.at((ty - coverage.y) * coverage.w + tx - coverage.x);
			double d = 96;
			for(const auto & e : segments)
				d = std::min(d, std::hypot(std::max({e.x0 - wx - .5, wx + .5 - e.x1, 0.}), std::max({e.y0 - wy - .5, wy + .5 - e.y1, 0.})));
			cell.rawDistance = has(tx, ty) ? d : -d;
			distance[i] = cell.rawDistance;
			const double c = material.sample(material.coarse, wx, wy), n = material.sample(material.mid, wx, wy);
			cell.erosion = 6 * (1 - c);
			cell.width = settings.edgeWidth + 4 * n;
			cell.originalWidth = cell.width;
			cell.initial = material.sample(material.signal, wx, wy);
			const double nx = wx + 34 * (noise(wx, wy, 173, 19) - .5), ny = wy + 34 * (noise(wx, wy, 197, 41) - .5);
			const double broad = noise(nx, ny, 211, 7), mid = noise(nx, ny, 87, 29), small = noise(nx, ny, 37, 53);
			const double bank = smooth((broad - .48) / .3), pocket = smooth((mid - .4) / .3);
			cell.retreat = settings.edgeRetreat * std::max(.65 * bank, pocket * (.35 + .65 * broad));
			cell.width *= 1 - .25 * bank * small;
			cell.gain = 1 + .25 * std::exp(-std::max(0.f, cell.rawDistance) / 36.);
			for(size_t k = 0; k < cell.phaseSin.size(); ++k)
			{
				const double phase = tau * noise(wx, wy, phaseScale[k], phaseSeed[k]);
				cell.phaseSin[k] = std::sin(phase);
				cell.phaseCos[k] = std::cos(phase);
			}
			const double ux = (wx - tx * 32 + .5) / 32, uy = (wy - ty * 32 + .5) / 32;
			auto corner = [&](int a, int b)
			{
				return std::round(intensity(a - .5, b - .5) * 254) / 254;
			};
			cell.intensity = corner(tx, ty) * (1 - ux) * (1 - uy) + corner(tx + 1, ty) * ux * (1 - uy) + corner(tx, ty + 1) * (1 - ux) * uy
						   + corner(tx + 1, ty + 1) * ux * uy;
			if(tx >= tiles.x && ty >= tiles.y && tx < tiles.right() && ty < tiles.bottom())
			{
				const auto & tile = tileInfo.at((ty - tiles.y) * tiles.w + tx - tiles.x);
				cell.eligible = tile.visible && tile.pulseAllowed;
				if(!tile.route.empty())
				{
					const auto color = tile.route.at((wy - ty * 32) * 32 + wx - tx * 32);
					routeAlpha[i] = color.a / 255.f;
					routeLum[i] = (.2126 * color.r + .7152 * color.g + .0722 * color.b) * routeAlpha[i];
					cell.routeMask = routeAlpha[i];
				}
			}
		}
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
		for(int axis : {0, 1})
		{
			std::vector<float> output(input.size());
			for(int y = 0; y < height; ++y)
				for(int x = 0; x < width; ++x)
				{
					double value = 0;
					for(int k = -radius; k <= radius; ++k)
						value +=
							input[std::clamp(y + (axis ? k : 0), 0, height - 1) * width + std::clamp(x + (axis ? 0 : k), 0, width - 1)] * kernel[k + radius];
					output[y * width + x] = value;
				}
			input.swap(output);
		}
		return input;
	};
	const auto smoothed = blur(distance, 20, 60), routeMean = blur(routeLum, 1.2, 4), routeWeight = blur(routeAlpha, 1.2, 4);
	for(size_t i = 0; i < cells.size(); ++i)
	{
		auto & cell = cells[i];
		cell.distance = std::min(distance[i], smoothed[i]);
		if(cell.routeMask > 0)
		{
			const double lum = routeLum[i] / cell.routeMask, mean = routeMean[i] / std::max(routeWeight[i], .000001f);
			cell.route = settings.roadRelief * cell.routeMask * (-5 + 8 * std::clamp((lum - mean) / 14, -1., 1.));
		}
		constexpr std::array<double, 3> edge{105, 155, 235}, center{15, 41, 157}, colour{.7, .85, 1};
		for(size_t c = 0; c < 3; ++c)
			cell.baseColour[c] = static_cast<uint8_t>(
				std::clamp(edge[c] + cell.intensity * (center[c] - edge[c]) + (cell.initial * settings.contrast + cell.route) * colour[c], 0., 255.)
			);
		const int x = i % width, y = i / width, wx = area.x + x * step, wy = area.y + y * step;
		if(wx < tiles.x * 32 || wy < tiles.y * 32 || wx >= tiles.right() * 32 || wy >= tiles.bottom() * 32)
			continue;
		const int index = (wy / 32 - tiles.y) * tiles.w + wx / 32 - tiles.x;
		const double exterior = smooth((cell.distance - cell.erosion + cell.originalWidth) / cell.originalWidth);
		cell.envelope = exterior;
		if(exterior > .001)
		{
			tileVisual[index] = true;
			visibleCells.push_back(i);
		}
		tileCoverage[index] = has(wx / 32, wy / 32);
		if(cell.eligible && cell.rawDistance > 58 && cell.routeMask == 0 && exterior > .98)
			candidates.push_back(i);
	}
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
	for(int y = 0; y < height; ++y)
		for(int x = 0; x < width; ++x)
		{
			const int wx = area.x + x * step, wy = area.y + y * step;
			if(wx < tileBounds.x * 32 || wy < tileBounds.y * 32 || wx >= tileBounds.right() * 32 || wy >= tileBounds.bottom() * 32)
				continue;
			const size_t i = y * width + x;
			auto & cell = cells[i];
			cell.eligible = allowed.at((wy / 32 - tileBounds.y) * tileBounds.w + wx / 32 - tileBounds.x);
			if(cell.eligible && cell.rawDistance > 58 && cell.routeMask == 0 && cell.envelope > .98)
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

float ManaAuraField::luminance(int x, int y) const
{
	const auto & c = pixels.at(std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1));
	return .23f * c.r + .64f * c.g + .13f * c.b;
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
	constexpr std::array<double, 3> colour{.7, .85, 1};
	for(const size_t i : visibleCells)
	{
		const int x = i % width, y = i / width;
		const auto & cell = cells[i];
		const int wx = area.x + x * step, wy = area.y + y * step;
		auto wave = [&](int harmonic, int phase)
		{
			return sine[harmonic] * cell.phaseCos[phase] + (cosine[harmonic] - 1) * cell.phaseSin[phase];
		};
		const double dx = settings.motionAmplitude * cell.gain * (2.3 * wave(2, 0) + 1.3 * wave(3, 1) + .45 * wave(5, 2));
		const double dy = settings.motionAmplitude * cell.gain * (2.1 * wave(2, 3) + 1.2 * wave(3, 4) + .4 * wave(5, 5));
		const double initial = cell.initial, moved = material.sample(material.signal, wx + dx, wy + dy);
		std::array<uint8_t, 3> rgb{};
		for(size_t c = 0; c < 3; ++c)
		{
			rgb[c] = byte(cell.baseColour[c] + (moved - initial) * settings.contrast * colour[c]);
		}
		const double pulse =
			settings.edgeMotion
			* (5.5 * (sine[2] * cell.phaseCos[6] + cosine[2] * cell.phaseSin[6]) + 2.5 * (sine[3] * cell.phaseCos[7] + cosine[3] * cell.phaseSin[7]));
		const double a = smooth((cell.distance - cell.erosion - cell.retreat - pulse + cell.width) / cell.width);
		const double oldWidth = cell.originalWidth;
		const double cap = smooth((cell.distance - cell.erosion + oldWidth) / oldWidth);
		pixels[i] = ColorRGBA(rgb[0], rgb[1], rgb[2], byte(std::min(a, cap) * 255));
	}
	if(pulses.empty())
		return;
	std::vector<float> exposure(cells.size());
	std::vector<size_t> touched;
	std::vector<bool> seen(cells.size());
	const int wide = std::max(1, static_cast<int>(std::lround(10. / step))), near = std::max(1, static_cast<int>(std::lround(4. / step)));
	for(const auto & p : pulses)
	{
		const double u = (seconds - p.birth) / p.duration;
		const double env = smooth(0, .35, u) * (1 - smooth(.55, 1, u)), phase = p.variant * 1.731;
		const int cx = (p.x - area.x) / step, cy = (p.y - area.y) / step, radius = std::ceil(settings.pulseRadius / step);
		for(int y = std::max(wide, cy - radius); y < std::min(height - wide, cy + radius); ++y)
			for(int x = std::max(wide, cx - radius); x < std::min(width - wide, cx + radius); ++x)
			{
				const size_t i = y * width + x;
				if(pixels[i].a < 245 || cells[i].routeMask > 0)
					continue;
				const double px = (x - cx) * step / settings.pulseRadius, py = (y - cy) * step / settings.pulseRadius, r2 = px * px + py * py;
				if(r2 >= 1)
					continue;
				const double lum = luminance(x, y),
							 localWide = (luminance(x - wide, y) + luminance(x + wide, y) + luminance(x, y - wide) + luminance(x, y + wide)) * .25;
				const double localNear = (luminance(x - near, y) + luminance(x + near, y) + luminance(x, y - near) + luminance(x, y + near)) * .25;
				const double relief = std::clamp((.75 * (lum - localWide) + .25 * (lum - localNear)) / 11, -1., 1.);
				const double fall = (1 - smooth(.42, 1, std::sqrt(r2))) * std::exp(-r2 * 1.8);
				exposure[i] +=
					settings.pulseAmplitude * env * fall * (.35 * relief * (.84 + .16 * std::cos(tau * u + phase)) + .11 * std::sin(tau * u - .7 + phase * .2));
				if(!seen[i])
				{
					seen[i] = true;
					touched.push_back(i);
				}
			}
	}
	for(const auto i : touched)
	{
		const double ev = std::clamp(exposure[i], -.28f, .28f);
		auto & c = pixels[i];
		c.r = byte(c.r * (1 + ev * (ev >= 0 ? .82 : 1.10)));
		c.g = byte(c.g * (1 + ev * (ev >= 0 ? 1.08 : 1.02)));
		c.b = byte(c.b * (1 + ev * (ev >= 0 ? .72 : .79)));
	}
}

ColorRGBA ManaAuraField::pixel(int worldX, int worldY) const
{
	if(worldX < area.x || worldY < area.y || worldX >= area.right() || worldY >= area.bottom())
		return ColorRGBA(0, 0, 0, 0);
	return pixels.at((worldY - area.y) / step * width + (worldX - area.x) / step);
}

bool ManaAuraField::containsTile(int x, int y) const
{
	if(x < tileBounds.x || y < tileBounds.y || x >= tileBounds.right() || y >= tileBounds.bottom())
		return false;
	return tileVisual.at((y - tileBounds.y) * tileBounds.w + x - tileBounds.x);
}
