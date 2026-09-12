/**
 * @file test_diplomacy_screen.cpp
 * @brief The Diplomacy screen offers the 2.10a actions and a casus belli picker.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/ui/Widget.hpp"

#include <string>
#include <variant>

using aoc::PlayerId;
using aoc::ui::ButtonData;
using aoc::ui::DiplomacyScreen;
using aoc::ui::LabelData;
using aoc::ui::UIManager;
using aoc::ui::Widget;

namespace {

struct Fixture {
    aoc::test::World world = aoc::test::makeWorld(2);
    aoc::sim::DiplomacyManager d;
    aoc::sim::GlobalDealTracker tracker;
    UIManager ui;
    DiplomacyScreen screen;

    Fixture() {
        this->d.initialize(2);
        this->d.meetPlayers(PlayerId{0}, PlayerId{1}, 3);
        aoc::test::addCityAt(this->world, PlayerId{0}, 4, 4, "Alpha");
        aoc::test::addCityAt(this->world, PlayerId{1}, 14, 8, "Beta");
        this->world.gameState.players()[1]->setCivId(static_cast<aoc::sim::CivId>(1));
        this->ui.setScreenSize(1920.0f, 1200.0f);
        this->screen.setScreenSize(1920.0f, 1200.0f);
        this->screen.setContext(&this->world.gameState, PlayerId{0}, &this->d, &this->world.grid,
                                &this->tracker, nullptr);
    }

    [[nodiscard]] int32_t labelsContaining(const std::string& needle) const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const LabelData* label = std::get_if<LabelData>(&w.data);
            if (label != nullptr && label->text.find(needle) != std::string::npos) { ++n; }
        }
        return n;
    }

    [[nodiscard]] int32_t buttonsLabelled(const std::string& text) const {
        int32_t n = 0;
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label == text) { ++n; }
        }
        return n;
    }

    bool clickButton(const std::string& text) {
        for (const Widget& w : this->ui.widgets()) {
            if (w.id == aoc::ui::INVALID_WIDGET) { continue; }
            const ButtonData* btn = std::get_if<ButtonData>(&w.data);
            if (btn != nullptr && btn->label == text) {
                const aoc::ui::WidgetId id = w.id;
                return this->ui.clickWidget(id);
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("at peace the card offers war, borders, denounce, friendship, delegation and embassy") {
    Fixture f;
    f.screen.open(f.ui);
    CHECK(f.buttonsLabelled("Declare War") == 1);
    CHECK(f.buttonsLabelled("Open Borders") == 1);
    CHECK(f.buttonsLabelled("Denounce") == 1);
    CHECK(f.buttonsLabelled("Friendship") == 1);
    CHECK(f.buttonsLabelled("Delegation (25 gold)") == 1);
    CHECK(f.buttonsLabelled("Embassy (50 gold)") == 1);
    CHECK(f.buttonsLabelled("Propose Peace") == 0);
}

TEST_CASE("Declare War opens a casus belli picker; the chosen casus belli starts the war") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Declare War"));
    CHECK(f.buttonsLabelled("Declare War") == 0);
    CHECK(f.buttonsLabelled("Surprise War") == 1);
    CHECK(f.buttonsLabelled("Formal War") == 0); // not denounced
    CHECK(f.buttonsLabelled("Cancel") == 1);
    REQUIRE(f.clickButton("Cancel"));
    CHECK(f.buttonsLabelled("Declare War") == 1);

    REQUIRE(f.clickButton("Declare War"));
    REQUIRE(f.clickButton("Surprise War"));
    CHECK(f.d.isAtWar(PlayerId{0}, PlayerId{1}));
    CHECK_FALSE(f.screen.isOpen());
    f.screen.open(f.ui);
    CHECK(f.buttonsLabelled("Propose Peace") == 1);
    CHECK(f.buttonsLabelled("Declare War") == 0);
    CHECK(f.buttonsLabelled("Denounce") == 0);
    CHECK(f.labelsContaining("[AT WAR]") == 1);
}

TEST_CASE("Denounce marks the relation and unlocks Formal War in the picker") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Denounce"));
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).denouncedOnTurn == f.world.gameState.currentTurn());
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("[Denounced]") == 1);
    CHECK(f.buttonsLabelled("Denounce") == 0);
    REQUIRE(f.clickButton("Declare War"));
    CHECK(f.buttonsLabelled("Formal War") == 1);
}

TEST_CASE("a rejected action leaves the state alone; an accepted one shows its tag") {
    Fixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Delegation (25 gold)")); // 0 gold
    CHECK_FALSE(f.d.relation(PlayerId{0}, PlayerId{1}).hasDelegation);
    f.world.gameState.player(PlayerId{0})->addGold(30, aoc::sim::MoneyFlow::external());
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Delegation (25 gold)"));
    CHECK(f.d.relation(PlayerId{0}, PlayerId{1}).hasDelegation);
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("[Delegation]") == 1);
    CHECK(f.buttonsLabelled("Delegation (25 gold)") == 0);
    CHECK(f.buttonsLabelled("Embassy (50 gold)") == 1);
}

TEST_CASE("the deal composer toggles preset terms and sends them; an AI takes a gift at once") {
    Fixture f;
    f.world.gameState.player(PlayerId{0})->addGold(150, aoc::sim::MoneyFlow::external());
    f.screen.open(f.ui);
    CHECK(f.buttonsLabelled("Propose Deal") == 1);
    REQUIRE(f.clickButton("Propose Deal"));
    CHECK(f.labelsContaining("Proposal: (nothing yet)") == 1);
    CHECK(f.buttonsLabelled("Give 100 gold") == 1);
    REQUIRE(f.clickButton("Give 100 gold"));
    CHECK(f.labelsContaining("Proposal: 100 gold (") == 1);
    REQUIRE(f.clickButton("Give 500 gold")); // replaces the 100
    CHECK(f.labelsContaining("Proposal: 500 gold (") == 1);
    REQUIRE(f.clickButton("Give 500 gold")); // same amount again removes it
    CHECK(f.labelsContaining("Proposal: (nothing yet)") == 1);
    REQUIRE(f.clickButton("Give 100 gold"));
    REQUIRE(f.clickButton("Non-Aggression"));
    CHECK(f.labelsContaining("Non-Aggression Pact (30 turns)") == 1);
    REQUIRE(f.clickButton("Send Proposal"));
    CHECK_FALSE(f.screen.isOpen());
    CHECK(f.world.gameState.player(PlayerId{1})->treasury() == 100);
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() == 50);
    REQUIRE(f.tracker.activeDeals.size() == 1);
    CHECK(f.tracker.activeDeals.front().hasTerm(aoc::sim::DealTermType::NonAggression));
    CHECK(f.tracker.activeDeals.front().turnsRemaining == 30); // the pact lives its term
    CHECK(f.tracker.hasNonAggressionPact(PlayerId{0}, PlayerId{1}));

    f.screen.open(f.ui);
    CHECK(f.buttonsLabelled("Propose Deal") == 1); // composer closed after sending
    REQUIRE(f.clickButton("Propose Deal"));
    REQUIRE(f.clickButton("Cancel"));
    CHECK(f.buttonsLabelled("Propose Deal") == 1);
}

namespace {

/// The human holds twelve silk the neighbour needs; the neighbour holds ten
/// iron the human needs. Both are cashed up.
struct GoodsFixture : Fixture {
    aoc::game::City& alpha;
    aoc::game::City& beta;

    GoodsFixture()
        : alpha(*this->world.gameState.player(PlayerId{0})->cities().front()),
          beta(*this->world.gameState.player(PlayerId{1})->cities().front()) {
        this->alpha.stockpile().addGoods(aoc::sim::goods::SILK, 12);
        this->beta.stockpile().addGoods(aoc::sim::goods::IRON_ORE, 10);
        this->world.gameState.player(PlayerId{1})->economy().totalNeeds[aoc::sim::goods::SILK] = 1;
        this->world.gameState.player(PlayerId{0})->economy().totalNeeds[aoc::sim::goods::IRON_ORE] = 4;
        this->world.gameState.player(PlayerId{0})->setTreasury(1000, aoc::sim::MoneyFlow::external());
        this->world.gameState.player(PlayerId{1})->setTreasury(1000, aoc::sim::MoneyFlow::external());
    }

    /// What the neighbour makes of a one-term deal from the human.
    [[nodiscard]] int32_t theirValue(const aoc::sim::DealTerm& term) const {
        aoc::sim::DiplomaticDeal deal;
        deal.playerA = PlayerId{0};
        deal.playerB = PlayerId{1};
        deal.terms.push_back(term);
        return aoc::sim::dealValueFor(this->world.gameState, this->d, PlayerId{1}, deal);
    }

    [[nodiscard]] static std::string view(int32_t value) {
        return "Their view: " + std::string(value >= 0 ? "+" : "") + std::to_string(value) + " gold" +
               (value >= 0 ? " (would accept)" : " (would refuse)");
    }

    [[nodiscard]] static aoc::sim::DealTerm goods(aoc::sim::DealTermType type, PlayerId from, PlayerId to,
                                                  uint16_t good, int32_t amount) {
        aoc::sim::DealTerm term{};
        term.type       = type;
        term.fromPlayer = from;
        term.toPlayer   = to;
        term.goodId     = good;
        term.goodAmount = amount;
        term.duration   = 30;
        return term;
    }
};

} // namespace

TEST_CASE("the composer offers goods rows from stock and need, shows the counterpart's view, and balances with gold") {
    GoodsFixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Propose Deal"));
    CHECK(f.labelsContaining("Silk x12 *") == 1);     // starred: they need it
    CHECK(f.labelsContaining("Iron Ore x10 *") == 1); // starred: I need it
    CHECK(f.buttonsLabelled("Sell 5 Silk") == 1);
    CHECK(f.buttonsLabelled("Supply 1 Silk/turn") == 1);
    CHECK(f.buttonsLabelled("Exclusive Silk") == 1);
    CHECK(f.buttonsLabelled("Ask 5 Iron Ore") == 1);
    CHECK(f.buttonsLabelled("Buy 1 Iron Ore/turn") == 1);
    CHECK(f.buttonsLabelled("Ask exclusive Iron Ore") == 1);

    const int32_t worth = f.theirValue(GoodsFixture::goods(aoc::sim::DealTermType::GoodsExchange, PlayerId{0},
                                                           PlayerId{1}, aoc::sim::goods::SILK, 5));
    REQUIRE(worth > 0);
    REQUIRE(f.clickButton("Sell 5 Silk"));
    CHECK(f.labelsContaining("Proposal: 5 Silk (") == 1);
    CHECK(f.labelsContaining(GoodsFixture::view(worth)) == 1);
    REQUIRE(f.clickButton("Sell 5 Silk")); // the same term again removes it
    CHECK(f.labelsContaining("Proposal: (nothing yet)") == 1);
    REQUIRE(f.clickButton("Sell 5 Silk"));

    REQUIRE(f.clickButton("Balance with gold")); // they pay what the silk is worth to them
    CHECK(f.labelsContaining(std::to_string(worth) + " gold (") == 1);
    CHECK(f.labelsContaining("(would accept)") == 1);
    REQUIRE(f.clickButton("Send Proposal"));
    CHECK(f.world.gameState.player(PlayerId{1})->treasury() == 1000 - worth);
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() == 1000 + worth);
    CHECK(f.alpha.stockpile().getAmount(aoc::sim::goods::SILK) == 7);
    CHECK(f.beta.stockpile().getAmount(aoc::sim::goods::SILK) == 5);
}

TEST_CASE("asking for their goods is refused until gold balances it, then it goes through") {
    GoodsFixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Propose Deal"));
    const int32_t worth = f.theirValue(GoodsFixture::goods(aoc::sim::DealTermType::GoodsExchange, PlayerId{1},
                                                           PlayerId{0}, aoc::sim::goods::IRON_ORE, 5));
    REQUIRE(worth < 0);
    REQUIRE(f.clickButton("Ask 5 Iron Ore"));
    CHECK(f.labelsContaining(GoodsFixture::view(worth)) == 1);
    REQUIRE(f.clickButton("Balance with gold")); // I pay what the iron is worth to them
    CHECK(f.labelsContaining("(would accept)") == 1);
    REQUIRE(f.clickButton("Send Proposal"));
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() == 1000 + worth);
    CHECK(f.world.gameState.player(PlayerId{1})->treasury() == 1000 - worth);
    CHECK(f.alpha.stockpile().getAmount(aoc::sim::goods::IRON_ORE) == 5);
    CHECK(f.beta.stockpile().getAmount(aoc::sim::goods::IRON_ORE) == 5);
}

TEST_CASE("a supply contract row is priced per turn at the counterpart's break-even") {
    GoodsFixture f;
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Propose Deal"));
    const int32_t stream = f.theirValue(GoodsFixture::goods(aoc::sim::DealTermType::SupplyContract, PlayerId{0},
                                                            PlayerId{1}, aoc::sim::goods::SILK, 1));
    REQUIRE(stream > 0);
    const int32_t perTurn = stream / 30;
    REQUIRE(f.clickButton("Supply 1 Silk/turn"));
    CHECK(f.labelsContaining("1 Silk per turn for 30 turns at " + std::to_string(perTurn) +
                             " gold per turn (") == 1);
    CHECK(f.labelsContaining("(would accept)") == 1);
    REQUIRE(f.clickButton("Send Proposal"));
    REQUIRE(f.tracker.activeDeals.size() == 1);
    const aoc::sim::DiplomaticDeal& deal = f.tracker.activeDeals.front();
    REQUIRE(deal.terms.size() == 1);
    CHECK(deal.terms[0].type == aoc::sim::DealTermType::SupplyContract);
    CHECK(deal.terms[0].goldPerTurn == perTurn);
    CHECK(deal.terms[0].duration == 30);
}

TEST_CASE("the inbox lists proposals to the human with Accept and Reject") {
    Fixture f;
    f.world.gameState.player(PlayerId{1})->addGold(200, aoc::sim::MoneyFlow::external());
    aoc::sim::PendingProposal offer;
    offer.from = PlayerId{1};
    offer.to   = PlayerId{0};
    offer.deal.playerA = PlayerId{1};
    offer.deal.playerB = PlayerId{0};
    aoc::sim::DealTerm gold{};
    gold.type       = aoc::sim::DealTermType::GoldLump;
    gold.fromPlayer = PlayerId{1};
    gold.toPlayer   = PlayerId{0};
    gold.goldLump   = 75;
    offer.deal.terms.push_back(gold);
    offer.expiresTurn = 5;
    f.world.gameState.pendingProposals().push_back(offer);

    f.screen.open(f.ui);
    CHECK(f.labelsContaining("INBOX") == 1);
    CHECK(f.labelsContaining("From Egypt: 75 gold (Egypt -> Rome)  (expires turn 5)") == 1);
    const std::string quote(aoc::sim::getLeaderDialogue(static_cast<aoc::sim::CivId>(1),
                                                        aoc::sim::DialogueContext::ProposeTrade));
    CHECK(f.labelsContaining(quote) == 1);
    CHECK(f.buttonsLabelled("Accept") == 1);
    CHECK(f.buttonsLabelled("Reject") == 1);
    REQUIRE(f.clickButton("Accept"));
    CHECK(f.world.gameState.pendingProposals().empty());
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() == 75);
    CHECK(f.world.gameState.player(PlayerId{1})->treasury() == 125);

    f.world.gameState.pendingProposals().push_back(offer);
    f.screen.open(f.ui);
    REQUIRE(f.clickButton("Reject"));
    CHECK(f.world.gameState.pendingProposals().empty());
    CHECK(f.world.gameState.player(PlayerId{0})->treasury() == 75);
    f.screen.open(f.ui);
    CHECK(f.labelsContaining("INBOX") == 0);
}
