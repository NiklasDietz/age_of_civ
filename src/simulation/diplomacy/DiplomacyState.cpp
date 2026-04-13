/**
 * @file DiplomacyState.cpp
 * @brief Diplomacy relation matrix management.
 */

#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cassert>

namespace aoc::sim {

void DiplomacyManager::initialize(uint8_t playerCount) {
    this->m_playerCount = playerCount;
    this->m_relations.resize(
        static_cast<std::size_t>(playerCount) * static_cast<std::size_t>(playerCount));
}

PairwiseRelation& DiplomacyManager::relation(PlayerId a, PlayerId b) {
    assert(a < this->m_playerCount && b < this->m_playerCount);
    return this->m_relations[static_cast<std::size_t>(a) * this->m_playerCount + b];
}

const PairwiseRelation& DiplomacyManager::relation(PlayerId a, PlayerId b) const {
    assert(a < this->m_playerCount && b < this->m_playerCount);
    return this->m_relations[static_cast<std::size_t>(a) * this->m_playerCount + b];
}

void DiplomacyManager::addModifier(PlayerId a, PlayerId b, RelationModifier modifier) {
    this->relation(a, b).modifiers.push_back(modifier);
    // Mirror: symmetric relation
    if (a != b) {
        this->relation(b, a).modifiers.push_back(std::move(modifier));
    }
}

void DiplomacyManager::declareWar(PlayerId aggressor, PlayerId target) {
    PairwiseRelation& relAB = this->relation(aggressor, target);
    PairwiseRelation& relBA = this->relation(target, aggressor);

    relAB.isAtWar = true;
    relBA.isAtWar = true;

    // War declaration adds a large negative modifier
    RelationModifier warMod{"Declared war", -50, 0};  // Permanent until peace
    relAB.modifiers.push_back(warMod);
    relBA.modifiers.push_back(warMod);

    // Break open borders and alliances
    relAB.hasOpenBorders = false;
    relBA.hasOpenBorders = false;
    relAB.hasDefensiveAlliance = false;
    relBA.hasDefensiveAlliance = false;
    relAB.hasMilitaryAlliance = false;
    relBA.hasMilitaryAlliance = false;
    relAB.hasResearchAgreement = false;
    relBA.hasResearchAgreement = false;
    relAB.hasEconomicAlliance = false;
    relBA.hasEconomicAlliance = false;

    LOG_INFO("Player %u declared war on Player %u",
             static_cast<unsigned>(aggressor), static_cast<unsigned>(target));
}

void DiplomacyManager::makePeace(PlayerId a, PlayerId b) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);

    relAB.isAtWar = false;
    relBA.isAtWar = false;
    relAB.turnsSincePeace = 0;
    relBA.turnsSincePeace = 0;

    // auto required: lambda type is unnameable
    auto removeWar = [](std::vector<RelationModifier>& mods) {
        mods.erase(
            std::remove_if(mods.begin(), mods.end(),
                [](const RelationModifier& m) { return m.reason == "Declared war"; }),
            mods.end());
    };
    removeWar(relAB.modifiers);
    removeWar(relBA.modifiers);

    // Post-war cooldown
    RelationModifier peaceMod{"Recent peace", -20, 30};
    relAB.modifiers.push_back(peaceMod);
    relBA.modifiers.push_back(peaceMod);

    LOG_INFO("Peace between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
}

void DiplomacyManager::grantOpenBorders(PlayerId a, PlayerId b) {
    this->relation(a, b).hasOpenBorders = true;
    this->relation(b, a).hasOpenBorders = true;
    this->addModifier(a, b, {"Open borders", 5, 0});
}

void DiplomacyManager::formDefensiveAlliance(PlayerId a, PlayerId b) {
    this->relation(a, b).hasDefensiveAlliance = true;
    this->relation(b, a).hasDefensiveAlliance = true;
    this->addModifier(a, b, {"Defensive alliance", 15, 0});
}

void DiplomacyManager::formMilitaryAlliance(PlayerId a, PlayerId b) {
    this->relation(a, b).hasMilitaryAlliance = true;
    this->relation(b, a).hasMilitaryAlliance = true;
    this->addModifier(a, b, {"Military alliance", 10, 0});
    LOG_INFO("Military alliance formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
}

void DiplomacyManager::formResearchAgreement(PlayerId a, PlayerId b) {
    this->relation(a, b).hasResearchAgreement = true;
    this->relation(b, a).hasResearchAgreement = true;
    this->addModifier(a, b, {"Research agreement", 5, 0});
    LOG_INFO("Research agreement formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
}

void DiplomacyManager::formEconomicAlliance(PlayerId a, PlayerId b) {
    this->relation(a, b).hasEconomicAlliance = true;
    this->relation(b, a).hasEconomicAlliance = true;
    this->addModifier(a, b, {"Economic alliance", 5, 0});
    LOG_INFO("Economic alliance formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
}

void DiplomacyManager::tickModifiers() {
    for (PairwiseRelation& rel : this->m_relations) {
        // Increment peace cooldown timer
        if (!rel.isAtWar && rel.turnsSincePeace < 1000) {
            ++rel.turnsSincePeace;
        }
        for (std::vector<RelationModifier>::iterator it = rel.modifiers.begin(); it != rel.modifiers.end(); ) {
            if (it->turnsRemaining > 0) {
                --it->turnsRemaining;
                if (it->turnsRemaining == 0) {
                    it = rel.modifiers.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }
}

void DiplomacyManager::setEmbargo(PlayerId a, PlayerId b, bool embargo) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);

    relAB.hasEmbargo = embargo;
    relBA.hasEmbargo = embargo;

    if (embargo) {
        RelationModifier embargoMod{"Trade Embargo", -15, 20};
        relAB.modifiers.push_back(embargoMod);
        relBA.modifiers.push_back(embargoMod);

        LOG_INFO("Trade embargo set between Player %u and Player %u",
                 static_cast<unsigned>(a), static_cast<unsigned>(b));
    } else {
        // Remove embargo modifiers
        // auto required: lambda type is unnameable
        auto removeEmbargo = [](std::vector<RelationModifier>& mods) {
            mods.erase(
                std::remove_if(mods.begin(), mods.end(),
                    [](const RelationModifier& m) { return m.reason == "Trade Embargo"; }),
                mods.end());
        };
        removeEmbargo(relAB.modifiers);
        removeEmbargo(relBA.modifiers);

        LOG_INFO("Trade embargo lifted between Player %u and Player %u",
                 static_cast<unsigned>(a), static_cast<unsigned>(b));
    }
}

bool DiplomacyManager::hasEmbargo(PlayerId a, PlayerId b) const {
    return this->relation(a, b).hasEmbargo;
}

bool DiplomacyManager::isAtWar(PlayerId a, PlayerId b) const {
    // Barbarians are always at war with everyone.
    if (a == BARBARIAN_PLAYER || b == BARBARIAN_PLAYER) {
        return true;
    }
    return this->relation(a, b).isAtWar;
}

} // namespace aoc::sim
