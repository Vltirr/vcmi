/*
 * CSpellWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CWindowObject.h"
#include "../widgets/IVideoHolder.h"

class CGHeroInstance;
class CGTownInstance;
class CSpell;

class IImage;
class CAnimImage;
class CPicture;
class CLabel;
class CGStatusBar;
class CPlayerInterface;
class CSpellWindow;
class CTextInput;
class TransparentFilledRectangle;
class CToggleButton;
class VideoWidgetOnce;

/// The spell window
class CSpellWindow : public CWindowObject, public IVideoHolder
{
	class SpellArea : public CIntObject
	{
		const CSpell * mySpell;
		int schoolLevel; //range: 0 none, 3 - expert
		CSpellWindow * owner;
		std::shared_ptr<CPicture> image;
		std::shared_ptr<CPicture> schoolBorder;
		std::shared_ptr<CLabel> name;
		std::shared_ptr<CLabel> level;
		std::shared_ptr<CLabel> cost;
		std::string learnedSpellRequirement() const;
	public:
		SpellArea(Rect pos, CSpellWindow * owner);
		~SpellArea();
		void setSpell(const CSpell * spell);

		void clickPressed(const Point & cursorPosition) override;
		void showPopupWindow(const Point & cursorPosition) override;
		void hover(bool on) override;
	};

	class InteractiveArea : public CIntObject
	{
		std::function<void()> onLeft;
		CSpellWindow * owner;

		std::string hoverText;
		std::string helpText;
	public:
		void clickPressed(const Point & cursorPosition) override;
		void showPopupWindow(const Point & cursorPosition) override;
		void hover(bool on) override;

		InteractiveArea(const Rect &myRect, const std::function<void()> & funcL, int helpTextId, CSpellWindow * _owner);
		InteractiveArea(const Rect &myRect, const std::function<void()> & funcL, std::string textId, CSpellWindow * _owner);
		InteractiveArea(const Rect &myRect, const std::function<void()> & funcL, CSpellWindow * _owner);
		void setHelp(const std::string & hover, const std::string & help);
	};

	std::shared_ptr<CPicture> leftCorner;
	std::shared_ptr<CPicture> rightCorner;

	// clickable page-turn areas
	std::shared_ptr<InteractiveArea> leftCornerArea;
	std::shared_ptr<InteractiveArea> rightCornerArea;

	std::shared_ptr<CAnimImage> schoolTab;
	std::vector<std::shared_ptr<CAnimImage>> schoolTabCustom;
	std::shared_ptr<CPicture> schoolTabAnyDisabled;
	std::shared_ptr<CAnimImage> schoolPicture;
	std::shared_ptr<CPicture> schoolPictureCustom;

	std::array<std::shared_ptr<SpellArea>, 24> spellAreas;
	std::shared_ptr<CLabel> mana;
	std::shared_ptr<InteractiveArea> manaArea;
	std::shared_ptr<CGStatusBar> statusBar;

	std::vector<std::shared_ptr<InteractiveArea>> interactiveAreas;

	std::shared_ptr<CTextInput> searchBox;
	std::shared_ptr<TransparentFilledRectangle> searchBoxRectangle;
	std::shared_ptr<CLabel> searchBoxDescription;

	std::shared_ptr<CToggleButton> showAllSpells;
	std::shared_ptr<CLabel> showAllSpellsDescription;
	std::shared_ptr<CPicture> availabilityBookmark;
	std::array<std::shared_ptr<CPicture>, 2> availabilityBookmarkIcons;
	std::shared_ptr<InteractiveArea> availabilityBookmarkArea;
	bool showOnlyCastableSpells = false;

	std::shared_ptr<VideoWidgetOnce> video;

	bool isBigSpellbook;
	int spellsPerPage;
	int offL;
	int offR;
	int offRM;
	int offT;
	int offB;

	std::map<SpellSchool, int> sitesPerTabAdv;
	std::map<SpellSchool, int> sitesPerTabBattle;

	const int MAX_CUSTOM_SPELL_SCHOOLS = 5;
	const int MAX_CUSTOM_SPELL_SCHOOLS_BIG = 6;
	std::vector<SpellSchool> customSpellSchools;

	bool battleSpellsOnly; //if true, only battle spells are displayed; if false, only adventure map spells are displayed
	SpellSchool selectedTab;
	int currentPage; //changes when corners are clicked
	std::vector<const CSpell *> mySpells; //all spels in this spellbook
	std::optional<PlayerColor> archiveOwner;
	std::map<SpellID, std::vector<ObjectInstanceID>> archiveSpells;
	std::shared_ptr<IImage> archiveBackdrop;
	Point archiveBackdropPosition;
	bool personalArchive = false;
	void updateArchiveBackdrop();
	std::string archiveDescription(const CSpell * spell) const;

	const CGHeroInstance * myHero; //hero whose spells are presented
	CPlayerInterface * myInt;

	void processSpells();
	bool isSpellUnavailable(const CSpell * spell) const;
	bool isSpellBlockedByWisdom(const CSpell * spell) const;
	void toggleSpellAvailability();
	void updateAvailabilityBookmark();
	void searchInput();
	void computeSpellsPerArea(); //recalculates spellAreas::mySpell

	void setSchoolImages(SpellSchool school);

	void setCurrentPage(int value);
	void turnPageLeft();
	void turnPageRight();

	void onVideoPlaybackFinished() override;

	bool openOnBattleSpells;
	std::function<void(SpellID)> onSpellSelect; //external processing of selected spell

public:
	CSpellWindow(const CGHeroInstance * _myHero, CPlayerInterface * _myInt, bool openOnBattleSpells = true, const std::function<void(SpellID)> & onSpellSelect = nullptr, const CGTownInstance * archiveTown = nullptr);
	~CSpellWindow();

	void fexitb();
	void fadvSpellsb();
	void fbattleSpellsb();
	void toggleSearchBoxFocus();
	void fmanaPtsb();

	void fLcornerb();
	void fRcornerb();

	void selectSchool(SpellSchool school);
	int pagesWithinCurrentTab();

	void keyPressed(EShortcut key) override;

	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
	void onScreenResize() override;
};
