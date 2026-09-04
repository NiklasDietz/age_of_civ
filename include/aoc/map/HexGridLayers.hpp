#pragma once

/**
 * @file HexGridLayers.hpp
 * @brief HexGrid::visitLayers(): every container layer of the grid, by name.
 *
 * One list for both the const and the mutable visitor, so a serializer that
 * walks it writes and reads the same layers. `scripts/check_grid_layers.py`
 * (ctest `test_grid_layer_list`) fails when HexGrid gains a container member
 * that is not listed here: add the new layer to this list and to nothing else.
 */

#include "aoc/map/HexGrid.hpp"

namespace aoc::map {

template <class Self, class Visitor>
void HexGrid::visitLayersImpl(Self& self, Visitor&& visitor) {
    visitor("terrain", self.m_terrain);
    visitor("feature", self.m_feature);
    visitor("elevation", self.m_elevation);
    visitor("riverEdges", self.m_riverEdges);
    visitor("resource", self.m_resource);
    visitor("reserves", self.m_reserves);
    visitor("prospectCooldown", self.m_prospectCooldown);
    visitor("owner", self.m_owner);
    visitor("improvement", self.m_improvement);
    visitor("road", self.m_road);
    visitor("tileInfra", self.m_tileInfra);
    visitor("plateId", self.m_plateId);
    visitor("boundaryTypeTile", self.m_boundaryTypeTile);
    visitor("hotspots", self.m_hotspots);
    visitor("plateMotion", self.m_plateMotion);
    visitor("plateCenter", self.m_plateCenter);
    visitor("plateLandFrac", self.m_plateLandFrac);
    visitor("plateCrustAge", self.m_plateCrustAge);
    visitor("plateMergesAbsorbed", self.m_plateMergesAbsorbed);
    visitor("plateIsPolar", self.m_plateIsPolar);
    visitor("sphereFieldElevationSnapshot", self.m_sphereFieldElevationSnapshot);
    visitor("rowLatitudeDeg", self.m_rowLatitudeDeg);
    visitor("crustAgeTile", self.m_crustAgeTile);
    visitor("magneticPolarity", self.m_magneticPolarity);
    visitor("sedimentDepth", self.m_sedimentDepth);
    visitor("rockType", self.m_rockType);
    visitor("marginType", self.m_marginType);
    visitor("soilFertility", self.m_soilFertility);
    visitor("volcanism", self.m_volcanism);
    visitor("seismicHazard", self.m_seismicHazard);
    visitor("permafrost", self.m_permafrost);
    visitor("lakeFlag", self.m_lakeFlag);
    visitor("upwelling", self.m_upwelling);
    visitor("isolatedRealm", self.m_isolatedRealm);
    visitor("landBridge", self.m_landBridge);
    visitor("refugium", self.m_refugium);
    visitor("climateHazard", self.m_climateHazard);
    visitor("glacialFeature", self.m_glacialFeature);
    visitor("oceanZone", self.m_oceanZone);
    visitor("cloudCover", self.m_cloudCover);
    visitor("flowDir", self.m_flowDir);
    visitor("naturalHazard", self.m_naturalHazard);
    visitor("biomeSubtype", self.m_biomeSubtype);
    visitor("marineDepth", self.m_marineDepth);
    visitor("wildlife", self.m_wildlife);
    visitor("disease", self.m_disease);
    visitor("windEnergy", self.m_windEnergy);
    visitor("solarEnergy", self.m_solarEnergy);
    visitor("hydroEnergy", self.m_hydroEnergy);
    visitor("geothermalEnergy", self.m_geothermalEnergy);
    visitor("tidalEnergy", self.m_tidalEnergy);
    visitor("waveEnergy", self.m_waveEnergy);
    visitor("atmosphericExtras", self.m_atmosphericExtras);
    visitor("hydroExtras", self.m_hydroExtras);
    visitor("eventMarker", self.m_eventMarker);
    visitor("mountainPass", self.m_mountainPass);
    visitor("defensibility", self.m_defensibility);
    visitor("domesticable", self.m_domesticable);
    visitor("tradeRoutePotential", self.m_tradeRoutePotential);
    visitor("habitability", self.m_habitability);
    visitor("wetlandSubtype", self.m_wetlandSubtype);
    visitor("reefTier", self.m_reefTier);
    visitor("warDamage", self.m_warDamage);
    visitor("anthropogenic", self.m_anthropogenic);
    visitor("settlementRuin", self.m_settlementRuin);
    visitor("activeTradeRoute", self.m_activeTradeRoute);
    visitor("koppen", self.m_koppen);
    visitor("mountainStructure", self.m_mountainStructure);
    visitor("oreGrade", self.m_oreGrade);
    visitor("strait", self.m_strait);
    visitor("harborScore", self.m_harborScore);
    visitor("channelPattern", self.m_channelPattern);
    visitor("vegetationDensity", self.m_vegetationDensity);
    visitor("coastalFeature", self.m_coastalFeature);
    visitor("submarineVent", self.m_submarineVent);
    visitor("volcanicProfile", self.m_volcanicProfile);
    visitor("karstSubtype", self.m_karstSubtype);
    visitor("desertSubtype", self.m_desertSubtype);
    visitor("massWasting", self.m_massWasting);
    visitor("namedWind", self.m_namedWind);
    visitor("forestAgeClass", self.m_forestAgeClass);
    visitor("soilMoistureRegime", self.m_soilMoistureRegime);
    visitor("lithology", self.m_lithology);
    visitor("soilOrder", self.m_soilOrder);
    visitor("crustalThickness", self.m_crustalThickness);
    visitor("geothermalGradient", self.m_geothermalGradient);
    visitor("albedo", self.m_albedo);
    visitor("vegetationType", self.m_vegetationType);
    visitor("atmosphericRiver", self.m_atmosphericRiver);
    visitor("cycloneBasin", self.m_cycloneBasin);
    visitor("seaSurfaceTemp", self.m_seaSurfaceTemp);
    visitor("iceShelfZone", self.m_iceShelfZone);
    visitor("bedrockLithology", self.m_bedrockLithology);
    visitor("permafrostDepth", self.m_permafrostDepth);
    visitor("cliffCoast", self.m_cliffCoast);
    visitor("coastalLandform", self.m_coastalLandform);
    visitor("riverRegime", self.m_riverRegime);
    visitor("aridLandform", self.m_aridLandform);
    visitor("transformFaultType", self.m_transformFaultType);
    visitor("lakeEffectSnow", self.m_lakeEffectSnow);
    visitor("drumlinDirection", self.m_drumlinDirection);
    visitor("sutureReactivated", self.m_sutureReactivated);
    visitor("solarInsolation", self.m_solarInsolation);
    visitor("topographicAspect", self.m_topographicAspect);
    visitor("slopeAngle", self.m_slopeAngle);
    visitor("ecotone", self.m_ecotone);
    visitor("pelagicProductivity", self.m_pelagicProductivity);
    visitor("shelfSedimentThickness", self.m_shelfSedimentThickness);
    visitor("glacialRebound", self.m_glacialRebound);
    visitor("sedimentTransportDir", self.m_sedimentTransportDir);
    visitor("coastalChange", self.m_coastalChange);
    visitor("streamOrder", self.m_streamOrder);
    visitor("navigable", self.m_navigable);
    visitor("damSite", self.m_damSite);
    visitor("riparian", self.m_riparian);
    visitor("aquiferRecharge", self.m_aquiferRecharge);
    visitor("cropSuitability", self.m_cropSuitability);
    visitor("pastureScore", self.m_pastureScore);
    visitor("forestryYield", self.m_forestryYield);
    visitor("foldAxis", self.m_foldAxis);
    visitor("metamorphicFacies", self.m_metamorphicFacies);
    visitor("plateStress", self.m_plateStress);
    visitor("cycloneIntensity", self.m_cycloneIntensity);
    visitor("droughtSeverity", self.m_droughtSeverity);
    visitor("stormWaveHeight", self.m_stormWaveHeight);
    visitor("snowLine", self.m_snowLine);
    visitor("habitatFragmentation", self.m_habitatFragmentation);
    visitor("endemismIndex", self.m_endemismIndex);
    visitor("speciesRichness", self.m_speciesRichness);
    visitor("netPrimaryProductivity", self.m_netPrimaryProductivity);
    visitor("growingSeasonDays", self.m_growingSeasonDays);
    visitor("frostDays", self.m_frostDays);
    visitor("carryingCapacity", self.m_carryingCapacity);
    visitor("soilClayPct", self.m_soilClayPct);
    visitor("soilSiltPct", self.m_soilSiltPct);
    visitor("soilSandPct", self.m_soilSandPct);
    visitor("seasonalTempRange", self.m_seasonalTempRange);
    visitor("diurnalTempRange", self.m_diurnalTempRange);
    visitor("uvIndex", self.m_uvIndex);
    visitor("coralBleachRisk", self.m_coralBleachRisk);
    visitor("magneticAnomaly", self.m_magneticAnomaly);
    visitor("heatFlow", self.m_heatFlow);
    visitor("volcanoReturnPeriod", self.m_volcanoReturnPeriod);
    visitor("tsunamiRunup", self.m_tsunamiRunup);
    visitor("topoPositionIndex", self.m_topoPositionIndex);
    visitor("topoWetnessIndex", self.m_topoWetnessIndex);
    visitor("roughness", self.m_roughness);
    visitor("curvature", self.m_curvature);
    visitor("riverDischarge", self.m_riverDischarge);
    visitor("drainageBasinArea", self.m_drainageBasinArea);
    visitor("watershedId", self.m_watershedId);
    visitor("livestockSuit", self.m_livestockSuit);
    visitor("faultTrace", self.m_faultTrace);
    visitor("reefTerrace", self.m_reefTerrace);
    visitor("mineSuitability", self.m_mineSuitability);
    visitor("coalSeamThickness", self.m_coalSeamThickness);
    visitor("soilPh", self.m_soilPh);
    visitor("iceCoverDuration", self.m_iceCoverDuration);
    visitor("hydropowerCapacity", self.m_hydropowerCapacity);
    visitor("petIndex", self.m_petIndex);
    visitor("aridityIndex", self.m_aridityIndex);
    visitor("erosionPotential", self.m_erosionPotential);
    visitor("carbonStock", self.m_carbonStock);
    visitor("wilderness", self.m_wilderness);
    visitor("floodFrequency", self.m_floodFrequency);
    visitor("canopyStratification", self.m_canopyStratification);
    visitor("riparianForest", self.m_riparianForest);
    visitor("magneticIntensity", self.m_magneticIntensity);
    visitor("groundwaterDepth", self.m_groundwaterDepth);
    visitor("greenhouseCrop", self.m_greenhouseCrop);
    visitor("naturalWonder", self.m_naturalWonder);
    visitor("chokepoint", self.m_chokepoint);
    visitor("falloutTurns", self.m_falloutTurns);
    visitor("preFalloutFeature", self.m_preFalloutFeature);
}

template <class Visitor>
void HexGrid::visitLayers(Visitor&& visitor) {
    HexGrid::visitLayersImpl(*this, visitor);
}

template <class Visitor>
void HexGrid::visitLayers(Visitor&& visitor) const {
    HexGrid::visitLayersImpl(*this, visitor);
}

} // namespace aoc::map
