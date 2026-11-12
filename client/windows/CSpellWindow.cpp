/*
 * CSpellWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CSpellWindow.h"

#include "../../lib/ScopeGuard.h"

#include "GUIClasses.h"
#include "InfoWindows.h"
#include "CCastleInterface.h"
#include "CMessage.h"

#include "../CPlayerInterface.h"
#include "../UIHelper.h"
#include "../PlayerLocalState.h"

#include "../battle/BattleInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "media/IVideoPlayer.h"
#include "media/ISoundPlayer.h"
#include "render/CAnimation.h"
#include "render/IRenderHandler.h"
#include "render/IImage.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "render/IFont.h"
#include "render/IScreenHandler.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/CComponent.h"
#include "../widgets/CTextInput.h"
#include "../widgets/TextControls.h"
#include "../widgets/Buttons.h"
#include "../widgets/VideoWidget.h"
#include "../adventureMap/AdventureMapInterface.h"
#include "events/InputHandler.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/adventure/AdventureSpellEffect.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/SpellSchoolHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/spells/CSpellHandler.h"

#include <chrono>
#include <cmath>

namespace
{
const std::string spellbookArt = "spellbookUI/";
const std::string favoritesArt = "spellbookUI/favorites/";

std::string favoriteText(const std::string & key)
{
	return GAME->translator().translate("vcmi.spellBook.favorites." + key);
}

Rect footerRibbon(bool large, int index)
{
	return Rect((large ? 520 : 331) + 49 * index, large ? 464 : 405, 37, 47);
}

class SpellbookLayeredPicture final : public CPicture
{
	std::shared_ptr<IImage> overlay;
	Point overlayOffset;

public:
	SpellbookLayeredPicture(const std::shared_ptr<IImage> & base, Point position, std::shared_ptr<IImage> overlay, Point overlayOffset = Point(0, 0))
		: CPicture(base, position), overlay(std::move(overlay)), overlayOffset(overlayOffset)
	{
	}

	void showAll(Canvas & to) override
	{
		CPicture::showAll(to);
		to.draw(overlay, pos.topLeft() + overlayOffset);
	}
};

class SpellbookSearchInput final : public CTextInput
{
	std::shared_ptr<CAnimImage> frame;
	std::shared_ptr<CLabel> placeholder;
	bool hovered = false;

	void updateFrame()
	{
		frame->setFrame(hasFocus() ? 2 : hovered ? 1 : 0);
		redraw();
	}

public:
	SpellbookSearchInput(const Rect & bounds, std::shared_ptr<CAnimImage> frame, std::shared_ptr<CLabel> placeholder)
		: CTextInput(bounds, FONT_TIMES, ETextAlignment::CENTERLEFT, false), frame(std::move(frame)), placeholder(std::move(placeholder))
	{
		addUsedEvents(HOVER);
		setColor(ColorRGBA(40, 25, 15));
	}

	void hover(bool on) override
	{
		hovered = on;
		updateFrame();
	}

	bool receiveEvent(const Point & position, int eventType) const override
	{
		if(eventType != LCLICK && eventType != HOVER)
			return CTextInput::receiveEvent(position, eventType);

		auto hitArea = frame->pos;
		if(!getText().empty())
			hitArea.w -= 22;
		return hitArea.isInside(position);
	}

	void onFocusGot() override
	{
		placeholder->disable();
		updateFrame();
		CTextInput::onFocusGot();
	}

	void onFocusLost() override
	{
		placeholder->setEnabled(getText().empty());
		updateFrame();
		CTextInput::onFocusLost();
	}
};

class SpellbookEmptyResults final : public CMultiLineLabel
{
public:
	SpellbookEmptyResults(const Point & center, const std::shared_ptr<IImage> & parchment)
		: CMultiLineLabel(
			Rect(center - parchment->dimensions() / 2 + Point(22, 12), parchment->dimensions() - Point(44, 24)),
			FONT_TIMES, ETextAlignment::CENTER, ColorRGBA(40, 25, 15),
			GAME->translator().translate("vcmi.spellBook.noMatchingSpells"))
	{
		OBJECT_CONSTRUCTION;
		auto frame = std::make_shared<CPicture>(parchment, Point(-22, -12));
		frame->removeUsedEvents(LCLICK | SHOW_POPUP);
		background = std::move(frame);
	}
};
}

class CSpellWindow::LevelTab final : public CIntObject
{

	CSpellWindow & owner;
	const int level;
	bool hovered = false;
	bool pressed = false;
	std::shared_ptr<CAnimImage> art;
	std::shared_ptr<CLabel> label;

public:
	LevelTab(CSpellWindow & owner, int level, const Rect & bounds, const AnimationPath & path)
		: CIntObject(LCLICK | SHOW_POPUP | HOVER), owner(owner), level(level)
	{
		pos = bounds;
		OBJECT_CONSTRUCTION;
		art = std::make_shared<CAnimImage>(path, 0);
		static const std::array<const char *, 6> numerals = {"", "I", "II", "III", "IV", "V"};
		const auto caption = level == 0 ? GAME->translator().translate("vcmi.spellBook.allLevels") : std::string(numerals.at(level));
		label = std::make_shared<CLabel>(pos.w / 2, 16, FONT_SMALL, ETextAlignment::CENTER, ColorRGBA(239, 210, 134), caption, pos.w - 8);
	}

	void showAll(Canvas & to) override
	{
		const bool selected = level == 0 ? owner.selectedLevels == 0 : (owner.selectedLevels & (1u << level)) != 0;
		art->setFrame(pressed ? 2 : selected ? 3 : hovered ? 1 : 0);
		CIntObject::showAll(to);
	}

	void hover(bool on) override
	{
		hovered = on;
		if(on)
		{
			const auto left = GAME->translator().translate("vcmi.spellBook.levelHelpLeft");
			const auto right = GAME->translator().translate("vcmi.spellBook.levelHelpRight");
			owner.statusBar->write(left + "  " + right);
		}
		else
			owner.statusBar->clear();
		owner.redraw();
	}

	void activate() override
	{
		if(isActive())
			return;

		CIntObject::activate();
		// The dispatcher retains the hover state while another window is active.
		if(isHovered() && receiveEvent(ENGINE->getCursorPosition(), HOVER))
			hover(true);
	}

	void deactivate() override
	{
		if(hovered)
			hover(false);
		CIntObject::deactivate();
	}

	void clickPressed(const Point &) override
	{
		pressed = true;
		if(!ENGINE->input().inputModeUsesGestures())
			ENGINE->sound().playSound(soundBase::button);
		owner.redraw();
	}

	void clickReleased(const Point &) override
	{
		pressed = false;
		if(ENGINE->input().inputModeUsesGestures())
			ENGINE->sound().playSound(soundBase::button);
		owner.selectLevel(level, false);
	}

	void clickCancel(const Point &) override
	{
		pressed = false;
		owner.redraw();
	}

	void showPopupWindow(const Point &) override
	{
		ENGINE->sound().playSound(soundBase::button);
		owner.selectLevel(level, true);
	}
};

namespace
{
std::shared_ptr<CButton> makeFavoriteButton(Point position, const std::string & art, const std::function<void()> & action,
	const std::string & help = {})
{
	auto button = std::make_shared<CButton>(position, AnimationPath::builtin(favoritesArt + art), CButton::tooltip(help, help), action);
	button->setHoverable(true);
	button->setRedrawParent(true);
	return button;
}

void favoriteCaption(CButton & button, const std::string & text, EFonts font, int inset = 0)
{
	auto label = std::make_shared<CLabel>(0, 0, font, ETextAlignment::CENTER, ColorRGBA(40, 25, 15), text, button.pos.w - 12 - inset);
	label->setAutoRedraw(false);
	button.setOverlay(label);
	label->moveBy(Point(-inset / 2, 0));
}

class FavoriteRibbon final : public CButton
{
	const int gripHeight;
	const std::function<bool()> hasPreviousPage;
	std::shared_ptr<IImage> occludedMask;
	bool isOpen = false;

public:
	FavoriteRibbon(bool large, const std::function<void()> & action, std::function<bool()> previousPage)
		: CButton(large ? Point(90, 15) : Point(96, 54),
			AnimationPath::builtin(favoritesArt + (large ? "ribbon-large" : "ribbon-normal")), CButton::tooltip(), action),
		gripHeight(large ? 25 : 24), hasPreviousPage(std::move(previousPage))
	{
		const auto maskPath = ImagePath::builtin(favoritesArt + (large ? "ribbon-large-6.png" : "ribbon-normal-6.png"));
		if(CResourceHandler::get()->existsResource(maskPath.addPrefix("SPRITES/")))
			occludedMask = ENGINE->renderHandler().loadImage(maskPath, EImageBlitMode::SIMPLE);
		setHoverable(true);
		setRedrawParent(true);
		setOpen(false, false);
	}

	void setOpen(bool open, bool adding)
	{
		isOpen = open;
		const int first = !open ? 0 : occludedMask && hasPreviousPage() ? 6 : 3;
		setImageOrder(first, first + 1, first, first + 2);
		const auto help = favoriteText(adding ? "cancel" : open ? "back" : "open");
		setHelp(CButton::tooltip(help, help));
	}

	bool receiveEvent(const Point & position, int eventType) const override
	{
		if(!CButton::receiveEvent(position, eventType))
			return false;
		const auto offset = position - pos.topLeft();
		if(!isOpen)
			return offset.y < gripHeight;
		return !occludedMask || !hasPreviousPage() || !occludedMask->isTransparent(offset);
	}
};

class FavoritesScopeMenu final : public CWindowObject
{
	std::function<Point()> anchor;
	std::array<std::shared_ptr<CButton>, 2> choices;

public:
	FavoritesScopeMenu(const std::function<Point()> & position, const std::array<std::string, 2> & labels,
		const std::function<void(bool)> & select)
		: CWindowObject(SHADOW_DISABLED), anchor(position)
	{
		pos = Rect(anchor(), Point(126, 44));
		addUsedEvents(LCLICK | KEYBOARD);
		OBJECT_CONSTRUCTION;
		for(size_t i = 0; i < choices.size(); ++i)
		{
			choices[i] = makeFavoriteButton(Point(0, 22 * static_cast<int>(i)), "scope-option", [this, select, i]
			{
				close();
				select(i == 1);
			});
			favoriteCaption(*choices[i], labels[i], FONT_TINY);
		}
	}

	bool receiveEvent(const Point & position, int eventType) const override
	{
		return eventType == LCLICK && !pos.isInside(position);
	}

	void clickPressed(const Point &) override { close(); }
	void keyPressed(EShortcut key) override { if(key == EShortcut::GLOBAL_RETURN) close(); }
	void onScreenResize() override { moveTo(anchor()); }
};
}

bool isLegacySpellSchool(SpellSchool school);

class CSpellWindow::Favorites final : public CIntObject
{
	enum class View { BOOK, PLAYER, HERO, ADDING };

	struct Removal
	{
		std::string spell;
		bool hero;
		JsonNode previous;
	};

	CSpellWindow & owner;
	const std::string heroKey;
	View view = View::BOOK;
	View lastList = View::PLAYER;
	std::optional<bool> preferFavorites;
	std::optional<Removal> removal;
	std::array<int, 2> counts{};
	std::array<std::shared_ptr<IImage>, 4> stars;
	std::shared_ptr<FavoriteRibbon> ribbon;
	std::shared_ptr<CButton> scope, add, restore, back, cancel, undo;
	std::shared_ptr<CPicture> prompt;
	std::shared_ptr<CLabel> promptText;

	bool heroList() const { return view == View::HERO || (view == View::ADDING && lastList == View::HERO); }
	bool hasHero() const { return !heroKey.empty(); }
	bool listOpen() const { return view == View::PLAYER || view == View::HERO; }

	std::string heroName() const
	{
		return GAME->translator().translate(owner.myHero->getNameTextID());
	}

	std::string forHero(const std::string & key) const
	{
		MetaString text = MetaString::createFromRawString(favoriteText(key));
		text.replaceRawString(heroName());
		return text.toString(&GAME->translator());
	}

	const JsonNode & preferences(bool hero) const
	{
		const auto & data = owner.myInt->localState->getSpellbookFavorites();
		return hero ? data["heroes"][heroKey] : data["player"];
	}

	bool marked(const std::string & spell, bool hero) const
	{
		const auto & specific = preferences(hero)[spell];
		if(specific.isBool())
			return specific.Bool();
		const auto & player = preferences(false)[spell];
		return hero && player.isBool() && player.Bool();
	}

	void setPreference(const std::string & spell, bool hero, const JsonNode & value)
	{
		auto data = owner.myInt->localState->getSpellbookFavorites();
		auto & list = hero ? data["heroes"][heroKey] : data["player"];
		if(value.isNull())
			list.Struct().erase(spell);
		else
			list[spell] = value;
		owner.myInt->localState->setSpellbookFavorites(std::move(data));
	}

	void setMarked(const std::string & spell, bool hero, bool value)
	{
		JsonNode entry;
		if(hero ? value != marked(spell, false) : value)
			entry.Bool() = value;
		setPreference(spell, hero, entry);
	}

	void changeView(View next)
	{
		view = next;
		if(view != View::ADDING)
			preferFavorites = listOpen();
		if(listOpen())
			lastList = view;
		removal.reset();
		owner.statusBar->clear();
		owner.searchInput();
	}

	const JsonNode & preferencesView() const
	{
		return owner.myInt->localState->getSpellbookFavorites()["views"][heroKey][owner.openOnBattleSpells ? "combat" : "adventure"];
	}

	std::string scopeLabel(bool hero) const
	{
		return (hero ? heroName() : favoriteText("player")) + " (" + std::to_string(counts[hero ? 1 : 0]) + ")";
	}

	void showScopes()
	{
		if(!hasHero())
			return;
		ENGINE->windows().createAndPushWindow<FavoritesScopeMenu>([this]{ return scope->pos.topLeft() - Point(0, 45); },
			std::array{scopeLabel(false), scopeLabel(true)}, [this](bool hero){ changeView(hero ? View::HERO : View::PLAYER); });
	}

	void restorePlayerList()
	{
		auto data = owner.myInt->localState->getSpellbookFavorites();
		data["heroes"].Struct().erase(heroKey);
		owner.myInt->localState->setSpellbookFavorites(std::move(data));
		removal.reset();
		owner.refreshSpells(owner.currentPage);
	}

	void undoRemoval()
	{
		if(!removal)
			return;
		setPreference(removal->spell, removal->hero, removal->previous);
		removal.reset();
		owner.refreshSpells(owner.currentPage);
	}

public:
	Favorites(CSpellWindow & owner, const CGHeroInstance * hero)
		: owner(owner), heroKey(hero ? std::to_string(hero->id.getNum()) : std::string())
	{
		setRedrawParent(true);
		OBJECT_CONSTRUCTION;
		for(size_t i = 0; i < stars.size(); ++i)
			stars[i] = ENGINE->renderHandler().loadImage(ImagePath::builtin(favoritesArt + "star-" + std::to_string(i) + ".png"), EImageBlitMode::SIMPLE);
		ribbon = std::make_shared<FavoriteRibbon>(owner.isBigSpellbook, [this]{ toggleView(); }, [this]
		{
			return this->owner.currentPage > 0;
		});
		const int y = owner.archiveOwner ? 512 : owner.isBigSpellbook ? 516 : 457;
		const int end = footerRibbon(owner.isBigSpellbook, 3).x + 37;
		scope = makeFavoriteButton(Point(90, y), "scope", [this]{ showScopes(); }, favoriteText(hasHero() ? "scopeHelp" : "playerHelp"));
		add = makeFavoriteButton(Point(end - 223, y), "primary", [this]{ changeView(View::ADDING); });
		restore = makeFavoriteButton(Point(end - 104, y), "secondary", [this]{ restorePlayerList(); }, hasHero() ? forHero("restoreHelp") : std::string());
		back = makeFavoriteButton(Point(end - 105, y), "back", [this]{ changeView(View::BOOK); }, favoriteText("back"));
		cancel = makeFavoriteButton(Point(end - 83, y), "cancel", [this]{ cancelAdding(); }, favoriteText("cancel"));
		undo = makeFavoriteButton(Point(end - 78 + (owner.isBigSpellbook ? 0 : 18), y), "undo", [this]{ undoRemoval(); }, favoriteText("undoHelp"));
		favoriteCaption(*add, favoriteText("add"), FONT_TINY);
		favoriteCaption(*restore, favoriteText("restore"), FONT_TINY);
		favoriteCaption(*back, favoriteText("back"), FONT_TINY);
		favoriteCaption(*cancel, favoriteText("cancel"), FONT_TINY);
		favoriteCaption(*undo, favoriteText("undo"), FONT_TINY);
		scope->setHoverEnabledWhenBlocked(true);
		scope->block(!hasHero());
		prompt = std::make_shared<CPicture>(ImagePath::builtin(favoritesArt + "prompt.png"), Point(90, y), EImageBlitMode::SIMPLE);
		prompt->removeUsedEvents(LCLICK | SHOW_POPUP);
		promptText = std::make_shared<CLabel>(192, y + 11, FONT_TINY, ETextAlignment::CENTER, ColorRGBA(40, 25, 15),
			std::string(), 190);
		updateControls();
	}

	void restoreView()
	{
		const auto & saved = preferencesView();
		lastList = View::HERO;
		if(saved.isStruct())
		{
			if(saved["open"].isBool())
				preferFavorites = saved["open"].Bool();
			lastList = saved["hero"].Bool() ? View::HERO : View::PLAYER;
			owner.selectedLevels = static_cast<unsigned>(saved["levels"].Integer()) & 0x3e;
			const auto school = SpellSchool(saved["school"].Integer());
			if(isLegacySpellSchool(school) || vstd::contains(owner.customSpellSchools, school))
				owner.selectedTab = school;
			for(const auto & entry : saved["additionalSchools"].Vector())
			{
				const auto extra = SpellSchool(entry.Integer());
				if(owner.selectedTab != SpellSchool::ANY && extra != owner.selectedTab && extra != SpellSchool::ANY
					&& (isLegacySpellSchool(extra) || vstd::contains(owner.customSpellSchools, extra))
					&& !vstd::contains(owner.additionalSchools, extra))
					owner.additionalSchools.push_back(extra);
			}
		}
		else
			owner.selectedTab = SpellSchool::ANY;

		view = preferFavorites.value_or(true) ? lastList : View::BOOK;
		owner.processSpells();
		const auto matchesContext = [this](const CSpell * spell)
		{
			return spell->isCombat() == owner.battleSpellsOnly;
		};
		const auto matchesFilters = [this, &matchesContext](const CSpell * spell)
		{
			return matchesContext(spell) && owner.matchesSchool(*spell);
		};
		if(!std::any_of(owner.mySpells.begin(), owner.mySpells.end(), matchesFilters))
		{
			owner.selectedLevels = 0;
			owner.additionalSchools.clear();
			owner.selectedTab = SpellSchool::ANY;
			owner.processSpells();
		}
		if(listOpen() && !std::any_of(owner.mySpells.begin(), owner.mySpells.end(), [&](const CSpell * spell)
		{
			return matchesContext(spell) && (preferFavorites.has_value() || !owner.isSpellUnavailable(spell));
		}))
		{
			view = View::BOOK;
			owner.processSpells();
		}
	}

	void saveView() const
	{
		if(owner.archiveOwner || !hasHero())
			return;
		auto data = owner.myInt->localState->getSpellbookFavorites();
		auto & saved = data["views"][heroKey][owner.openOnBattleSpells ? "combat" : "adventure"];
		if(preferFavorites)
			saved["open"].Bool() = *preferFavorites;
		saved["hero"].Bool() = lastList == View::HERO;
		saved["school"].Integer() = owner.selectedTab;
		auto & additional = saved["additionalSchools"].Vector();
		additional.clear();
		for(const auto school : owner.additionalSchools)
		{
			additional.emplace_back();
			additional.back().Integer() = school;
		}
		saved["levels"].Integer() = owner.selectedLevels;
		owner.myInt->localState->setSpellbookFavorites(std::move(data));
	}

	void toggleView()
	{
		if(!cancelAdding())
			changeView(listOpen() ? View::BOOK : lastList);
	}

	bool cancelAdding()
	{
		if(view != View::ADDING)
			return false;
		changeView(lastList);
		return true;
	}

	bool selectCandidate(const CSpell & spell)
	{
		if(view != View::ADDING)
			return false;
		setMarked(spell.getJsonKey(), heroList(), true);
		changeView(lastList);
		return true;
	}

	bool ribbonContains(const Point & point) const { return ribbon->receiveEvent(point, LCLICK); }

	bool markersEnabled() const { return view != View::ADDING; }
	bool isMarked(const CSpell & spell) const { return marked(spell.getJsonKey(), heroList()); }

	void toggle(const CSpell & spell)
	{
		const auto key = spell.getJsonKey();
		const bool hero = heroList();
		const bool wasMarked = marked(key, hero);
		removal.reset();
		if(wasMarked && listOpen())
			removal = Removal{key, hero, preferences(hero)[key]};
		setMarked(key, hero, !wasMarked);
		owner.refreshSpells(owner.currentPage);
	}

	std::string markerHelp(const CSpell & spell) const
	{
		const bool selected = isMarked(spell);
		return heroList() ? forHero(selected ? "removeHero" : "addHero") : favoriteText(selected ? "removePlayer" : "addPlayer");
	}

	const std::shared_ptr<IImage> & star(bool marked, bool hover) const
	{
		return stars[(marked ? 2 : 0) + (hover ? 1 : 0)];
	}

	void resetCounts() { counts = {}; }

	bool include(const CSpell & spell)
	{
		const auto key = spell.getJsonKey();
		const bool player = marked(key, false);
		const bool hero = hasHero() && marked(key, true);
		counts[0] += player;
		counts[1] += hero;
		switch(view)
		{
			case View::BOOK: return true;
			case View::PLAYER: return player;
			case View::HERO: return hero;
			case View::ADDING: return heroList() ? !hero : !player;
		}
		return false;
	}

	std::string emptyText() const
	{
		if(view == View::ADDING)
			return favoriteText("noCandidates");
		if(listOpen() && counts[heroList() ? 1 : 0] == 0)
			return favoriteText("empty");
		return GAME->translator().translate("vcmi.spellBook.noMatchingSpells");
	}

	void updateControls()
	{
		const bool open = listOpen();
		const bool adding = view == View::ADDING;
		ribbon->setOpen(open, adding);
		scope->setEnabled(open);
		if(open)
		{
			favoriteCaption(*scope, scopeLabel(heroList()), FONT_TINY, 18);
			const auto help = heroList() ? forHero("addHelp") : favoriteText("addPlayerHelp");
			add->setHelp(CButton::tooltip(help, help));
		}
		add->setEnabled(open);
		auto & secondary = heroList() ? restore : back;
		const int end = undo->pos.x + undo->pos.w - (owner.isBigSpellbook ? 0 : 18);
		const int secondaryRight = removal ? undo->pos.x - 7 : end;
		secondary->moveTo(Point(secondaryRight - secondary->pos.w, secondary->pos.y));
		add->moveTo(Point(secondary->pos.x - 7 - add->pos.w, add->pos.y));
		restore->setEnabled(view == View::HERO);
		if(view == View::HERO)
			restore->block(preferences(true).Struct().empty());
		back->setEnabled(view == View::PLAYER);
		undo->setEnabled(open && removal.has_value());
		if(adding)
			promptText->setText(heroList() ? forHero("pick") : favoriteText("pickPlayer"));
		cancel->setEnabled(adding);
		prompt->setEnabled(adding);
		promptText->setEnabled(adding);
	}
};

void CSpellWindow::buildEnhancedBackground(bool archive)
{
	auto source = archive
		? ENGINE->renderHandler().loadImage(ImagePath::builtin("SpellBookLarge"), EImageBlitMode::OPAQUE)
		: background->getSurface();
	const auto size = source->dimensions();
	auto cleaned = std::make_shared<CanvasImage>(size);
	auto canvas = cleaned->getCanvas();
	canvas.draw(source, Point(0, 0));

	const std::array<Rect, 4> oldRibbons = isBigSpellbook
		? std::array<Rect, 4>{Rect(278, 464, 37, 47), Rect(481, 465, 37, 41), Rect(575, 465, 37, 45), Rect(667, 465, 37, 47)}
		: std::array<Rect, 4>{Rect(220, 405, 37, 47), Rect(354, 406, 37, 41), Rect(417, 406, 37, 45), Rect(478, 406, 37, 47)};
	for(const auto & rect : oldRibbons)
		for(int x = 0; x < rect.w; ++x)
			canvas.draw(source, Point(rect.x + x, rect.y), Rect(rect.x - 2, rect.y, 1, rect.h));

	if(archive)
	{
		auto cover = std::make_shared<CanvasImage>(size);
		auto coverCanvas = cover->getCanvas();
		coverCanvas.drawColor(Rect(Point(0, 0), size), ColorRGBA(0, 0, 0, 0));
		const Rect area(7, 30, 782, 445);
		coverCanvas.draw(cleaned, area.topLeft(), area);
		cleaned = cover;
	}

	const std::string variant = archive ? "archive" : isBigSpellbook ? "large" : "normal";
	auto paper = ENGINE->renderHandler().loadImage(ImagePath::builtin(spellbookArt + "paper/" + variant + "-overlay.png"), EImageBlitMode::SIMPLE);
	background = std::make_shared<SpellbookLayeredPicture>(cleaned, Point(0, 0), paper);
}

namespace
{
std::shared_ptr<CPicture> makeSpellbookRibbon(const std::shared_ptr<IImage> & original, const Rect & source, Point position, const std::string & emblem = {})
{
	auto image = std::make_shared<CanvasImage>(Point(37, 47));
	auto canvas = image->getCanvas();
	canvas.drawColor(Rect(0, 0, 37, 47), ColorRGBA(0, 0, 0, 0));
	const int top = source.h == 41 ? 1 : 0;
	for(int y = 0; y < source.h; ++y)
	{
		const int notch = std::max(0, y - (source.h - 10));
		if(notch == 0)
			canvas.draw(original, Point(0, top + y), Rect(source.x, source.y + y, source.w, 1));
		else
		{
			const int width = (source.w / 2) * (10 - notch) / 10;
			if(width > 0)
				for(const int x : {0, source.w - width})
					canvas.draw(original, Point(x, top + y), Rect(source.x + x, source.y + y, width, 1));
		}
	}
	std::shared_ptr<CPicture> picture;
	if(emblem.empty())
		picture = std::make_shared<CPicture>(image, position);
	else
	{
		canvas.draw(original, Point(3, 0), Rect(420, 406, 31, 35));
		auto symbol = ENGINE->renderHandler().loadImage(ImagePath::builtin(spellbookArt + "context/" + emblem + "-symbol.png"), EImageBlitMode::SIMPLE);
		const Point offset((37 - symbol->dimensions().x) / 2, emblem == "combat" ? 2 : 4);
		picture = std::make_shared<SpellbookLayeredPicture>(image, position, symbol, offset);
	}
	picture->removeUsedEvents(CIntObject::LCLICK | CIntObject::SHOW_POPUP);
	return picture;
}
}

void CSpellWindow::buildLevelTabs()
{
	const auto path = AnimationPath::builtin(spellbookArt + "levels/heraldic-" + (isBigSpellbook ? "large" : "normal"));
	const int width = isBigSpellbook ? 54 : 48;
	const int x = isBigSpellbook ? 228 : 156;
	const int y = isBigSpellbook ? 0 : 28;
	for(int i = 0; i < static_cast<int>(levelTabs.size()); ++i)
		levelTabs[i] = std::make_shared<LevelTab>(*this, i, Rect(pos.x + x + i * (width + 4), pos.y + y, width, 40), path);

	levelHelpBackground = ENGINE->renderHandler().loadImage(ImagePath::builtin(spellbookArt + "help/background-B.png"), EImageBlitMode::SIMPLE);
}

void CSpellWindow::selectLevel(int level, bool additive)
{
	const auto previous = selectedLevels;
	if(level == 0)
		selectedLevels = 0;
	else if(!additive)
		selectedLevels = 1u << level;
	else
		selectedLevels |= 1u << level;
	if(previous != selectedLevels)
		searchInput();
	else
		redraw();
}

void CSpellWindow::setHoverHelp(const std::string & text)
{
	if(hoverHelp == text)
		return;
	hoverHelp = text;
	hoverHelpDelay = text.empty() ? 0 : 500;
	if(text.empty())
		removeUsedEvents(TIME);
	else
	{
		const auto cursor = ENGINE->getCursorPosition();
		hoverHelpAnchor = Rect(cursor, Point(1, 1));
		for(const auto & level : levelTabs)
			if(level->pos.isInside(cursor))
				hoverHelpAnchor = level->pos;
		addUsedEvents(TIME);
	}
	redraw();
}

void CSpellWindow::tick(uint32_t msPassed)
{
	if(msPassed < hoverHelpDelay)
	{
		hoverHelpDelay -= msPassed;
		return;
	}
	hoverHelpDelay = 0;
	removeUsedEvents(TIME);
	redraw();
}

void CSpellWindow::drawHoverHelp(Canvas & to)
{
	if(hoverHelp.empty() || hoverHelpDelay || !isActive())
		return;

	const auto size = levelHelpBackground->dimensions();
	const auto screen = ENGINE->screenDimensions();
	const auto lines = CMessage::breakText(hoverHelp, size.x - 44, FONT_TINY);
	const int lineHeight = ENGINE->renderHandler().loadFont(FONT_TINY)->getLineHeight();
	const int height = std::max(size.y, static_cast<int>(lines.size()) * lineHeight + 24);
	if(screen.x < size.x + 16 || screen.y < height + 16)
		return;

	const auto & anchor = hoverHelpAnchor;
	const int x = std::clamp(anchor.center().x - size.x / 2, 8, screen.x - size.x - 8);
	const int above = anchor.y - height - 8;
	const int y = above >= 8 ? above : std::min(anchor.y + anchor.h + 12, screen.y - height - 8);
	const Point origin(x, y);
	const int half = size.y / 2;
	to.draw(levelHelpBackground, origin, Rect(0, 0, size.x, half));
	for(int row = half; row < height - (size.y - half); ++row)
		to.draw(levelHelpBackground, origin + Point(0, row), Rect(0, half, size.x, 1));
	to.draw(levelHelpBackground, origin + Point(0, height - (size.y - half)), Rect(0, half, size.x, size.y - half));
	const ColorRGBA ink(40, 25, 15);
	int lineY = (height - static_cast<int>(lines.size()) * lineHeight) / 2;
	for(const auto & line : lines)
	{
		to.drawText(origin + Point(22, lineY), FONT_TINY, ink, ETextAlignment::TOPLEFT, line);
		lineY += lineHeight;
	}
}

void CSpellWindow::updateContextRibbon()
{
	if(!enhancedLayout)
		return;
	contextRibbons[0]->setEnabled(battleSpellsOnly);
	contextRibbons[1]->setEnabled(!battleSpellsOnly);
	if(contextArea)
	{
		const auto text = GAME->translator().translate(battleSpellsOnly ? "vcmi.spellBook.switchToAdventure" : "vcmi.spellBook.switchToCombat");
		contextArea->setHelp(text, text);
	}
}

// Ordering of spell school tabs in SpelTab.def
static const std::array schoolTabOrder =
{
	SpellSchool::AIR,
	SpellSchool::FIRE,
	SpellSchool::WATER,
	SpellSchool::EARTH,
	SpellSchool::ANY
};

int getAnimFrameFromSchool(SpellSchool school)
{
	auto it = std::find(schoolTabOrder.begin(), schoolTabOrder.end(), school);
	if (it != schoolTabOrder.end())
		return std::distance(schoolTabOrder.begin(), it);
	else
		return -1;
}

bool isLegacySpellSchool(SpellSchool school)
{
	return getAnimFrameFromSchool(school) != -1;
}

class SpellbookSchoolTabs final : public CIntObject
{
	std::shared_ptr<CAnimation> animation;
	std::shared_ptr<IImage> inactive;
	int primary = 4;
	std::array<bool, 5> extra{};

public:
	explicit SpellbookSchoolTabs(Point position)
		: CIntObject(0, position),
		animation(ENGINE->renderHandler().loadAnimation(AnimationPath::builtin("SpelTab"), EImageBlitMode::COLORKEY))
	{
		const auto dimensions = animation->getImage(0)->dimensions();
		pos.w = dimensions.x;
		pos.h = dimensions.y;
	}

	void setSelection(SpellSchool school, const std::vector<SpellSchool> & additional)
	{
		primary = getAnimFrameFromSchool(school);
		if(primary < 0 && !inactive)
			inactive = ENGINE->renderHandler().loadImage(ImagePath::builtin("SpelTabNone.png"), EImageBlitMode::COLORKEY);
		extra.fill(false);
		for(const auto id : additional)
		{
			const int frame = getAnimFrameFromSchool(id);
			if(frame >= 0)
				extra[frame] = true;
		}
	}

	void showAll(Canvas & to) override
	{
		const auto base = primary >= 0 ? animation->getImage(primary) : inactive;
		if(std::none_of(extra.begin(), extra.end(), [](bool selected){ return selected; }))
		{
			to.draw(base, pos.topLeft());
			return;
		}

		// The rows meet in transparent gaps common to every SpelTab frame.
		static constexpr std::array rows = {0, 55, 114, 177, 239, 294};
		static constexpr std::array frames = {0, 3, 1, 2, 4};
		for(size_t row = 0; row < frames.size(); ++row)
		{
			const int frame = frames[row];
			const auto art = extra[frame] ? animation->getImage(frame) : base;
			const Rect slice(0, rows[row], pos.w, rows[row + 1] - rows[row]);
			to.draw(art, pos.topLeft() + slice.topLeft(), slice);
		}
	}
};

CSpellWindow::InteractiveArea::InteractiveArea(const Rect & myRect, const std::function<void()> & funcL, int helpTextId, CSpellWindow * _owner, bool turnOnRelease)
	: turnOnRelease(turnOnRelease)
{
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
	pos = myRect;
	onLeft = funcL;
	hoverText = LIBRARY->generaltexth->zelp[helpTextId].first;
	helpText = LIBRARY->generaltexth->zelp[helpTextId].second;
	owner = _owner;
}

CSpellWindow::InteractiveArea::InteractiveArea(const Rect & myRect, const std::function<void()> & funcL, std::string textId, CSpellWindow * _owner)
{
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
	pos = myRect;
	onLeft = funcL;
	auto hoverTextTmp = MetaString::createFromTextID("vcmi.spellBook.tab.hover");
	hoverTextTmp.replaceTextID(textId);
	hoverText = hoverTextTmp.toString(&GAME->translator());
	auto helpTextTmp = MetaString::createFromTextID("vcmi.spellBook.tab.help");
	helpTextTmp.replaceTextID(textId);
	helpText = helpTextTmp.toString(&GAME->translator());
	owner = _owner;
}

CSpellWindow::InteractiveArea::InteractiveArea(const Rect & myRect, const std::function<void()> & funcL, CSpellWindow * _owner):
	onLeft(funcL),
	owner(_owner)
{
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
	pos = myRect;
}

void CSpellWindow::InteractiveArea::setHelp(const std::string & hover, const std::string & help)
{
	const bool refreshHover = hoverText != hover && isHovered();
	if(refreshHover && !hoverText.empty() && !hover.empty() && owner->hoverHelp == hoverText)
	{
		owner->hoverHelp = hover;
		owner->redraw();
	}
	hoverText = hover;
	helpText = help;
	if(refreshHover)
		this->hover(true);
}

void CSpellWindow::InteractiveArea::setSchoolAction(SpellSchool school)
{
	onRight = [this, school]{ owner->addSchool(school); };
	hoverText += "  " + GAME->translator().translate("vcmi.spellBook.schoolHelp");
}

void CSpellWindow::InteractiveArea::setHitExclusion(std::function<bool(const Point &)> predicate)
{
	excludesPoint = std::move(predicate);
}

bool CSpellWindow::InteractiveArea::receiveEvent(const Point & position, int eventType) const
{
	return (!excludesPoint || !excludesPoint(position)) && CIntObject::receiveEvent(position, eventType);
}

void CSpellWindow::InteractiveArea::clickPressed(const Point &)
{
	if(!turnOnRelease)
		activateLeft();
}

void CSpellWindow::InteractiveArea::clickReleased(const Point &)
{
	if(turnOnRelease)
		activateLeft();
}

void CSpellWindow::InteractiveArea::activateLeft()
{
	ENGINE->input().hapticFeedback();
	if(owner->enhancedLayout)
		ENGINE->sound().playSound(soundBase::button);
	onLeft();
}

void CSpellWindow::InteractiveArea::showPopupWindow(const Point & cursorPosition)
{
	if(onRight)
	{
		ENGINE->sound().playSound(soundBase::button);
		onRight();
	}
	else
		CRClickPopup::createAndPush(helpText);
}

void CSpellWindow::InteractiveArea::hover(bool on)
{
	if(on)
		owner->statusBar->write(hoverText);
	else
		owner->statusBar->clear();
}

class SpellbookSpellSorter
{
public:
	bool operator()(const CSpell * A, const CSpell * B)
	{
		if(A->getLevel() < B->getLevel())
			return true;
		if(A->getLevel() > B->getLevel())
			return false;

		for (const auto schoolId : LIBRARY->spellSchoolHandler->getAllObjects())
		{
			if(A->schools.count(schoolId) && !B->schools.count(schoolId))
				return true;
			if(!A->schools.count(schoolId) && B->schools.count(schoolId))
				return false;
		}

		return TextOperations::compareLocalizedStrings(A->getNameTranslated(), B->getNameTranslated());
	}
};

CSpellWindow::CSpellWindow(const CGHeroInstance * _myHero, CPlayerInterface * _myInt, bool openOnBattleSpells, const std::function<void(SpellID)> & onSpellSelect, const CGTownInstance * archiveTown):
	CWindowObject(archiveTown ? SHADOW_DISABLED : PLAYER_COLORED | (settings["gameTweaks"]["enableLargeSpellbook"].Bool() ? BORDERED : 0)),
	battleSpellsOnly(openOnBattleSpells),
	selectedTab(SpellSchool::ANY),
	currentPage(0),
	myHero(_myHero),
	myInt(_myInt),
	openOnBattleSpells(openOnBattleSpells),
	onSpellSelect(onSpellSelect),
	isBigSpellbook(archiveTown || settings["gameTweaks"]["enableLargeSpellbook"].Bool()),
	spellsPerPage(24),
	offL(-11),
	offR(195),
	offRM(110),
	offT(-37),
	offB(56)
{
	OBJECT_CONSTRUCTION;
	if(archiveTown)
	{
		const JsonNode config(JsonPath::builtin("config/widgets/arcaneArchive.json"));
		const auto readIntensity = [&](const char * field, double fallback)
		{
			const auto & value = config[field];
			if(value.isNull())
				return fallback;
			if(!value.isNumber() || !std::isfinite(value.Float()) || value.Float() < 0 || value.Float() > 1)
			{
				logGlobal->warn("Arcane Archive: config/widgets/arcaneArchive.json %s must be a number from 0 to 1; using %f", field, fallback);
				return fallback;
			}
			return value.Float();
		};
		archiveDarkness = readIntensity("darkness", archiveDarkness);
		archiveBlurMix = readIntensity("blurMix", archiveBlurMix);
		archiveOwner = archiveTown->getOwner();
		archiveSpells = myInt->cb->getArcaneArchive(*archiveOwner);
		personalArchive = myHero && myHero->hasSpellbook() && myInt->cb->getSettings().getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER)
			&& myInt->cb->getSettings().getBoolean(EGameSettings::SPELLS_ARCANE_ARCHIVE_PERSONAL);
	}
	const bool hasPersonalSpellbook = myHero && (!archiveTown || personalArchive);

	int maxCustomSchools = (isBigSpellbook ? MAX_CUSTOM_SPELL_SCHOOLS_BIG : MAX_CUSTOM_SPELL_SCHOOLS) * 2;
	int customSchoolsAvailable = 0;
	std::vector<SpellSchool> sortedSchools = LIBRARY->spellSchoolHandler->getAllObjects();
	std::ranges::sort(sortedSchools, [&](SpellSchool a, SpellSchool b) {
		auto cnt = [&](SpellSchool s) {
			return std::ranges::count_if(LIBRARY->spellh->objects, [&](auto const & sp) {
				return (archiveOwner ? archiveSpells.count(sp->getId()) != 0 : ((!sp->isSpecial() && myHero->spellbookContainsSpell(sp->getId())) || myHero->canCastThisSpell(sp.get())))
					&& sp->schools.count(s);
			});
		};
		return cnt(a) > cnt(b);
	});
	for(const auto schoolId : sortedSchools)
		if(
			!isLegacySpellSchool(schoolId) &&
			!LIBRARY->spellSchoolHandler->getById(schoolId)->getSchoolBookmarkPath().empty() &&
			!LIBRARY->spellSchoolHandler->getById(schoolId)->getSchoolHeaderPath().empty()
		)
		{
			customSchoolsAvailable++;
			if(customSpellSchools.size() < maxCustomSchools)
				customSpellSchools.push_back(schoolId);
		}

	if(customSchoolsAvailable > maxCustomSchools)
		logGlobal->warn("Too many custom spell schools (%d) — showing only first %d", customSchoolsAvailable, maxCustomSchools);

	if(isBigSpellbook)
	{
		if(archiveTown)
			background = std::make_shared<CPicture>(ImagePath::builtin("arcaneArchive/spellbook"), Point(0, 0), EImageBlitMode::SIMPLE);
		else
			background = std::make_shared<CPicture>(ImagePath::builtin("SpellBookLarge"), 0, 0);
		updateShadow();
	}
	else
	{
		background = std::make_shared<CPicture>(ImagePath::builtin("SpelBack"), 0, 0);
		offL = offR = offT = offB = offRM = 0;
		spellsPerPage = 12;
	}

	if(!archiveTown)
		background->setPlayerColor(GAME->interface()->playerID);

	const std::string variant = archiveTown ? "archive" : isBigSpellbook ? "large" : "normal";
	const auto layoutConfigPath = JsonPath::builtin("config/spellbookUI.json");
	if(settings["general"]["enableUiEnhancements"].Bool() && CResourceHandler::get()->existsResource(layoutConfigPath))
	{
		const JsonNode layoutConfig(layoutConfigPath);
		enhancedLayout = layoutConfig["enabled"].Bool()
			&& CResourceHandler::get()->existsResource(ImagePath::builtin("SPRITES/" + spellbookArt + "paper/" + variant + "-overlay.png"));
		positionedHelp = enhancedLayout && layoutConfig["positionedTooltips"].Bool();
	}
	if(enhancedLayout)
		buildEnhancedBackground(archiveTown != nullptr);

	pos = background->center(Point(pos.w/2 + pos.x, pos.h/2 + pos.y));

	Rect r(90, archiveTown ? 443 : (isBigSpellbook ? 480 : 420), isBigSpellbook ? 160 : 110, 16);
	if(enhancedLayout)
	{
		const Point searchOrigin(90, isBigSpellbook ? 478 : 415);
		const int width = isBigSpellbook ? 300 : 224;
		auto frame = std::make_shared<CAnimImage>(AnimationPath::builtin(spellbookArt + "search/" + (isBigSpellbook ? "large" : "normal")), 0, 0, searchOrigin.x, searchOrigin.y);
		searchQuill = std::make_shared<CPicture>(ImagePath::builtin(spellbookArt + "search/quill.png"), searchOrigin + Point(17, 8), EImageBlitMode::SIMPLE);
		searchQuill->removeUsedEvents(LCLICK | SHOW_POPUP);
		r = Rect(searchOrigin + Point(33, 6), Point(width - 58, 16));
		searchBoxDescription = std::make_shared<CLabel>(r.x, r.center().y, FONT_TIMES, ETextAlignment::CENTERLEFT,
			ColorRGBA(100, 76, 48), GAME->translator().translate("vcmi.spellBook.searchPlaceholder"), r.w);
		searchBox = std::make_shared<SpellbookSearchInput>(r, frame, searchBoxDescription);
		searchBox->setCallback(std::bind(&CSpellWindow::searchInput, this));
		clearSearch = std::make_shared<CPicture>(ImagePath::builtin(spellbookArt + "search/clear.png"), searchOrigin + Point(width - 22, 5), EImageBlitMode::SIMPLE);
		clearSearch->addLClickCallback([this]
		{
			ENGINE->sound().playSound(soundBase::button);
			searchBox->setText("");
			searchBox->giveFocus();
			searchInput();
		});
		clearSearch->disable();
		buildLevelTabs();
		const Point pageCenter(background->pos.w / 2, isBigSpellbook ? 248 : 238);
		emptyResults = std::make_shared<SpellbookEmptyResults>(pageCenter, levelHelpBackground);
		emptyResults->disable();
	}
	else if(settings["general"]["enableUiEnhancements"].Bool())
	{
		const ColorRGBA rectangleColor = ColorRGBA(0, 0, 0, 75);
		const ColorRGBA borderColor = ColorRGBA(128, 100, 75);
		const ColorRGBA grayedColor = ColorRGBA(158, 130, 105);
		searchBoxRectangle = std::make_shared<TransparentFilledRectangle>(r.resize(1), rectangleColor, borderColor);
		searchBoxDescription = std::make_shared<CLabel>(r.center().x, r.center().y, FONT_SMALL, ETextAlignment::CENTER, grayedColor, LIBRARY->generaltexth->translate("vcmi.spellBook.search"));

		searchBox = std::make_shared<CTextInput>(r, FONT_SMALL, ETextAlignment::CENTER, false);
		searchBox->setCallback(std::bind(&CSpellWindow::searchInput, this));
	}

	if(onSpellSelect)
	{
		Point boxPos = enhancedLayout ? Point(89, isBigSpellbook ? 514 : 448) : r.bottomLeft() + Point(-2, 5);
		showAllSpells = std::make_shared<CToggleButton>(boxPos, AnimationPath::builtin("sysopchk.def"), CButton::tooltip(LIBRARY->generaltexth->translate("core.help.458.hover"), LIBRARY->generaltexth->translate("core.help.458.hover")), [this](bool state){ searchInput(); });
		showAllSpellsDescription = std::make_shared<CLabel>(boxPos.x + 40, boxPos.y + 12, FONT_SMALL, ETextAlignment::CENTERLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("core.help.458.hover"));
	}
	// A save can retain the LNCL rule after the module has been disabled in the active preset.
	else if(hasPersonalSpellbook
		&& LIBRARY->engineSettings()->getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER)
		&& myInt->cb->getSettings().getBoolean(EGameSettings::SPELLS_LEARN_NOW_CAST_LATER))
	{
		const Point bookmarkPosition = enhancedLayout ? footerRibbon(isBigSpellbook, 0).topLeft() : Point(isBigSpellbook ? 379 : 288, isBigSpellbook ? 465 : 406);
		showOnlyCastableSpells = myInt->localState->getShowOnlyCastableSpells(myHero->id);
		const Rect bookmarkArtwork(417, 406, 37, 45);
		// The mana ribbon is blank in SpelBack; its number is a separate label.
		// Reuse these original pixels, just as createBigSpellBook does, without scaling.
		if(enhancedLayout)
		{
			auto spellbook = ENGINE->renderHandler().loadImage(ImagePath::builtin("SpelBack"), EImageBlitMode::OPAQUE);
			availabilityBookmark = makeSpellbookRibbon(spellbook, bookmarkArtwork, bookmarkPosition);
		}
		else if(archiveTown)
			availabilityBookmark = std::make_shared<CPicture>(background->getSurface(), Rect(575, 465, 37, 45), bookmarkPosition.x, bookmarkPosition.y);
		else
		{
			auto spellbook = ENGINE->renderHandler().loadImage(ImagePath::builtin("SpelBack"), EImageBlitMode::OPAQUE);
			availabilityBookmark = std::make_shared<CPicture>(spellbook, bookmarkArtwork, bookmarkPosition.x, bookmarkPosition.y);
		}
		availabilityBookmark->removeUsedEvents(LCLICK | SHOW_POPUP);
		const std::array bookmarkIcons = {"spellbook/allSpellsIcon.png", "spellbook/castableSpellsIcon.png"};
		for(size_t i = 0; i < availabilityBookmarkIcons.size(); ++i)
		{
			availabilityBookmarkIcons[i] = std::make_shared<CPicture>(ImagePath::builtin(bookmarkIcons[i]), bookmarkPosition + Point(7, 5));
			availabilityBookmarkIcons[i]->scaleTo(Point(23, 23));
			availabilityBookmarkIcons[i]->removeUsedEvents(LCLICK | SHOW_POPUP);
		}
		availabilityBookmarkIcons[1]->disable();
		availabilityBookmarkArea = std::make_shared<InteractiveArea>(Rect(bookmarkPosition + pos.topLeft(), bookmarkArtwork.dimensions()), std::bind(&CSpellWindow::toggleSpellAvailability, this), this);
		updateAvailabilityBookmark();
	}

	if(enhancedLayout && !onSpellSelect
		&& CResourceHandler::get()->existsResource(ImagePath::builtin("SPRITES/" + favoritesArt + "star-0.png")))
		favorites = std::make_shared<Favorites>(*this, hasPersonalSpellbook ? myHero : nullptr);
	processSpells();

	//numbers of spell pages computed

	const auto pageArt = [](const std::string & path)
	{
		return ImagePath::builtin(spellbookArt + "page/" + path + ".png");
	};
	const bool customCorners = enhancedLayout && CResourceHandler::get()->existsResource(pageArt("previous").addPrefix("SPRITES/"));
	if(customCorners)
	{
		leftCorner = std::make_shared<CPicture>(pageArt("previous"), Point(isBigSpellbook ? 90 : 91, 77 + offT), EImageBlitMode::SIMPLE);
		rightCorner = std::make_shared<CPicture>(pageArt("next"), Point(487 + offR, 72 + offT), EImageBlitMode::SIMPLE);
	}
	else
	{
		leftCorner = std::make_shared<CPicture>(ImagePath::builtin("SpelTrnL.bmp"), 97 + offL, 77 + offT);
		rightCorner = std::make_shared<CPicture>(ImagePath::builtin("SpelTrnR.bmp"), 487 + offR, 72 + offT);
	}

	schoolTab = std::make_shared<SpellbookSchoolTabs>(Point(524 + offR, 88));
	int customSchoolCount = customSpellSchools.size();
	int yStart = 93;
	int yEnd = yStart + (std::min(customSchoolCount, isBigSpellbook ? MAX_CUSTOM_SPELL_SCHOOLS_BIG : MAX_CUSTOM_SPELL_SCHOOLS) - 1) * 62;
	int denom = std::max(customSchoolCount - 1, 1);
	for(int i = 0; i < customSchoolCount; i++)
		schoolTabCustom.push_back(std::make_shared<CAnimImage>(LIBRARY->spellSchoolHandler->getById(customSpellSchools[i])->getSchoolBookmarkPath(), i == 0 ? 0 : 1, 0, isBigSpellbook ? 0 : 15, yStart + ((yEnd - yStart) * i) / denom));
	const int favoriteInset = favorites ? (isBigSpellbook ? 15 : 8) : 0;
	const int leftSpellColumn = 117 + offL + favoriteInset;
	// The ribbon reaches past the first spell column.
	const int schoolHeaderX = leftSpellColumn + (favorites ? (isBigSpellbook ? 6 : 5) : 0);
	schoolPicture = std::make_shared<CAnimImage>(AnimationPath::builtin("Schools"), 0, 0, schoolHeaderX, 74 + offT);

	const Point manaPosition = enhancedLayout ? footerRibbon(isBigSpellbook, 2).topLeft() + Point(18, 20) : isBigSpellbook ? Point(592, 485) : Point(434, 426);
	if(enhancedLayout)
	{
		const auto original = ENGINE->renderHandler().loadImage(ImagePath::builtin("SpelBack"), EImageBlitMode::OPAQUE);
		const auto contextPosition = footerRibbon(isBigSpellbook, 1).topLeft();
		contextRibbons[0] = makeSpellbookRibbon(original, Rect(220, 405, 37, 47), contextPosition, "combat");
		contextRibbons[1] = makeSpellbookRibbon(original, Rect(354, 406, 37, 41), contextPosition, "adventure");
		// Keep these pictures owned alongside the other footer controls.
		if(hasPersonalSpellbook)
			footerArtwork[0] = makeSpellbookRibbon(original, Rect(417, 406, 37, 45), footerRibbon(isBigSpellbook, 2).topLeft());
		const auto closePath = ImagePath::builtin(spellbookArt + "context/close-ribbon.png");
		if(CResourceHandler::get()->existsResource(closePath.addPrefix("SPRITES/")))
		{
			auto cloth = ENGINE->renderHandler().loadImage(closePath, EImageBlitMode::SIMPLE);
			auto symbol = ENGINE->renderHandler().loadImage(ImagePath::builtin(spellbookArt + "context/close-symbol.png"), EImageBlitMode::SIMPLE);
			footerArtwork[1] = std::make_shared<SpellbookLayeredPicture>(cloth, footerRibbon(isBigSpellbook, 3).topLeft(), symbol, Point(5, 6));
			footerArtwork[1]->removeUsedEvents(LCLICK | SHOW_POPUP);
		}
		else
			footerArtwork[1] = makeSpellbookRibbon(original, Rect(478, 406, 37, 47), footerRibbon(isBigSpellbook, 3).topLeft());
		updateContextRibbon();
	}
	mana = std::make_shared<CLabel>(manaPosition.x, manaPosition.y, enhancedLayout ? FONT_TINY : FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, hasPersonalSpellbook ? std::to_string(myHero->mana) : "");

	if(isBigSpellbook)
		statusBar = CGStatusBar::create(400, archiveTown ? 520 : 587);
	else
		statusBar = CGStatusBar::create(7, 569, ImagePath::builtin("Spelroll.bmp"));

	if(positionedHelp)
		statusBar->setHoverTextHandler([this](const std::string & text){ setHoverHelp(text); });

	Rect schoolRect( 549 + pos.x + offR, 94 + pos.y, 45, 35);
	if(enhancedLayout)
	{
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(footerRibbon(isBigSpellbook, 3) + pos.topLeft(), std::bind(&CSpellWindow::fexitb, this), 460, this));
		contextArea = std::make_shared<InteractiveArea>(footerRibbon(isBigSpellbook, 1) + pos.topLeft(), [this]
		{
			if(battleSpellsOnly)
				fadvSpellsb();
			else
				fbattleSpellsb();
		}, this);
		contextArea->setInputEnabled(!onSpellSelect);
		updateContextRibbon();
		if(hasPersonalSpellbook)
			manaArea = std::make_shared<InteractiveArea>(footerRibbon(isBigSpellbook, 2) + pos.topLeft(), std::bind(&CSpellWindow::fmanaPtsb, this), this);
	}
	else
	{
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(Rect(479 + pos.x + (isBigSpellbook ? 175 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fexitb, this), 460, this));
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(Rect(221 + pos.x + (isBigSpellbook ? 43 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fbattleSpellsb, this), 453, this));
		interactiveAreas.push_back(std::make_shared<InteractiveArea>(Rect(355 + pos.x + (isBigSpellbook ? 110 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fadvSpellsb, this), 452, this));
		manaArea = std::make_shared<InteractiveArea>(Rect(418 + pos.x + (isBigSpellbook ? 142 : 0), 405 + pos.y + offB, isBigSpellbook ? 60 : 36, 56), std::bind(&CSpellWindow::fmanaPtsb, this), this);
	}
	const auto addSchoolArea = [this](SpellSchool school, const Rect & rect, auto label)
	{
		auto area = std::make_shared<InteractiveArea>(rect, [this, school]{ selectSchool(school); }, label, this);
		if(enhancedLayout)
		{
			area->setSchoolAction(school);
		}
		interactiveAreas.push_back(area);
	};
	addSchoolArea(SpellSchool::AIR, schoolRect, 454);
	addSchoolArea(SpellSchool::EARTH, schoolRect + Point(0, 57), 457);
	addSchoolArea(SpellSchool::FIRE, schoolRect + Point(0, 116), 455);
	addSchoolArea(SpellSchool::WATER, schoolRect + Point(0, 176), 456);
	addSchoolArea(SpellSchool::ANY, schoolRect + Point(0, 236), 458);
	const int iaHeight = customSchoolCount > 1 ? std::min((yEnd - yStart) / denom, 60) : 60;
	for(int i = 0; i < customSchoolCount; ++i)
		addSchoolArea(customSpellSchools[i], Rect(schoolTabCustom[i]->pos.topLeft(), Point(80, iaHeight)),
			LIBRARY->spellSchoolHandler->getById(customSpellSchools[i])->getNameTextID());

	// Keep the ribbon's hit area stable until the page-turn click is released.
	leftCornerArea = std::make_shared<InteractiveArea>(leftCorner->pos, std::bind(&CSpellWindow::fLcornerb, this), 450, this, enhancedLayout);
	if(favorites)
		leftCornerArea->setHitExclusion([this](const Point & point){ return favorites->ribbonContains(point); });
	rightCornerArea = std::make_shared<InteractiveArea>(rightCorner->pos, std::bind(&CSpellWindow::fRcornerb, this), 451, this, enhancedLayout);

	//areas for spells
	int xpos = leftSpellColumn + pos.x;
	int ypos = 90 + offT + pos.y;

	for(int v=0; v<spellsPerPage; ++v)
	{
		spellAreas[v] = std::make_shared<SpellArea>( Rect(xpos, ypos, 65, 78), this);

		if(v == (spellsPerPage / 2) - 1) //to right page
		{
			xpos = offRM + 336 + pos.x - favoriteInset; ypos = 90 + offT + pos.y;
		}
		else
		{
			if(v%(isBigSpellbook ? 3 : 2) == 0 || (v%3 == 1 && isBigSpellbook))
			{
				xpos+=85;
			}
			else
			{
				xpos -= (isBigSpellbook ? 2 : 1)*85; ypos+=97;
			}
		}
	}

	SpellSchool school = battleSpellsOnly ? myInt->localState->getSpellbookSettings().spellbookLastTabBattle : myInt->localState->getSpellbookSettings().spellbookLastTabAdvmap;
	bool schoolFound = isLegacySpellSchool(school) || std::find(customSpellSchools.begin(), customSpellSchools.end(), school) != customSpellSchools.end();
	if(schoolFound)
		selectedTab = school;
	const bool rememberFavoriteView = favorites && !archiveTown;
	if(rememberFavoriteView)
		favorites->restoreView();
	setSchoolImages(selectedTab);
	int cp = battleSpellsOnly ? myInt->localState->getSpellbookSettings().spellbookLastPageBattle : myInt->localState->getSpellbookSettings().spellbookLastPageAdvmap;
	// spellbook last page battle index is not reset after battle, so this needs to stay here
	vstd::abetween(cp, 0, std::max(0, pagesWithinCurrentTab() - 1));
	if(!schoolFound || rememberFavoriteView)
		cp = 0;
	setCurrentPage(cp);
	computeSpellsPerArea();
	addUsedEvents(KEYBOARD);
	if(archiveTown)
		updateArchiveLayout();
}

CSpellWindow::~CSpellWindow()
{
	statusBar->setHoverTextHandler({});
}

void CSpellWindow::searchInput()
{
	if(searchBox)
		searchBoxDescription->setEnabled(searchBox->getText().empty() && (!enhancedLayout || !searchBox->hasFocus()));
	if(clearSearch)
		clearSearch->setEnabled(!searchBox->getText().empty());
	refreshSpells(0);
}

void CSpellWindow::refreshSpells(int page)
{
	processSpells();
	setCurrentPage(std::clamp(page, 0, std::max(0, pagesWithinCurrentTab() - 1)));
	computeSpellsPerArea();
}

bool CSpellWindow::isSpellUnavailable(const CSpell * spell) const
{
	if(!myHero || (archiveOwner && !personalArchive))
		return false;
	return !myHero->canCastThisSpell(spell) || myInt->cb->getSpellCost(spell, myHero) > myHero->mana;
}

bool CSpellWindow::isSpellBlockedByWisdom(const CSpell * spell) const
{
	return myHero && (!archiveOwner || personalArchive) && myHero->spellbookContainsSpell(spell->getId())
		&& spell->getLevel() > myHero->maxCastableSpellLevel()
		&& !myHero->canCastThisSpell(spell);
}

void CSpellWindow::updateAvailabilityBookmark()
{
	availabilityBookmarkIcons[0]->setEnabled(!showOnlyCastableSpells);
	availabilityBookmarkIcons[1]->setEnabled(showOnlyCastableSpells);
	availabilityBookmarkArea->setHelp(
		LIBRARY->generaltexth->translate(showOnlyCastableSpells ? "vcmi.spellBook.castableSpellsHover" : "vcmi.spellBook.allSpellsHover"),
		LIBRARY->generaltexth->translate("vcmi.spellBook.availabilityHelp"));
}

void CSpellWindow::toggleSpellAvailability()
{
	showOnlyCastableSpells = !showOnlyCastableSpells;
	myInt->localState->setShowOnlyCastableSpells(myHero->id, showOnlyCastableSpells);
	updateAvailabilityBookmark();
	searchInput();
}

void CSpellWindow::processSpells()
{
	mySpells.clear();
	sitesPerTabAdv.clear(); // hold page counts of previous run - would be added up otherwise
	sitesPerTabBattle.clear();
	if(favorites)
		favorites->resetCounts();

	//initializing castable spells
	mySpells.reserve(LIBRARY->spellh->objects.size());
	for(auto const & spell : LIBRARY->spellh->objects)
	{
		if(spell->isCreatureAbility())
			continue;
		bool available;
		if(archiveOwner)
			available = archiveSpells.contains(spell->id);
		else if(onSpellSelect)
			available = spell->isCombat() == openOnBattleSpells
				&& (myHero->canCastThisSpell(spell.get()) || (showAllSpells->isSelected() && !spell->isSpecial()));
		else
			available = (!spell->isSpecial() && myHero->spellbookContainsSpell(spell->id)) || myHero->canCastThisSpell(spell.get());
		if(!available || (showOnlyCastableSpells && isSpellBlockedByWisdom(spell.get())))
			continue;
		if(favorites && !favorites->include(*spell))
			continue;
		const int level = spell->getLevel();
		if(selectedLevels && (level < 1 || level > 5 || !(selectedLevels & (1u << level))))
			continue;
		if(!searchBox || TextOperations::isFuzzyMatch(searchBox->getText(), spell->getNameTranslated()))
			mySpells.push_back(spell.get());
	}

	SpellbookSpellSorter spellsorter;
	std::sort(mySpells.begin(), mySpells.end(), spellsorter);

	for(const auto spell : mySpells)
	{
		auto& sitesPerOurTab = spell->isCombat() ? sitesPerTabBattle : sitesPerTabAdv;

		++sitesPerOurTab[SpellSchool::ANY];

		spell->forEachSchool([&sitesPerOurTab](const SpellSchool & school, bool & stop)
		{
			++sitesPerOurTab[school];
		});
	}
	if(sitesPerTabAdv[SpellSchool::ANY] % spellsPerPage == 0)
		sitesPerTabAdv[SpellSchool::ANY]/=spellsPerPage;
	else
		sitesPerTabAdv[SpellSchool::ANY] = sitesPerTabAdv[SpellSchool::ANY]/spellsPerPage + 1;

	for(const auto v : LIBRARY->spellSchoolHandler->getAllObjects())
	{
		if(v == SpellSchool::ANY)
			continue;
		if(sitesPerTabAdv[v] <= spellsPerPage - 2)
			sitesPerTabAdv[v] = 1;
		else
		{
			if((sitesPerTabAdv[v] - (spellsPerPage - 2)) % spellsPerPage == 0)
				sitesPerTabAdv[v] = (sitesPerTabAdv[v] - (spellsPerPage - 2)) / spellsPerPage + 1;
			else
				sitesPerTabAdv[v] = (sitesPerTabAdv[v] - (spellsPerPage - 2)) / spellsPerPage + 2;
		}
	}

	if(sitesPerTabBattle[SpellSchool::ANY] % spellsPerPage == 0)
		sitesPerTabBattle[SpellSchool::ANY]/=spellsPerPage;
	else
		sitesPerTabBattle[SpellSchool::ANY] = sitesPerTabBattle[SpellSchool::ANY]/spellsPerPage + 1;

	for(const auto v : LIBRARY->spellSchoolHandler->getAllObjects())
	{
		if(v == SpellSchool::ANY)
			continue;
		if(sitesPerTabBattle[v] <= spellsPerPage - 2)
			sitesPerTabBattle[v] = 1;
		else
		{
			if((sitesPerTabBattle[v] - (spellsPerPage - 2)) % spellsPerPage == 0)
				sitesPerTabBattle[v] = (sitesPerTabBattle[v] - (spellsPerPage - 2)) / spellsPerPage + 1;
			else
				sitesPerTabBattle[v] = (sitesPerTabBattle[v] - (spellsPerPage - 2)) / spellsPerPage + 2;
		}
	}
}

void CSpellWindow::fexitb()
{
	if(favorites && favorites->cancelAdding())
		return;
	if(favorites)
		favorites->saveView();
	if(archiveOwner)
	{
		close();
		return;
	}
	auto spellBookState = myInt->localState->getSpellbookSettings();
	if(myInt->battleInt)
	{
		spellBookState.spellbookLastTabBattle = selectedTab;
		spellBookState.spellbookLastPageBattle = currentPage;
	}
	else
	{
		spellBookState.spellbookLastTabAdvmap = selectedTab;
		spellBookState.spellbookLastPageAdvmap = currentPage;
	}
	myInt->localState->setSpellbookSettings(spellBookState);

	if(onSpellSelect)
		onSpellSelect(SpellID::NONE);

	close();
}

void CSpellWindow::fadvSpellsb()
{
	if(enhancedLayout && onSpellSelect)
		return;
	if(battleSpellsOnly == true)
	{
		turnPageRight();
		battleSpellsOnly = false;
		updateContextRibbon();
		setCurrentPage(0);
	}
	computeSpellsPerArea();
}

void CSpellWindow::fbattleSpellsb()
{
	if(enhancedLayout && onSpellSelect)
		return;
	if(battleSpellsOnly == false)
	{
		turnPageLeft();
		battleSpellsOnly = true;
		updateContextRibbon();
		setCurrentPage(0);
	}
	computeSpellsPerArea();
}

void CSpellWindow::toggleSearchBoxFocus()
{
	if(searchBox != nullptr)
	{
		searchBox->hasFocus() ? searchBox->removeFocus() : searchBox->giveFocus();
	}
}

void CSpellWindow::fmanaPtsb()
{
}

void CSpellWindow::selectSchool(SpellSchool school)
{
	if(selectedTab != school || !additionalSchools.empty())
	{
		additionalSchools.clear();
		if(selectedTab < school)
			turnPageLeft();
		else
			turnPageRight();
		selectedTab = school;
		setSchoolImages(selectedTab);
		setCurrentPage(0);
	}
	computeSpellsPerArea();
}

bool CSpellWindow::matchesSchool(const CSpell & spell) const
{
	return selectedTab == SpellSchool::ANY || spell.schools.count(selectedTab)
		|| std::any_of(additionalSchools.begin(), additionalSchools.end(), [&](SpellSchool school){ return spell.schools.count(school); });
}

void CSpellWindow::addSchool(SpellSchool school)
{
	if(school == SpellSchool::ANY || selectedTab == SpellSchool::ANY)
	{
		selectSchool(school);
		return;
	}

	if(school != selectedTab && !vstd::contains(additionalSchools, school))
	{
		additionalSchools.push_back(school);
		setCurrentPage(0);
		computeSpellsPerArea();
	}
}

void CSpellWindow::fLcornerb()
{
	if(currentPage>0)
	{
		turnPageLeft();
		setCurrentPage(currentPage - 1);
	}
	computeSpellsPerArea();
}

void CSpellWindow::fRcornerb()
{
	if((currentPage + 1) < (pagesWithinCurrentTab()))
	{
		turnPageRight();
		setCurrentPage(currentPage + 1);
	}
	computeSpellsPerArea();
}

void CSpellWindow::show(Canvas & to)
{
	if(video)
		video->show(to);
	statusBar->show(to);
}

void CSpellWindow::showAll(Canvas & to)
{
	if(archiveBackdrop)
	{
		CanvasClipRectGuard clip(to, pos);
		if(!archiveBlurredBackdrop || archiveBlurMix < 1)
			to.draw(archiveBackdrop, pos.topLeft() + archiveBackdropPosition);
		if(archiveBlurredBackdrop && archiveBlurMix > 0)
		{
			archiveBlurredBackdrop->setAlpha(static_cast<uint8_t>(std::lround(archiveBlurMix * 255)));
			to.draw(archiveBlurredBackdrop, pos.topLeft() + archiveBackdropPosition);
		}
		if(archiveDarkness > 0)
			to.drawColorBlended(pos, ColorRGBA(0, 0, 0, static_cast<uint8_t>(std::lround(archiveDarkness * 255))));
	}
	if(archiveOwner)
	{
		auto current = myInt->cb->getArcaneArchive(*archiveOwner);
		if(current != archiveSpells)
		{
			archiveSpells = std::move(current);
			searchInput();
		}
	}
	CWindowObject::showAll(to);
	if(archiveOwner)
	{
		const Rect frame = Rect(pos.x - 14, pos.y - 15, pos.w + 28, pos.h + 29)
			.intersect(Rect(Point(0, 0), ENGINE->screenDimensions()));
		CMessage::drawBorder(*archiveOwner, to, frame.w, frame.h, frame.x, frame.y);
	}
	drawHoverHelp(to);
}

void CSpellWindow::updateArchiveLayout()
{
	const Point screen = ENGINE->screenDimensions();
	const int baseWidth = std::clamp(screen.x - 64, 800, 1120);
	const int extraWidth = std::max(screen.x - 1280, 0) / 2;
	const Point viewport(std::min(baseWidth + extraWidth, 1400), std::clamp(screen.y - 66, 600, 800));
	center(Rect(Point(0, 0), viewport));
	// Centre the visible cover near 54% height, reserving room for the lower controls.
	const int wideScreenOffset = std::min(extraWidth / 25, 8);
	const int footerReserve = favorites && screen.y - viewport.y < 30 ? 550 : 535;
	const int bookY = std::min(viewport.y * 54 / 100 - 252 + wideScreenOffset, viewport.y - footerReserve);
	const Point bookOffset((viewport.x - 800) / 2, bookY);
	for(auto * child : children)
		child->moveBy(bookOffset - archiveBookOffset);
	archiveBookOffset = bookOffset;
	updateArchiveBackdrop();
}

void CSpellWindow::updateArchiveBackdrop()
{
	const int scaling = ENGINE->screenHandler().getScalingFactor();
	if(archiveBackdrop && archiveBackdropSize == pos.dimensions() && archiveBackdropScaling == scaling)
		return;
	archiveBackdrop.reset();
	archiveBlurredBackdrop.reset();
	if(!CResourceHandler::get()->existsResource(ResourcePath("SPRITES/arcaneArchive/library", EResType::IMAGE)))
	{
		logGlobal->warn("Arcane Archive: missing SPRITES/arcaneArchive/library; using the spellbook without a backdrop");
		return;
	}
	auto originalImage = ENGINE->renderHandler().loadImage(ImagePath::builtin("arcaneArchive/library"), EImageBlitMode::OPAQUE);
	const Point viewport = pos.dimensions();
	const Point original = originalImage->dimensions();
	const double scale = std::max(static_cast<double>(viewport.x) / original.x, static_cast<double>(viewport.y) / original.y);
	const Point scaled(static_cast<int>(std::ceil(original.x * scale)), static_cast<int>(std::ceil(original.y * scale)));
	const auto prepare = [&](const std::shared_ptr<IImage> & source)
	{
		auto cached = ENGINE->renderHandler().createImage(source->dimensions(), CanvasScalingPolicy::AUTO);
		cached->getCanvas().draw(source, Point(0, 0));
		cached->scaleTo(scaled, EScalingAlgorithm::BILINEAR);
		return cached;
	};
	archiveBackdrop = prepare(originalImage);
	if(CResourceHandler::get()->existsResource(ResourcePath("SPRITES/arcaneArchive/libraryBlurred", EResType::IMAGE)))
	{
		auto blurred = ENGINE->renderHandler().loadImage(ImagePath::builtin("arcaneArchive/libraryBlurred"), EImageBlitMode::OPAQUE);
		if(blurred->dimensions() == original)
			archiveBlurredBackdrop = prepare(blurred);
		else
			logGlobal->warn("Arcane Archive: SPRITES/arcaneArchive/libraryBlurred dimensions differ from library; using sharp background");
	}
	else
		logGlobal->warn("Arcane Archive: missing SPRITES/arcaneArchive/libraryBlurred; using sharp background");
	archiveBackdropPosition = (viewport - scaled) / 2;
	archiveBackdropSize = viewport;
	archiveBackdropScaling = scaling;
}

void CSpellWindow::onScreenResize()
{
	if(archiveOwner)
		updateArchiveLayout();
	else
		CWindowObject::onScreenResize();
}

std::string CSpellWindow::archiveDescription(const CSpell * spell) const
{
	std::string result = spell->getDescriptionTranslated(personalArchive && myHero ? myHero->getSpellSchoolLevel(spell) : 0);
	result += "\n\n" + LIBRARY->generaltexth->translate("vcmi.arcaneArchive.sources");
	for(const auto townId : archiveSpells.at(spell->id))
		if(const auto * town = myInt->cb->getTown(townId))
			result += "\n" + GAME->translator().translate(town->getNameTextID());
	if(personalArchive && myHero)
	{
		result += "\n\n" + LIBRARY->generaltexth->translate(myHero->spellbookContainsSpell(spell->id)
			? "vcmi.arcaneArchive.known" : "vcmi.arcaneArchive.unknown");
		if(isSpellBlockedByWisdom(spell))
		{
			auto reason = MetaString::createFromTextID("vcmi.spellBook.wisdomRequired");
			reason.replaceNumber(spell->getLevel());
			reason.replaceNumber(myHero->maxCastableSpellLevel());
			result += "\n" + reason.toString(&GAME->translator());
		}
	}
	return result;
}

bool CSpellWindow::hasSchoolHeader() const
{
	return selectedTab != SpellSchool::ANY && additionalSchools.empty();
}

void CSpellWindow::computeSpellsPerArea()
{
	std::vector<const CSpell *> spellsCurSite;
	spellsCurSite.reserve(mySpells.size());
	for(const CSpell * spell : mySpells)
	{
		if(spell->isCombat() ^ !battleSpellsOnly
		   && matchesSchool(*spell)
			)
		{
			spellsCurSite.push_back(spell);
		}
	}

	if(!hasSchoolHeader())
	{
		if(spellsCurSite.size() > spellsPerPage)
		{
			spellsCurSite = std::vector<const CSpell *>(spellsCurSite.begin() + currentPage*spellsPerPage, spellsCurSite.end());
			if(spellsCurSite.size() > spellsPerPage)
			{
				spellsCurSite.erase(spellsCurSite.begin()+spellsPerPage, spellsCurSite.end());
			}
		}
	}
	else
	{
		if(spellsCurSite.size() > spellsPerPage - 2)
		{
			if(currentPage == 0)
			{
				spellsCurSite.erase(spellsCurSite.begin()+spellsPerPage-2, spellsCurSite.end());
			}
			else
			{
				spellsCurSite = std::vector<const CSpell *>(spellsCurSite.begin() + (currentPage-1)*spellsPerPage + spellsPerPage-2, spellsCurSite.end());
				if(spellsCurSite.size() > spellsPerPage)
				{
					spellsCurSite.erase(spellsCurSite.begin()+spellsPerPage, spellsCurSite.end());
				}
			}
		}
	}
	if(favorites)
		favorites->updateControls();
	if(emptyResults)
	{
		emptyResults->setEnabled(spellsCurSite.empty());
		if(favorites && spellsCurSite.empty())
			emptyResults->setText(favorites->emptyText());
	}
	//applying
	if(!hasSchoolHeader() || currentPage != 0)
	{
		for(size_t c=0; c<spellsPerPage; ++c)
		{
			if(c < spellsCurSite.size())
			{
				spellAreas[c]->setSpell(spellsCurSite[c]);
			}
			else
			{
				spellAreas[c]->setSpell(nullptr);
			}
		}
	}
	else
	{
		spellAreas[0]->setSpell(nullptr);
		spellAreas[1]->setSpell(nullptr);
		for(size_t c=0; c<spellsPerPage-2; ++c)
		{
			if(c < spellsCurSite.size())
				spellAreas[c+2]->setSpell(spellsCurSite[c]);
			else
				spellAreas[c+2]->setSpell(nullptr);
		}
	}
	redraw();
}

void CSpellWindow::setSchoolImages(SpellSchool school)
{
	OBJECT_CONSTRUCTION;

	schoolTab->setSelection(school, additionalSchools);

	for(size_t i = 0; i < schoolTabCustom.size(); ++i)
	{
		const auto id = customSpellSchools[i];
		schoolTabCustom[i]->setFrame(id == school || vstd::contains(additionalSchools, id) ? 0 : 1, 0);
		moveChildForeground(schoolTabCustom[i].get());
	}
	for(size_t i = 0; i < schoolTabCustom.size(); ++i)
		if(customSpellSchools[i] == school || vstd::contains(additionalSchools, customSpellSchools[i]))
			moveChildForeground(schoolTabCustom[i].get());

	const bool showHeader = hasSchoolHeader() && currentPage == 0;
	schoolPicture->visible = showHeader && isLegacySpellSchool(school);
	if(schoolPicture->visible)
		schoolPicture->setFrame(getAnimFrameFromSchool(school), 0);
	
	schoolPictureCustom.reset();
	if(showHeader && !isLegacySpellSchool(school))
	{
		schoolPictureCustom = std::make_shared<CPicture>(LIBRARY->spellSchoolHandler->getById(school)->getSchoolHeaderPath());
		schoolPictureCustom->moveTo(schoolPicture->pos.topLeft());
	}
}

void CSpellWindow::setCurrentPage(int value)
{
	currentPage = value;
	setSchoolImages(selectedTab);

	bool canTurnLeft = currentPage != 0;
	bool canTurnRight = currentPage + 1 < pagesWithinCurrentTab();

	leftCorner->setEnabled(canTurnLeft);
	rightCorner->setEnabled(canTurnRight);
	leftCornerArea->setEnabled(canTurnLeft);
	rightCornerArea->setEnabled(canTurnRight);

	mana->setText(myHero && (!archiveOwner || personalArchive) ? std::to_string(myHero->mana) : "");
	if(manaArea)
	{
		const auto manaDescription = myHero && (!archiveOwner || personalArchive) ? UIHelper::getHeroManaDescription(*myHero) : std::string();
		manaArea->setHelp(manaDescription, manaDescription);
	}
	ENGINE->fakeMouseMove(); // refresh hover text after updating page controls and mana
}

void CSpellWindow::turnPageLeft()
{
	OBJECT_CONSTRUCTION;
	if(settings["video"]["spellbookAnimation"].Bool() && !isBigSpellbook)
		video = std::make_shared<VideoWidgetOnce>(Point(13, 14), VideoPath::builtin("PGTRNLFT.SMK"), false, this);
}

void CSpellWindow::turnPageRight()
{
	OBJECT_CONSTRUCTION;
	if(settings["video"]["spellbookAnimation"].Bool() && !isBigSpellbook)
		video = std::make_shared<VideoWidgetOnce>(Point(13, 14), VideoPath::builtin("PGTRNRGH.SMK"), false, this);
}

void CSpellWindow::onVideoPlaybackFinished()
{
	video.reset();
	redraw();
}

void CSpellWindow::keyPressed(EShortcut key)
{
	switch(key)
	{
		case EShortcut::GLOBAL_RETURN:
			fexitb();
			break;

		case EShortcut::MOVE_LEFT:
			fLcornerb();
			break;
		case EShortcut::MOVE_RIGHT:
			fRcornerb();
			break;
		case EShortcut::MOVE_UP:
		case EShortcut::MOVE_DOWN:
		{
			bool down = key == EShortcut::MOVE_DOWN;
			static const std::array legacyOrder = { SpellSchool::AIR, SpellSchool::EARTH, SpellSchool::FIRE, SpellSchool::WATER, SpellSchool::ANY };

			auto order = customSpellSchools;
			order.insert(order.begin(), legacyOrder.begin(), legacyOrder.end());

			int idx = std::distance(order.begin(), std::find(order.begin(), order.end(), selectedTab));
			idx = (idx + (down ? 1 : -1) + static_cast<int>(order.size())) % static_cast<int>(order.size());
			if(selectedTab != order[idx])
				selectSchool(order[idx]);
			break;
		}
		case EShortcut::SPELLBOOK_TAB_COMBAT:
			fbattleSpellsb();
			break;
		case EShortcut::SPELLBOOK_TAB_ADVENTURE:
			fadvSpellsb();
			break;
		case EShortcut::SPELLBOOK_SEARCH_FOCUS:
			toggleSearchBoxFocus();
			break;
		case EShortcut::SPELLBOOK_TOGGLE_FAVORITES:
			if(favorites && (!searchBox || !searchBox->hasFocus()))
			{
				ENGINE->sound().playSound(soundBase::button);
				favorites->toggleView();
			}
			break;
	}
}

int CSpellWindow::pagesWithinCurrentTab()
{
	if(!additionalSchools.empty())
	{
		const auto count = std::count_if(mySpells.begin(), mySpells.end(), [&](const CSpell * spell)
		{
			return spell->isCombat() == battleSpellsOnly && matchesSchool(*spell);
		});
		return static_cast<int>((count + spellsPerPage - 1) / spellsPerPage);
	}
	return battleSpellsOnly ? sitesPerTabBattle[selectedTab] : sitesPerTabAdv[selectedTab];
}

CSpellWindow::SpellArea::SpellArea(Rect pos, CSpellWindow * owner)
{
	this->pos = pos;
	this->owner = owner;
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);
	if(owner->favorites)
		addUsedEvents(MOVE);

	schoolLevel = -1;
	mySpell = nullptr;

	OBJECT_CONSTRUCTION;

	name = std::make_shared<CLabel>(39, 70, FONT_TINY, ETextAlignment::CENTER);
	level = std::make_shared<CLabel>(39, 82, FONT_TINY, ETextAlignment::CENTER);
	cost = std::make_shared<CLabel>(39, 94, FONT_TINY, ETextAlignment::CENTER);

	for(auto l : {name, level, cost})
		l->setAutoRedraw(false);
}

CSpellWindow::SpellArea::~SpellArea() = default;

std::string CSpellWindow::SpellArea::learnedSpellRequirement() const
{
	if(owner->onSpellSelect || !mySpell || !owner->isSpellBlockedByWisdom(mySpell))
		return {};

	MetaString message = MetaString::createFromTextID("vcmi.spellBook.wisdomRequired");
	message.replaceNumber(mySpell->getLevel());
	message.replaceNumber(owner->myHero->maxCastableSpellLevel());
	return message.toString(&GAME->translator());
}

void CSpellWindow::SpellArea::clickPressed(const Point & cursorPosition)
{
	if(mySpell)
	{
		ENGINE->input().hapticFeedback();
		if(owner->favorites)
		{
			if(owner->favorites->selectCandidate(*mySpell))
			{
				ENGINE->sound().playSound(soundBase::button);
				return;
			}
			if(favoriteArea().isInside(cursorPosition))
			{
				ENGINE->sound().playSound(soundBase::button);
				owner->favorites->toggle(*mySpell);
				return;
			}
		}
		if(owner->archiveOwner)
		{
			GAME->interface()->showInfoDialog(owner->archiveDescription(mySpell), std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
			return;
		}

		if(owner->onSpellSelect)
		{
			owner->onSpellSelect(mySpell->id);
			owner->close();
			return;
		}

		const auto requirement = learnedSpellRequirement();
		if(!requirement.empty())
		{
			GAME->interface()->showInfoDialog(requirement);
			return;
		}

		auto spellCost = owner->myInt->cb->getSpellCost(mySpell, owner->myHero);
		if(spellCost > owner->myHero->mana) //insufficient mana
		{
			MetaString message = MetaString::createFromTextID("core.genrltxt.206"); // That spell costs %d spell points. Your hero only has %d spell points...
			message.replaceNumber(spellCost);
			message.replaceNumber(owner->myHero->mana);
			GAME->interface()->showInfoDialog(message.toString(&GAME->translator()));
			return;
		}

		//anything that is not combat spell is adventure spell
		//this not an error in general to cast even creature ability with hero
		const bool combatSpell = mySpell->isCombat();
		if(combatSpell == mySpell->isAdventure())
		{
			logGlobal->error("Spell have invalid flags");
			return;
		}

		const bool inCombat = owner->myInt->battleInt != nullptr;
		const bool inCastle = owner->myInt->castleInt != nullptr;

		//battle spell on adv map or adventure map spell during combat => display infowindow, not cast
		if((combatSpell != inCombat) || inCastle || (!combatSpell && !GAME->interface()->makingTurn))
		{
			std::vector<std::shared_ptr<CComponent>> hlp(1, std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
			GAME->interface()->showInfoDialog(mySpell->getDescriptionTranslated(schoolLevel), hlp);
		}
		else if(combatSpell)
		{
			spells::detail::ProblemImpl problem;
			if(mySpell->canBeCast(problem, owner->myInt->battleInt->getBattle().get(), spells::Mode::HERO, owner->myHero))
			{
				owner->myInt->battleInt->castThisSpell(mySpell->id);
				owner->fexitb();
			}
			else
			{
				std::vector<std::string> texts;
				problem.getAll(texts);
				if(!texts.empty())
					GAME->interface()->showInfoDialog(texts.front());
				else
					GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.spellUnknownProblem"));
			}
		}
		else //adventure spell
		{
			const CGHeroInstance * h = owner->myHero;
			if(owner->favorites)
				owner->favorites->saveView();
			ENGINE->windows().popWindows(1);

			auto guard = vstd::makeScopeGuard([this]()
			{
				auto spellBookState = owner->myInt->localState->getSpellbookSettings();
				spellBookState.spellbookLastTabAdvmap = owner->selectedTab;
				spellBookState.spellbookLastPageAdvmap = owner->currentPage;
				owner->myInt->localState->setSpellbookSettings(spellBookState);
			});

			spells::detail::ProblemImpl problem;
			if (mySpell->getAdventureMechanics().canBeCast(problem, GAME->interface()->cb.get(), owner->myHero))
			{
				const auto * rangeEffect = mySpell->getAdventureMechanics().getEffectAs<AdventureSpellRangedEffect>(owner->myHero);

				if(rangeEffect != nullptr)
					adventureInt->enterCastingMode(mySpell);
				else
					owner->myInt->cb->castSpell(h, mySpell->id);
			}
			else
			{
				std::vector<std::string> texts;
				problem.getAll(texts);
				if(!texts.empty())
					GAME->interface()->showInfoDialog(texts.front());
				else
					GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.spellUnknownProblem"));
			}
		}
	}
}

void CSpellWindow::SpellArea::showPopupWindow(const Point & cursorPosition)
{
	if(mySpell)
	{
		if(owner->archiveOwner)
		{
			CRClickPopup::createAndPush(owner->archiveDescription(mySpell), std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
			return;
		}
		std::string dmgInfo;
		auto causedDmg = owner->myInt->cb->estimateSpellDamage(mySpell, owner->myHero);
		if(causedDmg == 0 || mySpell->id == SpellID::TITANS_LIGHTNING_BOLT) //Titan's Lightning Bolt already has damage info included
			dmgInfo.clear();
		else
		{
			MetaString dmgText = MetaString::createFromTextID("core.genrltxt.343");
			dmgText.replaceNumber(causedDmg);
			dmgInfo = dmgText.toString(&GAME->translator());
		}

		const auto requirement = learnedSpellRequirement();
		const auto description = mySpell->getDescriptionTranslated(schoolLevel) + dmgInfo
			+ (requirement.empty() ? std::string() : "\n\n" + requirement);
		CRClickPopup::createAndPush(description, std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
	}
}

void CSpellWindow::SpellArea::hover(bool on)
{
	favoriteHovered = on && mySpell && owner->favorites && owner->favorites->markersEnabled()
		&& favoriteArea().isInside(ENGINE->getCursorPosition());
	if(owner->favorites)
		owner->redraw();
	if(mySpell)
	{
		if(on)
		{
			if(favoriteHovered)
			{
				owner->statusBar->write(owner->favorites->markerHelp(*mySpell));
				return;
			}
			MetaString message = MetaString::createFromRawString("%s (%s)");
			message.replaceTextID(mySpell->getNameTextID());
			message.replaceTextID("core.genrltxt", 171 + mySpell->getLevel());
			owner->statusBar->write(message.toString(&GAME->translator()));
		}
		else
			owner->statusBar->clear();
	}
}

Rect CSpellWindow::SpellArea::favoriteArea() const
{
	return Rect(pos.topLeft() + Point(65, 50), Point(15, 15));
}

bool CSpellWindow::SpellArea::receiveEvent(const Point & position, int eventType) const
{
	if(owner->favorites && mySpell)
	{
		if(owner->favorites->ribbonContains(position))
			return false;
		if(!owner->favorites->markersEnabled() && eventType == LCLICK)
			return Rect(pos.topLeft(), Point(80, 97)).isInside(position);
		if(owner->favorites->markersEnabled() && favoriteArea().isInside(position))
			return true;
	}
	return CIntObject::receiveEvent(position, eventType);
}

void CSpellWindow::SpellArea::mouseMoved(const Point & position, const Point &)
{
	if(mySpell && owner->favorites->markersEnabled() && favoriteHovered != favoriteArea().isInside(position))
		hover(true);
}

void CSpellWindow::SpellArea::showAll(Canvas & to)
{
	CIntObject::showAll(to);
	if(!mySpell || !owner->favorites || !owner->favorites->markersEnabled())
		return;

	const bool marked = owner->favorites->isMarked(*mySpell);
	if(marked || isHovered())
		to.draw(owner->favorites->star(marked, favoriteHovered), favoriteArea().topLeft());
}

void CSpellWindow::SpellArea::setSpell(const CSpell * spell)
{
	schoolBorder.reset();
	image.reset();
	name->setText("");
	level->setText("");
	cost->setText("");
	mySpell = spell;
	favoriteHovered = mySpell && owner->favorites && isHovered() && owner->favorites->markersEnabled()
		&& favoriteArea().isInside(ENGINE->getCursorPosition());
	if(mySpell)
	{
		SpellSchool whichSchool = mySpell->schools.empty() ? SpellSchool::ANY : *mySpell->schools.begin();
		const bool personal = owner->myHero && (!owner->archiveOwner || owner->personalArchive);
		schoolLevel = personal ? owner->myHero->getSpellSchoolLevel(mySpell, &whichSchool) : 0;
		auto spellCost = personal ? owner->myInt->cb->getSpellCost(mySpell, owner->myHero) : mySpell->getCost(0);

		{
			OBJECT_CONSTRUCTION;
			const uint8_t brightness = !owner->onSpellSelect && owner->isSpellBlockedByWisdom(mySpell) ? 120 : 255;
			// A private image reference keeps brightness changes local to this spell slot.
			auto sprite = ENGINE->renderHandler().loadImage(AnimationPath::builtin("Spells"), mySpell->id.getNum(), 0, EImageBlitMode::COLORKEY);
			sprite->setBrightness(brightness);
			image = std::make_shared<CPicture>(sprite, Point(0, 0));
			image->removeUsedEvents(LCLICK | SHOW_POPUP);

			const auto borderSchool = owner->selectedTab == SpellSchool::ANY || !owner->additionalSchools.empty() ? whichSchool : owner->selectedTab;
			if(borderSchool.hasValue())
			{
				auto borderSprite = ENGINE->renderHandler().loadImage(LIBRARY->spellSchoolHandler->getById(borderSchool)->getSpellBordersPath(), schoolLevel, 0, EImageBlitMode::COLORKEY);
				borderSprite->setBrightness(brightness);
				schoolBorder = std::make_shared<CPicture>(borderSprite, Point(0, 0));
				schoolBorder->removeUsedEvents(LCLICK | SHOW_POPUP);
			}
		}

		ColorRGBA firstLineColor, secondLineColor;
		if(owner->enhancedLayout)
		{
			firstLineColor = ColorRGBA(40, 25, 15);
			secondLineColor = !owner->onSpellSelect && owner->isSpellUnavailable(mySpell) ? ColorRGBA(100, 36, 19) : ColorRGBA(66, 44, 25);
		}
		else if(!owner->onSpellSelect && owner->isSpellUnavailable(mySpell))
		{
			firstLineColor = Colors::WHITE;
			secondLineColor = Colors::ORANGE;
		}
		else
		{
			firstLineColor = Colors::YELLOW;
			secondLineColor = Colors::WHITE;
		}

		name->color = firstLineColor;
		name->setText(mySpell->getNameTranslated());

		level->color = secondLineColor;
		std::string levelTextID = mySpell->getLevel() > 0 ? TextIdentifier("core.genrltxt", 171 + mySpell->getLevel()).get()
														  : "vcmi.spellBook.zero_level.hint";

		if(schoolLevel > 0)
		{
			MetaString levelText = MetaString::createFromRawString("%s/%s");
			levelText.replaceTextID(levelTextID);
			levelText.replaceTextID("core.skilllev", 3 + (schoolLevel - 1)); //lines 4-6
			level->setText(levelText.toString(&GAME->translator()));
		}
		else
			level->setText(GAME->translator().translate(levelTextID));

		cost->color = secondLineColor;
		MetaString costText = MetaString::createFromRawString("%s: %d");
		costText.replaceTextID("core.genrltxt.387"); // Spell Points
		costText.replaceNumber(spellCost);
		cost->setText(costText.toString(&GAME->translator()));
	}
}
