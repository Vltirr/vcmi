/*
 * BattleFocusBar.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../gui/CIntObject.h"

#include "../../lib/battle/BattleSide.h"

class IImage;

/// Renders one side's Focus resource as a layered, non-interactive battle HUD widget.
class BattleFocusBar : public CIntObject
{
	BattleSide side;
	int32_t currentPoints;
	int32_t pointsPerCharge;
	int32_t maxCharges;
	int layoutCharges = 0;

	std::shared_ptr<IImage> frame;
	std::shared_ptr<IImage> progressFill;
	std::shared_ptr<IImage> separator;
	std::shared_ptr<IImage> activeCheckpoint;
	std::shared_ptr<IImage> inactiveCheckpoint;
	std::shared_ptr<IImage> ornament;
	std::shared_ptr<IImage> activeIcon;
	std::shared_ptr<IImage> inactiveIcon;

	void configureLayout(int charges);
	void render(Canvas & to) const;

public:
	static constexpr int WIDTH = 227;
	static constexpr int HEIGHT = 53;

	explicit BattleFocusBar(BattleSide side);

	void updateState(bool enabled, int32_t points, int32_t pointsPerCharge, int32_t maxCharges);
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
