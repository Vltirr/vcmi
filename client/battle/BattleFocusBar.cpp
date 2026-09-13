/*
 * BattleFocusBar.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleFocusBar.h"

#include "../GameEngine.h"
#include "../render/Canvas.h"
#include "../render/IRenderHandler.h"

#include "../../lib/filesystem/ResourcePath.h"

namespace
{
constexpr int FRAME_Y = 7;
constexpr int TRACK_X = 5;
constexpr int TRACK_Y = 11;
constexpr int TRACK_WIDTH = 188;
constexpr int TRACK_HEIGHT = 8;
constexpr int SEPARATOR_Y = 10;
constexpr int ACTIVE_SEPARATOR_WIDTH = 2;
constexpr int CHECKPOINT_SIZE = 18;
constexpr int ICON_SIZE = 28;
constexpr int MAX_VISIBLE_CHECKPOINTS = 3;
constexpr int MIN_SEGMENT_WIDTH = 3;
constexpr int STANDARD_WIDGET_WIDTH = TRACK_X * 2 + TRACK_WIDTH;

std::shared_ptr<IImage> loadFocusImage(const std::string & name)
{
	return ENGINE->renderHandler().loadImage(ImagePath::builtin("battle/focus/" + name), EImageBlitMode::COLORKEY);
}
}

BattleFocusBar::BattleFocusBar(BattleSide battleSide)
	: CIntObject(0)
	, side(battleSide)
	, currentPoints(0)
	, pointsPerCharge(1)
	, maxCharges(1)
	, frame(loadFocusImage("FocusBarFrame.png"))
	, progressFill(loadFocusImage("ProgressBar_Energy.png"))
	, separator(loadFocusImage("Separator_ProgressBar_Energy.png"))
	, activeCheckpoint(loadFocusImage("EnegyPoint_ProgressBar_Activ.png"))
	, inactiveCheckpoint(loadFocusImage("EnegyPoint_ProgressBar_NotActiv.png"))
	, activeIcon(loadFocusImage("Icon_Energy_AddSection.png"))
	, inactiveIcon(loadFocusImage("Icon_Energy.png"))
{
	pos.w = WIDTH;
	pos.h = HEIGHT;
	setInputEnabled(false);
}

void BattleFocusBar::updateState(bool enabled, int32_t points, int32_t newPointsPerCharge, int32_t newMaxCharges)
{
	pointsPerCharge = std::max(1, newPointsPerCharge);
	maxCharges = std::max(1, newMaxCharges);
	const int64_t capacity = static_cast<int64_t>(pointsPerCharge) * maxCharges;
	currentPoints = static_cast<int32_t>(std::clamp<int64_t>(points, 0, capacity));
	setEnabled(enabled);
}

void BattleFocusBar::render(Canvas & to) const
{
	const int64_t capacity = static_cast<int64_t>(pointsPerCharge) * maxCharges;
	const int fillWidth = static_cast<int>(
		(static_cast<int64_t>(currentPoints) * TRACK_WIDTH + capacity / 2) / capacity);
	const Point widgetPosition = pos.topLeft();
	const int frameX = side == BattleSide::ATTACKER ? 0 : WIDTH - STANDARD_WIDGET_WIDTH;
	const int trackX = frameX + TRACK_X;

	to.draw(frame, widgetPosition + Point(frameX, FRAME_Y));

	if(fillWidth > 0)
	{
		if(side == BattleSide::ATTACKER)
		{
			to.draw(progressFill, widgetPosition + Point(trackX, TRACK_Y), Rect(0, 0, fillWidth, TRACK_HEIGHT));
		}
		else
		{
			const int sourceX = TRACK_WIDTH - fillWidth;
			const int destinationX = trackX + sourceX;
			to.draw(progressFill, widgetPosition + Point(destinationX, TRACK_Y),
				Rect(sourceX, 0, fillWidth, TRACK_HEIGHT));
		}
	}

	const int segmentCount = std::min<int64_t>(capacity, TRACK_WIDTH / MIN_SEGMENT_WIDTH);
	for(int segment = 1; segment < segmentCount; ++segment)
	{
		const int progressOffset = (segment * TRACK_WIDTH + segmentCount / 2) / segmentCount;
		const int attackerCenter = TRACK_X + progressOffset;
		const int separatorCenter = side == BattleSide::ATTACKER ? attackerCenter : WIDTH - attackerCenter;
		const int separatorWidth = progressOffset <= fillWidth
			? std::min(ACTIVE_SEPARATOR_WIDTH, separator->width())
			: separator->width();
		const int separatorSourceX = (separator->width() - separatorWidth) / 2;
		to.draw(separator, widgetPosition + Point(separatorCenter - separatorWidth / 2, SEPARATOR_Y),
			Rect(separatorSourceX, 0, separatorWidth, separator->height()));
	}

	const int visibleCheckpoints = std::min(maxCharges, MAX_VISIBLE_CHECKPOINTS);
	for(int checkpoint = 1; checkpoint <= visibleCheckpoints; ++checkpoint)
	{
		const int representedCharge = (checkpoint * maxCharges + visibleCheckpoints - 1) / visibleCheckpoints;
		const int64_t threshold = static_cast<int64_t>(representedCharge) * pointsPerCharge;
		const int progressOffset = static_cast<int>((threshold * TRACK_WIDTH + capacity / 2) / capacity);
		const int attackerCenter = TRACK_X + progressOffset;
		const int checkpointCenter = side == BattleSide::ATTACKER ? attackerCenter : WIDTH - attackerCenter;
		const auto & checkpointImage = currentPoints >= threshold ? activeCheckpoint : inactiveCheckpoint;
		to.draw(checkpointImage, widgetPosition + Point(checkpointCenter - CHECKPOINT_SIZE / 2,
			(HEIGHT - CHECKPOINT_SIZE) / 2));
	}

	const auto & icon = currentPoints > 0 ? activeIcon : inactiveIcon;
	const int attackerIconX = (STANDARD_WIDGET_WIDTH - ICON_SIZE) / 2;
	const int iconX = side == BattleSide::ATTACKER ? attackerIconX : WIDTH - attackerIconX - ICON_SIZE;
	to.draw(icon, widgetPosition + Point(iconX, (HEIGHT - ICON_SIZE) / 2));
}

void BattleFocusBar::show(Canvas & to)
{
	render(to);
}

void BattleFocusBar::showAll(Canvas & to)
{
	render(to);
}
