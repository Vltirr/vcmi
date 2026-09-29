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
constexpr int ORIGINAL_FRAME_WIDTH = 218;
constexpr int FRAME_EXTENSION = 5;
constexpr int FRAME_WIDTH = ORIGINAL_FRAME_WIDTH + FRAME_EXTENSION;
constexpr int FRAME_HEIGHT = 18;
constexpr int TRACK_X = 6;
constexpr int TRACK_Y = 12;
constexpr int TRACK_WIDTH = 206;
constexpr int TRACK_HEIGHT = 9;
constexpr int SEPARATOR_WIDTH = 3;
constexpr int SEPARATOR_HEIGHT = TRACK_HEIGHT;
constexpr int INACTIVE_SEPARATOR_HEIGHT = 7;
constexpr int ACTIVE_SEPARATOR_WIDTH = 2;
constexpr int CHECKPOINT_SIZE = 20;
constexpr int CHECKPOINT_Y = FRAME_Y + (FRAME_HEIGHT - CHECKPOINT_SIZE) / 2;
constexpr int ICON_SIZE = 34;
constexpr int ICON_VISIBLE_TOP_PADDING = 4;
constexpr int ICON_TRACK_OVERLAP = 4;
constexpr int ICON_Y = TRACK_Y + TRACK_HEIGHT - ICON_VISIBLE_TOP_PADDING - ICON_TRACK_OVERLAP;
constexpr int ORNAMENT_WIDTH = 104;
constexpr int ORNAMENT_HEIGHT = 25;
constexpr int ORNAMENT_Y = 20;
constexpr int MIN_SEGMENT_WIDTH = 3;

constexpr int MAX_VISIBLE_CHECKPOINTS = 5;
constexpr int STANDARD_WIDGET_WIDTH = ORIGINAL_FRAME_WIDTH;

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
	, frame(loadFocusImage("FocusBarFrameDarkGold.png"))
	, progressFill(loadFocusImage("ProgressBar_Energy.png"))
	, separator(loadFocusImage("Separator_ProgressBar_Energy.png"))
	, activeCheckpoint(loadFocusImage("EnegyPoint_ProgressBar_Activ.png"))
	, inactiveCheckpoint(loadFocusImage("EnegyPoint_ProgressBar_NotActiv.png"))
	, ornament(loadFocusImage("FocusBarOrnament.png"))
	, activeIcon(loadFocusImage("Icon_Energy_AddSection.png"))
	, inactiveIcon(loadFocusImage("Icon_Energy.png"))
{
	pos.w = WIDTH;
	pos.h = HEIGHT;
	frame->scaleTo(Point(FRAME_WIDTH, FRAME_HEIGHT), EScalingAlgorithm::BILINEAR);
	progressFill->scaleTo(Point(TRACK_WIDTH, TRACK_HEIGHT), EScalingAlgorithm::BILINEAR);
	separator->scaleTo(Point(SEPARATOR_WIDTH, SEPARATOR_HEIGHT), EScalingAlgorithm::BILINEAR);
	activeCheckpoint->scaleTo(Point(CHECKPOINT_SIZE, CHECKPOINT_SIZE), EScalingAlgorithm::BILINEAR);
	inactiveCheckpoint->scaleTo(Point(CHECKPOINT_SIZE, CHECKPOINT_SIZE), EScalingAlgorithm::BILINEAR);
	ornament->scaleTo(Point(ORNAMENT_WIDTH, ORNAMENT_HEIGHT), EScalingAlgorithm::BILINEAR);
	activeIcon->scaleTo(Point(ICON_SIZE, ICON_SIZE), EScalingAlgorithm::BILINEAR);
	inactiveIcon->scaleTo(Point(ICON_SIZE, ICON_SIZE), EScalingAlgorithm::BILINEAR);
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
	const int frameX = side == BattleSide::ATTACKER ? 0 : WIDTH - FRAME_WIDTH;
	const int trackX = frameX + TRACK_X + (side == BattleSide::DEFENDER ? FRAME_EXTENSION : 0);
	const int attackerOrnamentX = (STANDARD_WIDGET_WIDTH - ORNAMENT_WIDTH) / 2;
	const int ornamentX = side == BattleSide::ATTACKER ? attackerOrnamentX : WIDTH - attackerOrnamentX - ORNAMENT_WIDTH;

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
		const bool isActive = progressOffset <= fillWidth;
		const int separatorWidth = isActive
			? std::min(ACTIVE_SEPARATOR_WIDTH, separator->width())
			: separator->width();
		const int separatorHeight = isActive
			? separator->height()
			: std::min(INACTIVE_SEPARATOR_HEIGHT, separator->height());
		const int separatorSourceX = (separator->width() - separatorWidth) / 2;
		const int separatorSourceY = (separator->height() - separatorHeight) / 2;
		const int separatorY = TRACK_Y + (TRACK_HEIGHT - separatorHeight) / 2;
		to.draw(separator, widgetPosition + Point(separatorCenter - separatorWidth / 2, separatorY),
			Rect(separatorSourceX, separatorSourceY, separatorWidth, separatorHeight));
	}

	to.draw(ornament, widgetPosition + Point(ornamentX, ORNAMENT_Y));

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
			CHECKPOINT_Y));
	}

	const auto & icon = currentPoints > 0 ? activeIcon : inactiveIcon;
	const int attackerIconX = (STANDARD_WIDGET_WIDTH - ICON_SIZE) / 2;
	const int iconX = side == BattleSide::ATTACKER ? attackerIconX : WIDTH - attackerIconX - ICON_SIZE;
	to.draw(icon, widgetPosition + Point(iconX, ICON_Y));
}

void BattleFocusBar::show(Canvas & to)
{
	render(to);
}

void BattleFocusBar::showAll(Canvas & to)
{
	render(to);
}
