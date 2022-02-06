#include <float.h>
#include <math.h>
#include <vector>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <ctime>
#include "terranx_wtp.h"
#include "ai.h"
#include "wtp.h"
#include "game.h"
#include "ai.h"
#include "aiProduction.h"
#include "aiMoveFormer.h"
#include "aiHurry.h"
#include "aiProduction.h"

// global variables

int currentTurn = -1;
int aiFactionId = -1;

AIData data;

/*
Top level faction upkeep entry point.
This is called for AI enabled factions only.
*/
int aiFactionUpkeep(const int factionId)
{
	debug("aiFactionUpkeep - %s\n", MFactions[factionId].noun_faction);
	
	// set AI faction id for future reference
	
	aiFactionId = factionId;
	
	// recalculate global faction parameters on this turn if not yet done
	
	aiStrategy();
	
	// redirect to vanilla function for the rest of processing
	// that, in turn, is overriden by Thinker
	
	int returnValue = faction_upkeep(factionId);
	
//	// update base productions
//	
//	setProduction();
//	
	// consider hurrying production in all bases
	
	considerHurryingProduction(factionId);
	
	// return value
	
	return returnValue;
	
}

/*
AI strategy.
*/
void aiStrategy()
{
	debug("aiStrategy: aiFactionId=%d\n", aiFactionId);

	// clear lists
	
	data.clear();
	
	// populate shared strategy lists
	
	clock_t s = clock();
	analyzeGeography();
	clock_t e = clock();
    double elapsed = (double)(e - s) / (double)CLOCKS_PER_SEC;
    debug("(time) analyzeGeography: %f\n", elapsed);
	
	setSharedOceanRegions();
	populateGlobalVariables();
	evaluateBaseExposures();
	
	// design units
	
	designUnits();
	
	// bases native defense
	
	evaluateBaseNativeDefenseDemands();
	
	// bases defense
	
	evaluateDefenseDemand();
	
}

/*
Analyzes and sets geographical parameters.
*/
void analyzeGeography()
{
	debug("analyzeGeography - %s\n", MFactions[aiFactionId].noun_faction);
	
	// populate associations
	
	std::unordered_set<int> polarMapIndexes;
	
	std::unordered_map<int, int> *associations = &(data.geography.associations);
	associations->clear();
	
	for (int mapIndex = 0; mapIndex < *map_area_tiles; mapIndex++)
	{
		MAP *tile = getMapTile(mapIndex);
		int region = getExtendedRegion(tile);
		
		associations->insert({region, {region}});
		
		if (isPolarRegion(tile))
		{
			bool ocean = is_ocean(tile);
			
			polarMapIndexes.insert(mapIndex);
			
			for (MAP * adjacentTile : getAdjacentTiles(mapIndex, false))
			{
				bool adjacentOcean = is_ocean(adjacentTile);
				int adjacentRegion = getExtendedRegion(adjacentTile);
				
				// do not associate polar region with polar region
				
				if (isPolarRegion(adjacentTile))
					continue;
				
				if (adjacentOcean == ocean)
				{
					associations->at(region) = adjacentRegion;
					break;
				}
				
			}
			
		}
		
	}
	
	// populate connections
	
	std::unordered_map<int, std::unordered_set<int>> *connections = &(data.geography.connections);
	connections->clear();
	
	for (int mapIndex = 0; mapIndex < *map_area_tiles; mapIndex++)
	{
		MAP *tile = getMapTile(mapIndex);
		bool ocean = is_ocean(tile);
		int region = getExtendedRegion(tile);
		
		connections->insert({region, {}});
		
		for (MAP * adjacentTile : getAdjacentTiles(mapIndex, false))
		{
			bool adjacentOcean = is_ocean(adjacentTile);
			int adjacentRegion = getExtendedRegion(adjacentTile);
			int adjacentAssociation = associations->at(adjacentRegion);
			
			if (adjacentOcean != ocean)
			{
				connections->at(region).insert(adjacentAssociation);
			}
			
		}
		
	}
	
	// extend polar region connections
	
	for (int mapIndex : polarMapIndexes)
	{
		MAP *tile = getMapTile(mapIndex);
		int region = getExtendedRegion(tile);
		int association = associations->at(region);
		
		if (association != region)
		{
			connections->at(region).insert(connections->at(association).begin(), connections->at(association).end());
		}
		
	}
	
	// populate faction specific data
	
	for (int factionId = 0; factionId < MaxPlayerNum; factionId++)
	{
		std::unordered_map<int, int> *factionAssociations = &(data.geography.faction[factionId].associations);
		factionAssociations->clear();
		
		std::unordered_map<MAP *, int> *factionCoastalBaseOceanAssociations = &(data.geography.faction[factionId].coastalBaseOceanAssociations);
		factionCoastalBaseOceanAssociations->clear();
		
		std::unordered_map<int, std::unordered_set<int>> *factionConnections = &(data.geography.faction[factionId].connections);
		factionConnections->clear();
		
		// populate friendly base ocean region sets
		
		std::unordered_map<MAP *, std::unordered_set<int>> coastalBaseOceanRegionSets;
		std::vector<std::unordered_set<int>> joinedOceanRegionSets;
		
		for (int baseId = 0; baseId < *total_num_bases; baseId++)
		{
			BASE *base = &(Bases[baseId]);
			MAP *baseTile = getBaseMapTile(baseId);
			
			// friendly base
			
			if (!(base->faction_id == factionId || has_pact(factionId, base->faction_id)))
				continue;
			
			// land base
			
			if (isOceanRegion(baseTile->region))
				continue;
			
			// get base ocean regions
			
			std::unordered_set<int> baseOceanRegionSet = getBaseOceanRegions(baseId);
			
			// coastal base
			
			if (baseOceanRegionSet.size() == 0)
				continue;
			
			// store base ocean regions
			
			coastalBaseOceanRegionSets.insert({baseTile, baseOceanRegionSet});
			factionCoastalBaseOceanAssociations->insert({baseTile, associations->at(*(baseOceanRegionSet.begin()))});
			
			// joined oceans
			
			if (baseOceanRegionSet.size() >= 2)
			{
				joinedOceanRegionSets.push_back(baseOceanRegionSet);
			}
			
		}
		
		// no joined oceans
		
		if (joinedOceanRegionSets.size() == 0)
			continue;
		
		// populate faction associations and connections
		
		factionAssociations->insert(associations->begin(), associations->end());
		factionConnections->insert(connections->begin(), connections->end());
		
		// update faction associations
		
		for (std::unordered_set<int> &joinedOceanRegionSet : joinedOceanRegionSets)
		{
			// find affected associations
			
			std::unordered_set<int> affectedAssociations;
			int resultingAssociation = INT_MAX;
			std::unordered_set<int> joinedConnections;
			
			for (int joinedOceanRegion : joinedOceanRegionSet)
			{
				int joinedOceanAssociation = factionAssociations->at(joinedOceanRegion);
				
				affectedAssociations.insert(joinedOceanAssociation);
				resultingAssociation = std::min(resultingAssociation, joinedOceanAssociation);
				joinedConnections.insert(factionConnections->at(joinedOceanAssociation).begin(), factionConnections->at(joinedOceanAssociation).end());
				
			}
			
			// update affected associations and connections
			
			for (const auto &factionAssociationsEntry : *factionAssociations)
			{
				int region = factionAssociationsEntry.first;
				int association = factionAssociationsEntry.second;
				
				if (affectedAssociations.count(association) != 0)
				{
					factionAssociations->at(region) = resultingAssociation;
					factionConnections->at(region).insert(joinedConnections.begin(), joinedConnections.end());
				}
				
			}
			
		}
		
		// update coastalBaseOceanAssociations
		
		for (const auto &factionCoastalBaseOceanAssociationsEntry : *factionCoastalBaseOceanAssociations)
		{
			MAP *baseTile = factionCoastalBaseOceanAssociationsEntry.first;
			int association = factionCoastalBaseOceanAssociationsEntry.second;
			
			factionCoastalBaseOceanAssociations->at(baseTile) = factionAssociations->at(association);
			
		}
		
	}
	
	if (DEBUG)
	{
		debug("\tassociations\n");
		
		std::map<int, int> sortedAssociations;
		sortedAssociations.insert(data.geography.associations.begin(), data.geography.associations.end());
		
		for (const auto &association : sortedAssociations)
		{
			debug("\t\t%4d -> %4d\n", association.first, association.second);
			
		}
		
		debug("\tconnections\n");
		
		std::map<int, std::unordered_set<int>> sortedConnections;
		sortedConnections.insert(data.geography.connections.begin(), data.geography.connections.end());
		
		for (auto const &connectionsEntry : sortedConnections)
		{
			int region = connectionsEntry.first;
			const std::unordered_set<int> *regionConnections = &(connectionsEntry.second);
			
			debug("\t%4d ->", region);
			
			std::set<int> sortedRegionConnections;
			sortedRegionConnections.insert(regionConnections->begin(), regionConnections->end());
			
			for (int connectionRegion : sortedRegionConnections)
			{
				debug(" %4d", connectionRegion);
			
			}
			
			debug("\n");
			
		}
		
		for (int factionId = 0; factionId < MaxPlayerNum; factionId++)
		{
			debug("\tfactionId=%d\n", factionId);
			
			debug("\t\tassociations\n");
			
			std::map<int, int> sortedFactionAssociations;
			sortedFactionAssociations.insert(data.geography.faction[factionId].associations.begin(), data.geography.faction[factionId].associations.end());
			
			for (auto const &associationsEntry : sortedFactionAssociations)
			{
				int region = associationsEntry.first;
				int association = associationsEntry.second;
				
				debug("\t\t\t%4d -> %4d\n", region, association);
				
			}
			
			debug("\t\tconnections\n");
			
			std::map<int, std::unordered_set<int>> sortedFactionConnections;
			sortedFactionConnections.insert(data.geography.faction[factionId].connections.begin(), data.geography.faction[factionId].connections.end());
			
			for (auto const &connectionsEntry : sortedFactionConnections)
			{
				int region = connectionsEntry.first;
				const std::unordered_set<int> *regionConnections = &(connectionsEntry.second);
				
				debug("\t\t\t%4d ->", region);
				
				std::set<int> sortedRegionConnections;
				sortedRegionConnections.insert(regionConnections->begin(), regionConnections->end());
				
				for (int connectionRegion : sortedRegionConnections)
				{
					debug(" %4d", connectionRegion);
				
				}
				
				debug("\n");
				
			}
			
			debug("\t\tcoastalBaseOceanAssociations\n");
			
			std::map<MAP *, int> sortedFactionCoastalBaseOceanAssociations;
			sortedFactionCoastalBaseOceanAssociations.insert(data.geography.faction[factionId].coastalBaseOceanAssociations.begin(), data.geography.faction[factionId].coastalBaseOceanAssociations.end());
			
			for (auto const &coastalBaseOceanAssociationsEntry : sortedFactionCoastalBaseOceanAssociations)
			{
				MAP *baseTile = coastalBaseOceanAssociationsEntry.first;
				int coastalBaseOceanAssociation = coastalBaseOceanAssociationsEntry.second;
				
				Location baseLocation = getLocation(baseTile);
				
				debug("\t\t\t(%3d,%3d) -> %d", baseLocation.x, baseLocation.y, coastalBaseOceanAssociation);
				
			}
			
			debug("\n");
			
		}
		
	}
		
}

/*
Finds all regions those are accessible by other factions.
Set bases accessible to shared ocean regions.
*/
void setSharedOceanRegions()
{
	// find shared ocean regions
	
	std::unordered_set<int> sharedOceanRegions;
	
	for (int baseId = 0; baseId < *total_num_bases; baseId++)
	{
		BASE *base = &(Bases[baseId]);
		
		// skip own
		
		if (base->faction_id == aiFactionId)
			continue;
		
		// get connected ocean regions
		
		std::set<int> baseConnectedOceanRegions = getBaseConnectedOceanRegions(baseId);
		
		// add regions
		
		sharedOceanRegions.insert(baseConnectedOceanRegions.begin(), baseConnectedOceanRegions.end());
		
	}
	
	// update own bases
	
	for (int baseId : data.baseIds)
	{
		// clear flag by default
		
		data.baseStrategies[baseId].inSharedOceanRegion = false;
		
		// get connected ocean regions
		
		std::set<int> baseConnectedOceanRegions = getBaseConnectedOceanRegions(baseId);
		
		for (int baseConnectedOceanRegion : baseConnectedOceanRegions)
		{
			if (sharedOceanRegions.count(baseConnectedOceanRegion))
			{
				data.baseStrategies[baseId].inSharedOceanRegion = true;
				break;
			}
			
		}
		
	}
	
}

void populateGlobalVariables()
{
	// maxBaseSize
	
	data.maxBaseSize = getMaxBaseSize(aiFactionId);
	
	// best weapon and armor
	
	data.bestWeaponOffenseValue = getFactionBestPrototypedWeaponOffenseValue(aiFactionId);
	data.bestArmorDefenseValue = getFactionBestPrototypedArmorDefenseValue(aiFactionId);
	
	// populate factions combat modifiers

	debug("%-24s\nfactionCombatModifiers:\n", MFactions[aiFactionId].noun_faction);

	for (int id = 1; id < 8; id++)
	{
		// store combat modifiers

		data.factionInfos[id].offenseMultiplier = getFactionOffenseMultiplier(id);
		data.factionInfos[id].defenseMultiplier = getFactionDefenseMultiplier(id);
		data.factionInfos[id].fanaticBonusMultiplier = getFactionFanaticBonusMultiplier(id);

		debug
		(
			"\t%-24s: offenseMultiplier=%4.2f, defenseMultiplier=%4.2f, fanaticBonusMultiplier=%4.2f\n",
			MFactions[id].noun_faction,
			data.factionInfos[id].offenseMultiplier,
			data.factionInfos[id].defenseMultiplier,
			data.factionInfos[id].fanaticBonusMultiplier
		);

	}

	debug("\n");

	// populate other factions threat koefficients

	debug("%-24s\notherFactionthreatCoefficients:\n", MFactions[aiFactionId].noun_faction);

	for (int id = 1; id < 8; id++)
	{
		// skip aliens

		if (id == 0)
			continue;

		// skip self

		if (id == aiFactionId)
			continue;

		// get relation from other faction

		int otherFactionRelation = Factions[id].diplo_status[aiFactionId];

		// calculate threat koefficient

		double threatCoefficient;

		if (otherFactionRelation & DIPLO_VENDETTA)
		{
			threatCoefficient = conf.ai_production_threat_coefficient_vendetta;
		}
		else if (otherFactionRelation & DIPLO_PACT)
		{
			threatCoefficient = conf.ai_production_threat_coefficient_pact;
		}
		else if (otherFactionRelation & DIPLO_TREATY)
		{
			threatCoefficient = conf.ai_production_threat_coefficient_treaty;
		}
		else
		{
			threatCoefficient = conf.ai_production_threat_coefficient_other;
		}
		
		// human threat is increased
		
		if (is_human(id))
		{
			threatCoefficient += conf.ai_production_threat_coefficient_human;
		}
		
		// store threat koefficient

		data.factionInfos[id].threatCoefficient = threatCoefficient;

		debug("\t%-24s: %08x => %4.2f\n", MFactions[id].noun_faction, otherFactionRelation, threatCoefficient);

	}

	// populate factions best combat item values
	
	for (int factionId = 1; factionId < 8; factionId++)
	{
		data.factionInfos[factionId].bestWeaponOffenseValue = getFactionBestPrototypedWeaponOffenseValue(factionId);
		data.factionInfos[factionId].bestArmorDefenseValue = getFactionBestPrototypedArmorDefenseValue(factionId);
	}

	debug("\n");

	// populate bases

	debug("data.baseStrategies\n");

	for (int baseId = 0; baseId < *total_num_bases; baseId++)
	{
		BASE *base = &(Bases[baseId]);

		// exclude not own bases

		if (base->faction_id != aiFactionId)
			continue;

		// add base
		
		data.baseIds.push_back(baseId);
		
		// add base location

		MAP *baseLocation = getMapTile(base->x, base->y);
		data.baseLocations[baseLocation] = baseId;

		// add base strategy

		data.baseStrategies[baseId] = {};
		BaseStrategy *baseStrategy = &(data.baseStrategies[baseId]);
		
		baseStrategy->base = base;
		baseStrategy->intrinsicDefenseMultiplier = getBaseDefenseMultiplier(baseId, -1);
		baseStrategy->conventionalDefenseMultipliers[TRIAD_LAND] = getBaseDefenseMultiplier(baseId, TRIAD_LAND);
		baseStrategy->conventionalDefenseMultipliers[TRIAD_SEA] = getBaseDefenseMultiplier(baseId, TRIAD_SEA);
		baseStrategy->conventionalDefenseMultipliers[TRIAD_AIR] = getBaseDefenseMultiplier(baseId, TRIAD_AIR);
		baseStrategy->sensorOffenseMultiplier = getSensorOffenseMultiplier(base->faction_id, base->x, base->y);
		baseStrategy->sensorDefenseMultiplier = getSensorDefenseMultiplier(base->faction_id, base->x, base->y);

		debug("\n[%3d] %-25s\n", baseId, data.baseStrategies[baseId].base->name);

		// add base regions
		
		int baseRegion = getBaseMapTile(baseId)->region;
		
		data.presenceRegions.insert(getBaseMapTile(baseId)->region);
		data.regionBaseIds[baseRegion].insert(baseId);

		std::set<int> baseConnectedRegions = getBaseConnectedRegions(baseId);

		for (int region : baseConnectedRegions)
		{
			if (data.regionBaseGroups.find(region) == data.regionBaseGroups.end())
			{
				data.regionBaseGroups[region] = std::vector<int>();
			}

			data.regionBaseGroups[region].push_back(baseId);

		}

	}

	debug("\n");

	// populate vehicles
	
	debug("populate vehicles - %s\n", MFactions[aiFactionId].noun_faction);
	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(Vehicles[id]);
		MAP *vehicleTile = getVehicleMapTile(id);

		// store all vehicle current id in pad_0 field

		vehicle->pad_0 = id;

		// further process only own vehicles

		if (vehicle->faction_id != aiFactionId)
			continue;
		
		debug("\t[%3d] (%3d,%3d) region = %3d\n", id, vehicle->x, vehicle->y, vehicleTile->region);
		
		// add vehicle
		
		data.vehicleIds.push_back(id);

		// combat vehicles

		if (isCombatVehicle(id))
		{
			// add vehicle to global list

			data.combatVehicleIds.push_back(id);
			
			// scout and native
			
			if (isScoutVehicle(id) || isNativeVehicle(id))
			{
				data.scoutVehicleIds.push_back(id);
			}

			// add surface vehicle to region list
			// except land unit in ocean
			
			if (vehicle->triad() != TRIAD_AIR && !(vehicle->triad() == TRIAD_LAND && isOceanRegion(vehicleTile->region)))
			{
				if (data.regionSurfaceCombatVehicleIds.count(vehicleTile->region) == 0)
				{
					data.regionSurfaceCombatVehicleIds[vehicleTile->region] = std::vector<int>();
				}
				data.regionSurfaceCombatVehicleIds[vehicleTile->region].push_back(id);
				
				// add scout to region list
				
				if (isScoutVehicle(id))
				{
					if (data.regionSurfaceScoutVehicleIds.count(vehicleTile->region) == 0)
					{
						data.regionSurfaceScoutVehicleIds[vehicleTile->region] = std::vector<int>();
					}
					data.regionSurfaceScoutVehicleIds[vehicleTile->region].push_back(id);
				}
				
			}

			// find if vehicle is at base

			MAP *vehicleLocation = getMapTile(vehicle->x, vehicle->y);
			std::unordered_map<MAP *, int>::iterator baseLocationsIterator = data.baseLocations.find(vehicleLocation);

			if (baseLocationsIterator == data.baseLocations.end())
			{
				// add outside vehicle

				data.outsideCombatVehicleIds.push_back(id);

			}
			else
			{
				BaseStrategy *baseStrategy = &(data.baseStrategies[baseLocationsIterator->second]);

				// add combat vehicle to garrison
				
				if (isCombatVehicle(id))
				{
					baseStrategy->garrison.push_back(id);
				}

				// add to native protection

				double nativeProtection = calculateNativeDamageDefense(id) / 10.0;

				if (vehicle_has_ability(vehicle, ABL_TRANCE))
				{
					nativeProtection *= (1 + (double)Rules->combat_bonus_trance_vs_psi / 100.0);
				}

				baseStrategy->nativeProtection += nativeProtection;

			}

		}
		else if (isColonyVehicle(id))
		{
			data.colonyVehicleIds.push_back(id);
		}
		else if (isFormerVehicle(id))
		{
			data.formerVehicleIds.push_back(id);
		}

	}

	// populate units

    for (int i = 0; i < 128; i++)
	{
        int id = (i < 64 ? i : (aiFactionId - 1) * 64 + i);

        UNIT *unit = &Units[id];

		// skip not enabled

		if (id < 64 && !has_tech(aiFactionId, unit->preq_tech))
			continue;

        // skip empty

        if (strlen(unit->name) == 0)
			continue;

        // skip obsolete

        if ((unit->obsolete_factions & (0x1 << aiFactionId)) != 0)
			continue;

		// add unit

		data.unitIds.push_back(id);
		
		// add combat unit
		// either psi or anti psi unit or conventional with best weapon/armor
		
		if
		(
			isCombatUnit(id)
			&&
			(
				Weapon[unit->weapon_type].offense_value < 0
				||
				Armor[unit->armor_type].defense_value < 0
				||
				unit_has_ability(id, ABL_TRANCE)
				||
				unit_has_ability(id, ABL_EMPATH)
				||
				Weapon[unit->weapon_type].offense_value >= data.bestWeaponOffenseValue
				||
				Armor[unit->armor_type].defense_value >= data.bestArmorDefenseValue
			)
		)
		{
			data.combatUnitIds.push_back(id);
			
			// populate specific triads
			
			switch (unit->triad())
			{
			case TRIAD_LAND:
				data.landCombatUnitIds.push_back(id);
				data.landAndAirCombatUnitIds.push_back(id);
				break;
			case TRIAD_SEA:
				data.seaCombatUnitIds.push_back(id);
				data.seaAndAirCombatUnitIds.push_back(id);
				break;
			case TRIAD_AIR:
				data.airCombatUnitIds.push_back(id);
				data.landAndAirCombatUnitIds.push_back(id);
				data.seaAndAirCombatUnitIds.push_back(id);
				break;
			}
			
		}
		
	}
	
	// max mineral surplus
	
	data.maxMineralSurplus = 1;
	
	for (int baseId : data.baseIds)
	{
		BASE *base = &(Bases[baseId]);
		MAP *baseTile = getBaseMapTile(baseId);
		
		data.maxMineralSurplus = std::max(data.maxMineralSurplus, base->mineral_surplus);
		
		if (data.regionMaxMineralSurpluses.count(baseTile->region) == 0)
		{
			data.regionMaxMineralSurpluses[baseTile->region] = 1;
		}
		
		data.regionMaxMineralSurpluses[baseTile->region] = std::max(data.regionMaxMineralSurpluses[baseTile->region], base->mineral_surplus);
		
	}
	
	// populate factions airbases
	
	for (int factionId = 1; factionId < 8; factionId++)
	{
		data.factionInfos[factionId].airbases.clear();
		
		// stationary airbases
		
		for (int mapIndex = 0; mapIndex < *map_area_tiles; mapIndex++)
		{
			Location location = getMapIndexLocation(mapIndex);
			MAP *tile = getMapTile(mapIndex);
			
			if
			(
				map_has_item(tile, TERRA_BASE_IN_TILE | TERRA_AIRBASE)
				&&
				(tile->owner == factionId || has_pact(factionId, tile->owner))
			)
			{
				data.factionInfos[factionId].airbases.push_back(location);
			}
			
		}
		
		// mobile airbases
		
		for (int vehicleId = 0; vehicleId < *total_num_vehicles; vehicleId++)
		{
			VEH *vehicle = &(Vehicles[vehicleId]);
			
			if
			(
				vehicle_has_ability(vehicleId, ABL_CARRIER)
				&&
				(vehicle->faction_id == factionId || has_pact(factionId, vehicle->faction_id))
			)
			{
				data.factionInfos[factionId].airbases.push_back(Location(vehicle->x, vehicle->y));
			}
			
		}
		
	}
	
	// base exposure
	
	populateBaseExposures();
	
	// best vehicle armor
	
	data.bestVehicleWeaponOffenseValue = 0;
	data.bestVehicleArmorDefenseValue = 0;
	
	for (int vehicleId = 0; vehicleId < *total_num_vehicles; vehicleId++)
	{
		VEH *vehicle = &(Vehicles[vehicleId]);
		
		data.bestVehicleWeaponOffenseValue = std::max(data.bestVehicleWeaponOffenseValue, vehicle->offense_value());
		data.bestVehicleArmorDefenseValue = std::max(data.bestVehicleArmorDefenseValue, vehicle->defense_value());
		
	}
	
}

VEH *getVehicleByAIId(int aiId)
{
	// check if ID didn't change

	VEH *oldVehicle = &(Vehicles[aiId]);

	if (oldVehicle->pad_0 == aiId)
		return oldVehicle;

	// otherwise, scan all vehicles

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(Vehicles[id]);

		if (vehicle->pad_0 == aiId)
			return vehicle;

	}

	return NULL;

}

Location getNearestPodLocation(int vehicleId)
{
	VEH *vehicle = &(Vehicles[vehicleId]);
	
	Location nearestPodLocation;
	int minRange = INT_MAX;
	
	for (int mapIndex = 0; mapIndex < *map_area_tiles; mapIndex++)
	{
		Location location = getMapIndexLocation(mapIndex);
		
		if (isDestinationReachable(vehicleId, location.x, location.y, false) && (goody_at(location.x, location.y) != 0))
		{
			int range = map_range(vehicle->x, vehicle->y, location.x, location.y);
			
			if (range < minRange)
			{
				nearestPodLocation.set(location);
				minRange = range;
			}
			
		}
		
	}
	
	return nearestPodLocation;
	
}

void designUnits()
{
	// get best values
	
	int bestWeapon = getFactionBestWeapon(aiFactionId);
	int bestArmor = getFactionBestArmor(aiFactionId);
	int fastLandChassis = (has_chassis(aiFactionId, CHS_HOVERTANK) ? CHS_HOVERTANK : CHS_SPEEDER);
	int fastSeaChassis = (has_chassis(aiFactionId, CHS_CRUISER) ? CHS_CRUISER : CHS_FOIL);
	int bestReactor = best_reactor(aiFactionId);
	
	// land defenders
	
	proposeMultiplePrototypes
	(
		aiFactionId,
		{CHS_INFANTRY},
		{WPN_HAND_WEAPONS},
		{bestArmor},
		{0, ABL_COMM_JAMMER, ABL_AAA, ABL_POLY_ENCRYPTION, ABL_POLICE_2X},
		bestReactor,
		PLAN_DEFENSIVE,
		NULL
	);
	
	// land attackers
	
	proposeMultiplePrototypes
	(
		aiFactionId,
		{CHS_INFANTRY},
		{bestWeapon},
		{ARM_NO_ARMOR},
		{0, ABL_AMPHIBIOUS, ABL_BLINK_DISPLACER, ABL_SOPORIFIC_GAS, ABL_ANTIGRAV_STRUTS},
		bestReactor,
		PLAN_OFFENSIVE,
		NULL
	);
	
	// land paratroopers
	
	proposeMultiplePrototypes
	(
		aiFactionId,
		{CHS_INFANTRY},
		{bestWeapon},
		{bestArmor},
		{ABL_DROP_POD},
		bestReactor,
		PLAN_COMBAT,
		NULL
	);
	
	// land armored attackers
	
	proposeMultiplePrototypes
	(
		aiFactionId,
		{CHS_INFANTRY},
		{bestWeapon},
		{bestArmor},
		{0, ABL_AMPHIBIOUS, ABL_BLINK_DISPLACER, ABL_SOPORIFIC_GAS, ABL_COMM_JAMMER, ABL_AAA, ABL_POLY_ENCRYPTION},
		bestReactor,
		PLAN_COMBAT,
		NULL
	);
	
	// land fast attackers
	
	proposeMultiplePrototypes
	(
		aiFactionId,
		{fastLandChassis},
		{bestWeapon},
		{ARM_NO_ARMOR},
		{0, ABL_AMPHIBIOUS, ABL_BLINK_DISPLACER, ABL_SOPORIFIC_GAS, ABL_AIR_SUPERIORITY, ABL_ANTIGRAV_STRUTS, ABL_DISSOCIATIVE_WAVE},
		bestReactor,
		PLAN_OFFENSIVE,
		NULL
	);
	
	// ships
	
	proposeMultiplePrototypes
	(
		aiFactionId,
		{fastSeaChassis},
		{bestWeapon},
		{bestArmor},
		{0, ABL_AAA, ABL_BLINK_DISPLACER, ABL_SOPORIFIC_GAS, ABL_MARINE_DETACHMENT},
		bestReactor,
		PLAN_COMBAT,
		NULL
	);
	
}

/*
Propose multiple prototype combinations.
*/
void proposeMultiplePrototypes(int factionId, std::vector<int> chassisIds, std::vector<int> weaponIds, std::vector<int> armorIds, std::vector<int> abilitiesSets, int reactor, int plan, char *name)
{
	for (int chassisId : chassisIds)
	{
		for (int weaponId : weaponIds)
		{
			for (int armorId : armorIds)
			{
				for (int abilitiesSet : abilitiesSets)
				{
					checkAndProposePrototype(factionId, chassisId, weaponId, armorId, abilitiesSet, reactor, plan, name);
					
				}
				
			}
			
		}
		
	}
	
}

/*
Verify proposed prototype is allowed and propose it if yes.
Verifies all technologies are available and abilities area allowed.
*/
void checkAndProposePrototype(int factionId, int chassisId, int weaponId, int armorId, int abilities, int reactor, int plan, char *name)
{
	// check chassis is available
	
	if (!has_chassis(factionId, chassisId))
		return;
	
	// check weapon is available
	
	if (!has_weapon(factionId, weaponId))
		return;
	
	// check armor is available
	
	if (!has_armor(factionId, armorId))
		return;
	
	// check reactor is available
	
	if (!has_reactor(factionId, reactor))
		return;
	
	// check abilities are available and allowed
	
	for (int abilityId = ABL_ID_SUPER_TERRAFORMER; abilityId <= ABL_ID_ALGO_ENHANCEMENT; abilityId++)
	{
		int abilityFlag = (0x1 << abilityId);
		
		if ((abilities & abilityFlag) == 0)
			continue;
		
		if (!has_ability(factionId, abilityId))
			return;
		
		CAbility *ability = &(Ability[abilityId]);
		
		// not allowed for triad
		
		switch (Chassis[chassisId].triad)
		{
		case TRIAD_LAND:
			if ((ability->flags & AFLAG_ALLOWED_LAND_UNIT) == 0)
				return;
			break;
			
		case TRIAD_SEA:
			if ((ability->flags & AFLAG_ALLOWED_SEA_UNIT) == 0)
				return;
			break;
			
		case TRIAD_AIR:
			if ((ability->flags & AFLAG_ALLOWED_AIR_UNIT) == 0)
				return;
			break;
			
		}
		
		// not allowed for combat unit
		
		if (Weapon[weaponId].offense_value != 0 && (ability->flags & AFLAG_ALLOWED_COMBAT_UNIT) == 0)
			return;
		
		// not allowed for terraform unit
		
		if (weaponId == WPN_TERRAFORMING_UNIT && (ability->flags & AFLAG_ALLOWED_TERRAFORM_UNIT) == 0)
			return;
		
		// not allowed for non-combat unit
		
		if (Weapon[weaponId].offense_value == 0 && (ability->flags & AFLAG_ALLOWED_NONCOMBAT_UNIT) == 0)
			return;
		
		// not allowed for probe team
		
		if (weaponId == WPN_PROBE_TEAM && (ability->flags & AFLAG_NOT_ALLOWED_PROBE_TEAM) != 0)
			return;
		
		// not allowed for non transport unit
		
		if (weaponId != WPN_TROOP_TRANSPORT && (ability->flags & AFLAG_TRANSPORT_ONLY_UNIT) != 0)
			return;
		
		// not allowed for fast unit
		
		if (chassisId == CHS_INFANTRY && Chassis[chassisId].speed > 1 && (ability->flags & AFLAG_NOT_ALLOWED_FAST_UNIT) != 0)
			return;
		
		// not allowed for non probes
		
		if (weaponId != WPN_PROBE_TEAM && (ability->flags & AFLAG_ONLY_PROBE_TEAM) != 0)
			return;
		
	}
	
	// propose prototype
	
	int unitId = modified_propose_proto(factionId, chassisId, weaponId, armorId, abilities, reactor, plan, name);
	
	debug("checkAndProposePrototype - %s\n", MFactions[aiFactionId].noun_faction);
	debug("\treactor=%d, chassisId=%d, weaponId=%d, armorId=%d, abilities=%s\n", reactor, chassisId, weaponId, armorId, getAbilitiesString(abilities).c_str());
	debug("\tunitId=%d\n", unitId);
	
}

void evaluateBaseNativeDefenseDemands()
{
	debug("evaluateBaseNativeDefenseDemands\n");
	
	for (int baseId : data.baseIds)
	{
		BASE *base = &(Bases[baseId]);
		MAP *baseTile = getBaseMapTile(baseId);
		bool ocean = isOceanRegion(baseTile->region);
		
		debug("\t%s\n", base->name);
		
		// estimate native attack strength
		
		double nativePsiAttackStrength = getNativePsiAttackStrength(ocean ? TRIAD_SEA : TRIAD_LAND);
		debug("\t\tnativePsiAttackStrength = %f\n", nativePsiAttackStrength);
		
		// calculate count of the nearest unpopulated tiles
		
		int unpopulatedTiles = 28 - (nearby_items(base->x, base->y, 3, TERRA_BASE_RADIUS) - 21);
		debug("\t\tunpopulatedTiles = %d\n", unpopulatedTiles);
		
		// estimate count
		
		double anticipatedCount = 0.5 + 1.0 * ((double)unpopulatedTiles / 28.0);
		debug("\t\tanticipatedCount = %f\n", anticipatedCount);
		
		// calculate pop chance
		
		double popChance = 0.0;
		
		for (int otherBaseId : data.baseIds)
		{
			BASE *otherBase =&(Bases[otherBaseId]);
			MAP *otherBaseTile = getBaseMapTile(otherBaseId);
			
			// same region only
			
			if (otherBaseTile->region != baseTile->region)
				continue;
			
			// calculate range
			
			int range = map_range(base->x, base->y, otherBase->x, otherBase->y);
			
			// only in vicinity
			
			if (range >= 10)
				continue;
			
			// compute contribution
			
			popChance += ((double)std::min(100, otherBase->eco_damage) / 100.0) * (1.0 - (double)range / 10.0);
			
		}
		
		anticipatedCount += (double)((Factions[aiFactionId].pop_total + 1) / 3) * popChance;
		
		double anticipatedNativePsiAttackStrength = std::max(0.0, nativePsiAttackStrength * anticipatedCount);

		// calculate current native protectors in base

		double baseNativeProtection = std::max(0.0, getBaseNativeProtection(baseId));
		double remainingBaseNativeProtection = anticipatedNativePsiAttackStrength - baseNativeProtection;
		
		debug("\t\tbaseNativeProtection=%f, remainingBaseNativeProtection=%f\n", baseNativeProtection, remainingBaseNativeProtection);
		
		// store demand
		
		data.baseAnticipatedNativeAttackStrengths[baseId] = anticipatedNativePsiAttackStrength;
		data.baseRemainingNativeProtectionDemands[baseId] = remainingBaseNativeProtection;
		
	}
	
}

int getNearestFactionBaseRange(int factionId, int x, int y)
{
	int nearestFactionBaseRange = 9999;

	for (int baseId = 0; baseId < *total_num_bases; baseId++)
	{
		BASE *base = &(Bases[baseId]);

		// own bases

		if (base->faction_id != factionId)
			continue;

		nearestFactionBaseRange = std::min(nearestFactionBaseRange, map_range(x, y, base->x, base->y));

	}

	return nearestFactionBaseRange;

}

int getNearestOtherFactionBaseRange(int factionId, int x, int y)
{
	int nearestFactionBaseRange = INT_MAX;

	for (int baseId = 0; baseId < *total_num_bases; baseId++)
	{
		BASE *base = &(Bases[baseId]);

		// other faction bases

		if (base->faction_id == factionId)
			continue;

		nearestFactionBaseRange = std::min(nearestFactionBaseRange, map_range(x, y, base->x, base->y));

	}

	return nearestFactionBaseRange;

}

int getNearestBaseId(int x, int y, std::unordered_set<int> baseIds)
{
	int nearestBaseId = -1;
	int nearestBaseRange = INT_MAX;

	for (int baseId : baseIds)
	{
		BASE *base = &(Bases[baseId]);
		
		int range = map_range(x, y, base->x, base->y);
		
		if (nearestBaseId == -1 || range < nearestBaseRange)
		{
			nearestBaseId = baseId;
			nearestBaseRange = range;
		}

	}

	return nearestBaseId;

}

int getNearestBaseRange(int x, int y, std::unordered_set<int> baseIds)
{
	int nearestBaseRange = INT_MAX;

	for (int baseId : baseIds)
	{
		BASE *base = &(Bases[baseId]);

		nearestBaseRange = std::min(nearestBaseRange, map_range(x, y, base->x, base->y));

	}

	return nearestBaseRange;

}

void evaluateBaseExposures()
{
	for (int baseId : data.baseIds)
	{
		BASE *base = &(Bases[baseId]);
		MAP *baseTile = getBaseMapTile(baseId);
		bool ocean = isOceanRegion(baseTile->region);
		
		// find nearest not own tile
		
		int nearestNotOwnTileRange = INT_MAX;

		for (int mapIndex = 0; mapIndex < *map_area_tiles; mapIndex++)
		{
			Location location = getMapIndexLocation(mapIndex);
			MAP* tile = getMapTile(mapIndex);
			
			// exclude own tiles
			
			if (tile->owner == aiFactionId)
				continue;
			
			// calculate range
			
			int range = map_range(base->x, base->y, location.x, location.y);
			
			nearestNotOwnTileRange = std::min(nearestNotOwnTileRange, range);
			
		}
		
		// set base distance based on realm
		
		double baseDistance = (ocean ? 7.0 : 3.0);
		
		// estimate exposure
		
		double exposure = 1.0 / std::max(1.0, (double)nearestNotOwnTileRange / baseDistance);
		
		data.baseStrategies[baseId].exposure = exposure;
		
	}
	
}

int getFactionBestPrototypedWeaponOffenseValue(int factionId)
{
	int bestWeaponOffenseValue = 0;
	
	for (int unitId : getFactionPrototypes(factionId, false))
	{
		UNIT *unit = &(Units[unitId]);
		
		// skip non combat units

		if (!isCombatUnit(unitId))
			continue;
		
		// get weapon offense value
		
		int weaponOffenseValue = Weapon[unit->weapon_type].offense_value;
		
		// update best weapon offense value
		
		bestWeaponOffenseValue = std::max(bestWeaponOffenseValue, weaponOffenseValue);
		
	}
	
	return bestWeaponOffenseValue;

}

int getFactionBestPrototypedArmorDefenseValue(int factionId)
{
	int bestArmorDefenseValue = 0;
	
	for (int unitId : getFactionPrototypes(factionId, false))
	{
		UNIT *unit = &(Units[unitId]);
		
		// skip non combat units

		if (!isCombatUnit(unitId))
			continue;
		
		// get armor defense value
		
		int armorDefenseValue = Armor[unit->armor_type].defense_value;
		
		// update best armor defense value
		
		bestArmorDefenseValue = std::max(bestArmorDefenseValue, armorDefenseValue);
		
	}
	
	return bestArmorDefenseValue;

}

/*
Arbitrary algorithm to evaluate generic combat strength.
*/
double evaluateCombatStrength(double offenseValue, double defenseValue)
{
	double adjustedOffenseValue = evaluateOffenseStrength(offenseValue);
	double adjustedDefenseValue = evaluateDefenseStrength(defenseValue);
	
	double combatStrength = std::max(adjustedOffenseValue, adjustedDefenseValue) + 0.5 * std::min(adjustedOffenseValue, adjustedDefenseValue);
	
	return combatStrength;
	
}

/*
Arbitrary algorithm to evaluate generic offense strength.
*/
double evaluateOffenseStrength(double offenseValue)
{
	double adjustedOffenseValue = offenseValue * offenseValue;
	
	return adjustedOffenseValue;
	
}

/*
Arbitrary algorithm to evaluate generic oefense strength.
*/
double evaluateDefenseStrength(double DefenseValue)
{
	double adjustedDefenseValue = DefenseValue * DefenseValue;
	
	return adjustedDefenseValue;
	
}

bool isVehicleThreatenedByEnemyInField(int vehicleId)
{
	VEH *vehicle = &(Vehicles[vehicleId]);
	MAP *vehicleTile = getVehicleMapTile(vehicleId);
	
	// not in base
	
	if (map_has_item(vehicleTile, TERRA_BASE_IN_TILE))
		return false;
	
	// check other units
	
	bool threatened = false;
	
	for (int otherVehicleId = 0; otherVehicleId < *total_num_vehicles; otherVehicleId++)
	{
		VEH *otherVehicle = &(Vehicles[otherVehicleId]);
		MAP *otherVehicleTile = getVehicleMapTile(otherVehicleId);
		
		// exclude non alien units not in war
		
		if (otherVehicle->faction_id != 0 && !at_war(otherVehicle->faction_id, vehicle->faction_id))
			continue;
		
		// exclude non combat units
		
		if (!isCombatVehicle(otherVehicleId))
			continue;
		
		// unit is in different region and not air
		
		if (otherVehicle->triad() != TRIAD_AIR && otherVehicleTile->region != vehicleTile->region)
			continue;
		
		// calculate threat range
		
		int threatRange = (otherVehicle->triad() == TRIAD_LAND ? 2 : 1) * Units[otherVehicle->unit_id].speed();
		
		// calculate range
		
		int range = map_range(otherVehicle->x, otherVehicle->y, vehicle->x, vehicle->y);
		
		// compare ranges
		
		if (range <= threatRange)
		{
			threatened = true;
			break;
		}
		
	}
	
	return threatened;
	
}

/*
This function uses frendly ports as connections for current faction vehicles.
*/
bool isDestinationReachable(int vehicleId, int x, int y, bool accountForSeaTransport)
{
	VEH *vehicle = &(Vehicles[vehicleId]);
	MAP *vehicleTile = getVehicleMapTile(vehicleId);
	MAP *destinationTile = getMapTile(x, y);
	
	assert(vehicleTile != NULL);
	assert(destinationTile != NULL);
	
	bool reachable = false;
	
	switch (Units[vehicle->unit_id].chassis_type)
	{
	case CHS_GRAVSHIP:
		
		reachable = true;
		
		break;
		
	case CHS_NEEDLEJET:
	case CHS_COPTER:
	case CHS_MISSILE:
		
		// Some complex logic here. For now disable it.
		
		reachable = false;
		
		break;
		
	case CHS_FOIL:
	case CHS_CRUISER:
		
		{
			if (vehicle->faction_id == aiFactionId)
			{
				int vehicleOceanAssociation = getOceanAssociation(vehicleTile, vehicle->faction_id);
				int destinationOceanAssociation = getOceanAssociation(destinationTile, vehicle->faction_id);
				
				if (vehicleOceanAssociation != -1 && destinationOceanAssociation != -1 && destinationOceanAssociation == vehicleOceanAssociation)
				{
					reachable = true;
				}
				
			}
			else
			{
				reachable = (destinationTile->region == vehicleTile->region);
			}
			
		}
		
		break;
		
	case CHS_INFANTRY:
	case CHS_SPEEDER:
	case CHS_HOVERTANK:
		
		// only land region is rechable by land vehicle
		if (isLandRegion(destinationTile->region))
		{
			// same region
			if (destinationTile->region == vehicleTile->region)
			{
				reachable = true;
			}
			// different ocean region
			else if (isOceanRegion(vehicleTile->region))
			{
				// next to the destination region
				if (isNextToRegion(vehicle->x, vehicle->y, destinationTile->region))
				{
					if
					(
						// amphibious at a base
						(map_has_item(vehicleTile, TERRA_BASE_IN_TILE) && vehicle_has_ability(vehicleId, ABL_AMPHIBIOUS))
						||
						// transported by sea transport
						(accountForSeaTransport && isLandVehicleOnSeaTransport(vehicleId))
					)
					{
						reachable = true;
					}
					
				}
				
			}
			
		}
		else
		{
			reachable = false;
		}
		
		break;
		
	}
	
	return reachable;
	
}

int getRangeToNearestFactionAirbase(int x, int y, int factionId)
{
	int rangeToNearestFactionAirbase = INT_MAX;
	
	for (Location location : data.factionInfos[factionId].airbases)
	{
		int range = map_range(x, y, location.x, location.y);
		
		rangeToNearestFactionAirbase = std::min(rangeToNearestFactionAirbase, range);
		
	}
	
	return rangeToNearestFactionAirbase;
	
}

void populateBaseExposures()
{
	for (int baseId : data.baseIds)
	{
		MAP *baseTile = getBaseMapTile(baseId);
		bool ocean = isOceanRegion(baseTile->region);
		
		// find nearest enemy base distance
		
		int nearestEnemyBaseDistance = getNearestEnemyBaseDistance(baseId);
		
		// get exposure decay distance
		
		double exposureDecayDistance = (ocean ? 10.0 : 5.0);
		
		// calculate exposure
		
		double exposure = std::max(exposureDecayDistance, (double)nearestEnemyBaseDistance) / exposureDecayDistance;
		
		// set exposure
		
		data.baseStrategies[baseId].exposure = exposure;
		
	}
	
}

int getNearestEnemyBaseDistance(int baseId)
{
	BASE *base = &(Bases[baseId]);
	
	int nearestEnemyBaseDistance = INT_MAX;
	
	for (int otherBaseId = 0; otherBaseId < *total_num_bases; otherBaseId++)
	{
		BASE *otherBase = &(Bases[otherBaseId]);
		
		// skip own
		
		if (otherBase->faction_id == base->faction_id)
			continue;
		
		// get distance
		
		int distance = vector_dist(base->x, base->y, otherBase->x, otherBase->y);
		
		// update best values
		
		if (distance < nearestEnemyBaseDistance)
		{
			nearestEnemyBaseDistance = distance;
		}
		
	}
	
	return nearestEnemyBaseDistance;
	
}

void evaluateDefenseDemand()
{
	debug("\n\n==================================================\nevaluateDefenseDemand - %s\n", MFactions[aiFactionId].noun_faction);
	
	// evaluate base defense demands
	
	data.mostVulnerableBaseId = -1;
	data.mostVulnerableBaseDefenseDemand = 0.0;
	
	for (int baseId : data.baseIds)
	{
		BASE *base = &(Bases[baseId]);
		MAP *baseTile = getBaseMapTile(baseId);
		bool ocean = isOceanRegion(baseTile->region);
		BaseStrategy *baseStrategy = &(data.baseStrategies[baseId]);
		
		debug
		(
			"\n\t<>\n\t(%3d,%3d) %s, sensorOffenseMultiplier=%f, sensorDefenseMultiplier=%f, intrinsicDefenseMultiplier=%f, conventionalDefenseMultipliers[TRIAD_LAND]=%f, conventionalDefenseMultipliers[TRIAD_SEA]=%f, conventionalDefenseMultipliers[TRIAD_AIR]=%f\n",
			base->x,
			base->y,
			base->name,
			baseStrategy->sensorOffenseMultiplier,
			baseStrategy->sensorDefenseMultiplier,
			baseStrategy->intrinsicDefenseMultiplier,
			baseStrategy->conventionalDefenseMultipliers[TRIAD_LAND],
			baseStrategy->conventionalDefenseMultipliers[TRIAD_SEA],
			baseStrategy->conventionalDefenseMultipliers[TRIAD_AIR]
		);
		
		// calculate defender strength
		
		MilitaryStrength *defenderStrength = &(baseStrategy->defenderStrength);
		
		debug("\t\tdefenderMilitaryStrength\n");
		
		for (int vehicleId : data.combatVehicleIds)
		{
			VEH *vehicle = &(Vehicles[vehicleId]);
			int unitId = vehicle->unit_id;
			UNIT *unit = &(Units[unitId]);
			int chassisId = unit->chassis_type;
			CChassis *chassis = &(Chassis[chassisId]);
			int triad = chassis->triad;
			MAP *vehicleTile = getVehicleMapTile(vehicleId);
			
			// land base
			if (isLandRegion(baseTile->region))
			{
				// no sea vehicle for land base protection
				
				if (triad == TRIAD_SEA)
					continue;
				
				// no land vehicle in other region for land base protection
				
				if (triad == TRIAD_LAND && vehicleTile->region != baseTile->region)
					continue;
				
			}
			// ocean base
			else
			{
				// no land vehicle everywhere besides base for ocean base protection
				
				if (triad == TRIAD_LAND && !(vehicle->x == base->x && vehicle->y == base->y))
					continue;
				
				// no sea vehicle in other region for ocean base protection
				
				if (triad == TRIAD_SEA && !isVehicleInRegion(vehicleId, baseTile->region))
					continue;
				
			}
			
			// calculate vehicle distance to base
			
			int distance = map_range(base->x, base->y, vehicle->x, vehicle->y);
			
			// limit only vehicle by no farther than 20 tiles away
			
			if (distance > 20)
				continue;
			
			// calculate vehicle speed
			
			double speed = (double)veh_chassis_speed(vehicleId);
			double roadSpeed = (double)mod_veh_speed(vehicleId);
			
			// increase land vehicle speed based on game stage
			
			if (triad == TRIAD_LAND)
			{
				speed = speed + (roadSpeed - speed) * (double)std::min(250, std::max(0, *current_turn - 50)) / 250.0;
			}
			
			// calculate time to reach the base
			
			double time = (double)distance / speed;
			
			// increase effective time for land unit in different region or in ocean
			
			if (triad == TRIAD_LAND && distance > 0 && (isOceanRegion(vehicleTile->region) || vehicleTile->region != baseTile->region))
			{
				time *= 5;
			}
			
			// reduce weight based on time to reach the base
			// every 10 turns reduce weight in half
			
			double weight = pow(0.5, time / 10.0);
			
			// update total weight
			
			defenderStrength->totalWeight += weight;
			
			// create and get entry
			
			if (defenderStrength->unitStrengths.count(unitId) == 0)
			{
				defenderStrength->unitStrengths[unitId] = UnitStrength();
			}
			
			UnitStrength *unitStrength = &(defenderStrength->unitStrengths[unitId]);
			
			// add weight to unit type
			
			unitStrength->weight += weight;
			
			// calculate strength
			
			double psiOffenseStrength = getVehiclePsiOffenseStrength(vehicleId);
			double psiDefenseStrength = getVehiclePsiDefenseStrength(vehicleId, true);
			
			double conventionalOffenseStrength = getVehicleConventionalOffenseStrength(vehicleId);
			double conventionalDefenseStrength = getVehicleConventionalDefenseStrength(vehicleId, true);
			
			// psi strength for all vehicles
			
			unitStrength->psiOffense += weight * psiOffenseStrength;
			unitStrength->psiDefense += weight * psiDefenseStrength;
			
			// conventional strength for regular vehicles only
			
			if (!isNativeUnit(unitId))
			{
				unitStrength->conventionalOffense += weight * conventionalOffenseStrength;
				unitStrength->conventionalDefense += weight * conventionalDefenseStrength;
			}
			
			debug
			(
				"\t\t\t(%3d,%3d) %-32s distance=%3d, time=%6.2f, weight=%f, psiOffenseStrength=%f, psiDefenseStrength=%f, conventionalOffenseStrength=%f, conventionalDefenseStrength=%f\n",
				vehicle->x, vehicle->y, Units[vehicle->unit_id].name, distance, time, weight,
				psiOffenseStrength, psiDefenseStrength, conventionalOffenseStrength, conventionalDefenseStrength
			);
			
		}
		
		// normalize weights
		
		defenderStrength->normalize();
		
		debug("\t\t\ttotalWeight=%f\n", defenderStrength->totalWeight);
		if (DEBUG)
		{
			for (const auto &k : defenderStrength->unitStrengths)
			{
				const int unitId = k.first;
				const UnitStrength *unitStrength = &(k.second);
				
				UNIT *unit = &(Units[unitId]);
				
				debug("\t\t\t\t[%3d] %-32s weight=%f, psiOffense=%f, psiDefense=%f, conventionalOffense=%f, conventionalDefense=%f\n", unitId, unit->name, unitStrength->weight, unitStrength->psiOffense, unitStrength->psiDefense, unitStrength->conventionalOffense, unitStrength->conventionalDefense);
				
			}
			
		}
		
		// calculate opponent strength
		
		MilitaryStrength *opponentStrength = &(baseStrategy->opponentStrength);
		
		debug("\t\topponentMilitaryStrength\n");
		
		for (int vehicleId = 0; vehicleId < *total_num_vehicles; vehicleId++)
		{
			VEH *vehicle = &(Vehicles[vehicleId]);
			int unitId = vehicle->unit_id;
			UNIT *unit = &(Units[unitId]);
			int chassisId = unit->chassis_type;
			CChassis *chassis = &(Chassis[chassisId]);
			int triad = chassis->triad;
			MAP *vehicleTile = getVehicleMapTile(vehicleId);
			
			// not alien
			
			if (vehicle->faction_id == 0)
				continue;
			
			// not own
			
			if (vehicle->faction_id == aiFactionId)
				continue;
			
			// combat
			
			if (!isCombatVehicle(vehicleId))
				continue;
			
			// exclude vehicles unable to attack base
			
			if (!ocean && triad == TRIAD_SEA || (ocean && triad == TRIAD_LAND && !vehicle_has_ability(vehicleId, ABL_AMPHIBIOUS)))
				continue;
			
			// exclude sea units in different region - they cannot possibly reach us
			
			if (triad == TRIAD_SEA && vehicleTile->region != baseTile->region)
				continue;
			
			// calculate vehicle distance to base
			
			int distance = map_range(base->x, base->y, vehicle->x, vehicle->y);
			
			// limit only vehicle by no farther than 20 tiles away
			
			if (distance > 20)
				continue;
			
			// calculate vehicle speed
			
			double speed = (double)veh_chassis_speed(vehicleId);
			double roadSpeed = (double)mod_veh_speed(vehicleId);
			
			// increase land vehicle speed based on game stage
			
			if (triad == TRIAD_LAND)
			{
				speed = speed + (roadSpeed - speed) * (double)std::min(250, std::max(0, *current_turn - 50)) / 250.0;
			}
			
			// calculate time to reach the base
			
			double time = (double)distance / speed;
			
			// increase effective turns for land unit in different region or in ocean
			
			if (triad == TRIAD_LAND && distance > 0 && (isOceanRegion(vehicleTile->region) || vehicleTile->region != baseTile->region))
			{
				time *= 5;
			}
			
			// reduce weight based on time to reach the base
			// every 10 turns reduce weight in half
			
			double weight = pow(0.5, time / 10.0);
			
			// modify strength multiplier based on diplomatic relations
			
			weight *= data.factionInfos[vehicle->faction_id].threatCoefficient;
			
			// exclude empty weight
			
			if (weight == 0.0)
				continue;
			
			// update total weight
			
			opponentStrength->totalWeight += weight;
			
			// pack unit type, create and get entry
			
			if (opponentStrength->unitStrengths.count(unitId) == 0)
			{
				opponentStrength->unitStrengths[unitId] = UnitStrength();
			}
			
			UnitStrength *unitStrength = &(opponentStrength->unitStrengths[unitId]);
			
			// add weight to unit type
			
			unitStrength->weight += weight;
			
			// calculate strength
			
			double basePsiDefenseMultiplier = baseStrategy->intrinsicDefenseMultiplier;
			
			double psiOffenseStrength = getVehiclePsiOffenseStrength(vehicleId) / baseStrategy->sensorDefenseMultiplier / basePsiDefenseMultiplier;
			double psiDefenseStrength = getVehiclePsiDefenseStrength(vehicleId, false) / baseStrategy->sensorOffenseMultiplier;
			
			double baseConventionalDefenseMultiplier = (isVehicleHasAbility(vehicleId, ABL_BLINK_DISPLACER) ? baseStrategy->intrinsicDefenseMultiplier : baseStrategy->conventionalDefenseMultipliers[triad]);
			
			double conventionalOffenseStrength = getVehicleConventionalOffenseStrength(vehicleId) / baseStrategy->sensorDefenseMultiplier / baseConventionalDefenseMultiplier;
			double conventionalDefenseStrength = getVehicleConventionalDefenseStrength(vehicleId, false) / baseStrategy->sensorOffenseMultiplier;
			
			// psi strength for all vehicles
			
			unitStrength->psiOffense += weight * psiOffenseStrength;
			unitStrength->psiDefense += weight * psiDefenseStrength;
			
			// conventional strength for regular vehicles only
			
			if (!isNativeUnit(unitId))
			{
				unitStrength->conventionalOffense += weight * conventionalOffenseStrength;
				unitStrength->conventionalDefense += weight * conventionalDefenseStrength;
			}
			
			debug
			(
				"\t\t\t(%3d,%3d) %-32s distance=%3d, time=%6.2f, weight=%f, psiOffenseStrength=%f, psiDefenseStrength=%f, conventionalOffenseStrength=%f, conventionalDefenseStrength=%f\n",
				vehicle->x, vehicle->y, Units[vehicle->unit_id].name, distance, time, weight,
				psiOffenseStrength, psiDefenseStrength, conventionalOffenseStrength, conventionalDefenseStrength
			);
			
		}
		
		// normalize weights
		
		opponentStrength->normalize();
		
		debug("\t\t\ttotalWeight=%f\n", opponentStrength->totalWeight);
		if (DEBUG)
		{
			for (const auto &k : opponentStrength->unitStrengths)
			{
				const int unitId = k.first;
				const UnitStrength *unitStrength = &(k.second);
				
				UNIT *unit = &(Units[unitId]);
				
				debug("\t\t\t\t[%3d] %-32s weight=%f, psiOffense=%f, psiDefense=%f, conventionalOffense=%f, conventionalDefense=%f\n", unitId, unit->name, unitStrength->weight, unitStrength->psiOffense, unitStrength->psiDefense, unitStrength->conventionalOffense, unitStrength->conventionalDefense);
				
			}
			
		}
		
		// evaluate relative portion of defender destroyed
		
		debug("\t\tdefenderDestroyed\n");
		
		double defenderDestroyed = 0.0;
		
		for (const auto &opponentUnitStrengthEntry : opponentStrength->unitStrengths)
		{
			const int opponentUnitId = opponentUnitStrengthEntry.first;
			const UnitStrength *opponentUnitStrength = &(opponentUnitStrengthEntry.second);
			
			UNIT *opponentUnit = &(Units[opponentUnitId]);
			
			for (const auto defenderStrengthEntry : defenderStrength->unitStrengths)
			{
				const int defenderUnitId = defenderStrengthEntry.first;
				const UnitStrength *defenderUnitStrength = &(defenderStrengthEntry.second);
				
				UNIT *defenderUnit = &(Units[defenderUnitId]);
				
				// impossible combinations
				
				// two needlejets without air superiority
				if
				(
					(opponentUnit->chassis_type == CHS_NEEDLEJET && !unit_has_ability(defenderUnitId, ABL_AIR_SUPERIORITY))
					&&
					(defenderUnit->chassis_type == CHS_NEEDLEJET && !unit_has_ability(opponentUnitId, ABL_AIR_SUPERIORITY))
				)
				{
					continue;
				}
				
				// calculate odds
				
				double attackOdds;
				double defendOdds;
				
				if (isNativeUnit(opponentUnitId) || isNativeUnit(defenderUnitId))
				{
					attackOdds =
						opponentUnitStrength->psiOffense
						/
						defenderUnitStrength->psiDefense
					;
					
					defendOdds =
						opponentUnitStrength->psiDefense
						/
						defenderUnitStrength->psiOffense
					;
					
				}
				else
				{
					attackOdds =
						opponentUnitStrength->conventionalOffense
						/
						defenderUnitStrength->conventionalDefense
						// type to type combat modifier (opponent attacks)
						*
						getConventionalCombatBonusMultiplier(opponentUnitId, defenderUnitId)
					;
					
					defendOdds =
						opponentUnitStrength->conventionalDefense
						/
						defenderUnitStrength->conventionalOffense
						// type to type combat modifier (defender attacks)
						/
						getConventionalCombatBonusMultiplier(defenderUnitId, opponentUnitId)
					;
					
				}
				
				// calculate attack and defend probabilities
				
				double attackProbability;
				double defendProbability;
				
				// defender cannot attack
				if
				(
					// unit without air superiority cannot attack neeglejet
					(opponentUnit->chassis_type == CHS_NEEDLEJET && !unit_has_ability(defenderUnitId, ABL_AIR_SUPERIORITY))
					||
					// land unit cannot attack from sea base
					(ocean && defenderUnit->triad() == TRIAD_LAND)
				)
				{
					attackProbability = 1.0;
					defendProbability = 0.0;
				}
				// opponent cannot attack
				else if
				(
					// unit without air superiority cannot attack neeglejet
					(defenderUnit->chassis_type == CHS_NEEDLEJET && !unit_has_ability(opponentUnitId, ABL_AIR_SUPERIORITY))
				)
				{
					attackProbability = 0.0;
					defendProbability = 1.0;
				}
				// opponent disengages
				else if
				(
					(
						(opponentUnit->triad() == TRIAD_LAND && defenderUnit->triad() == TRIAD_LAND)
						||
						(opponentUnit->triad() == TRIAD_SEA && defenderUnit->triad() == TRIAD_SEA)
					)
					&&
					(unit_chassis_speed(opponentUnitId) > unit_chassis_speed(defenderUnitId))
				)
				{
					attackProbability = 1.0;
					defendProbability = 0.0;
				}
				// defender disengages
				else if
				(
					(
						(opponentUnit->triad() == TRIAD_LAND && defenderUnit->triad() == TRIAD_LAND)
						||
						(opponentUnit->triad() == TRIAD_SEA && defenderUnit->triad() == TRIAD_SEA)
					)
					&&
					(unit_chassis_speed(defenderUnitId) > unit_chassis_speed(opponentUnitId))
				)
				{
					attackProbability = 0.0;
					defendProbability = 1.0;
				}
				else
				{
					attackProbability = 0.5;
					defendProbability = 0.5;
				}
				
				// work out defender choice
				// defender can choose not to attack if it worse than defending
				
				if
				(
					defendProbability > 0.0
					&&
					attackOdds < defendOdds
				)
				{
					attackProbability = 1.0;
					defendProbability = 0.0;
				}
				
				// calculate defender destroyed
				
				double defenderDestroyedTypeVsType =
					// occurence probability
					(opponentUnitStrength->weight * defenderUnitStrength->weight)
					*
					(
						attackProbability * attackOdds
						+
						defendProbability * defendOdds
					)
				;
				
				debug
				(
					"\t\t\topponent: weight=%f %-32s\n\t\t\tdefender: weight=%f %-32s\n\t\t\t\tattack: probability=%f, odds=%f\n\t\t\t\tdefend: probatility=%f, odds=%f\n\t\t\t\tdefenderDestroyedTypeVsType=%f\n",
					opponentUnitStrength->weight,
					opponentUnit->name,
					defenderUnitStrength->weight,
					defenderUnit->name,
					attackProbability,
					attackOdds,
					defendProbability,
					defendOdds,
					defenderDestroyedTypeVsType
				)
				;
				
				// update summaries
				
				defenderDestroyed += defenderDestroyedTypeVsType;
				
			}
			
		}
		
		// defender destroyed total
		
		double defenderDestroyedTotal =
			defenderDestroyed
			// proportion of total weight
			*
			(opponentStrength->totalWeight / defenderStrength->totalWeight)
			// defense superiority coefficient
			*
			conf.ai_production_defense_superiority_coefficient
		;
		
		debug("\t\tdefenderDestroyed=%f, strengthProportion=%f, superiorityCoefficient=%f, defenderDestroyedTotal=%f\n", defenderDestroyed, (opponentStrength->totalWeight / defenderStrength->totalWeight), conf.ai_production_defense_superiority_coefficient, defenderDestroyedTotal);
		
		// set defenseDemand
		
		if (defenderDestroyedTotal > 1.0)
		{
			data.baseStrategies[baseId].defenseDemand = (defenderDestroyedTotal - 1.0) / defenderDestroyedTotal;
		}
		
		debug("\t\tdefenseDemand=%f\n", data.baseStrategies[baseId].defenseDemand);
		
		// update global values
		
		if (data.baseStrategies[baseId].defenseDemand > data.mostVulnerableBaseDefenseDemand)
		{
			data.mostVulnerableBaseId = baseId;
			data.mostVulnerableBaseDefenseDemand = data.baseStrategies[baseId].defenseDemand;
		}
		
	}
	
	debug("\n\tmostVulnerableBase = %s, mostVulnerableBaseDefenseDemand = %f\n", Bases[data.mostVulnerableBaseId].name, data.mostVulnerableBaseDefenseDemand);
	
	// assign defense demand targets
	
	debug("\n\t->\n\ttargetBases\n");
	
	for (int baseId : data.baseIds)
	{
		BASE *base = &(Bases[baseId]);
		MAP *baseTile = getBaseMapTile(baseId);
		
		int bestTargetBaseId = -1;
		double bestTargetBasePreference = 0.0;
		
		for (int targetBaseId : data.baseIds)
		{
			BASE *targetBase = &(Bases[targetBaseId]);
			MAP *targetBaseTile = getBaseMapTile(targetBaseId);
			
			// ignore target ocean base without access to it
			
			if (isOceanRegion(targetBaseTile->region) && !isBaseConnectedToRegion(baseId, targetBaseTile->region))
				continue;
			
			// ignore target land base in different region
			
			if (isLandRegion(targetBaseTile->region) && baseTile->region != targetBaseTile->region)
				continue;
			
			// get range
			
			double range = (double)map_range(base->x, base->y, targetBase->x, targetBase->y);
			
			// adjust range for different regions except coastal base supplying sea units to ocean base
			
			if (baseTile->region != targetBaseTile->region && !(isOceanRegion(targetBaseTile->region) && isBaseConnectedToRegion(baseId, targetBaseTile->region)))
			{
				range *= 5;
			}
			
			// calculate range coefficient
			// preference halves every 20 tiles
			
			double rangeCoefficient = pow(0.5, range / 20.0);
			
			// calculate preference
			
			double targetBasePreference = rangeCoefficient * data.baseStrategies[targetBaseId].defenseDemand;
			
			// update best
			
			if (targetBasePreference > bestTargetBasePreference)
			{
				bestTargetBaseId = targetBaseId;
				bestTargetBasePreference = targetBasePreference;
			}
			
		}
		
		data.baseStrategies[baseId].targetBaseId = bestTargetBaseId;
		
		debug("\t\t%-25s -> %-25s\n", Bases[baseId].name, (bestTargetBaseId == -1 ? "" : Bases[bestTargetBaseId].name));
		
	}
	
	debug("\n");
	
}

/*
Returns combat bonus multiplier for specific units.
Conventional combat only.

ABL_DISSOCIATIVE_WAVE cancels following abilities:
ABL_EMPATH
ABL_TRANCE
ABL_COMM_JAMMER
ABL_AAA
ABL_SOPORIFIC_GAS
*/
double getConventionalCombatBonusMultiplier(int attackerUnitId, int defenderUnitId)
{
	UNIT *attackerUnit = &(Units[attackerUnitId]);
	UNIT *defenderUnit = &(Units[defenderUnitId]);
	
	// do not modify psi combat
	
	if (isNativeUnit(attackerUnitId) || isNativeUnit(defenderUnitId))
		return 1.0;
	
	// conventional combat
	
	double combatBonusMultiplier = 1.0;
	
	// fast unit without blink displacer against comm jammer
	
	if
	(
		unit_has_ability(defenderUnitId, ABL_COMM_JAMMER)
		&&
		attackerUnit->triad() == TRIAD_LAND && unit_chassis_speed(attackerUnitId) > 1
		&&
		!unit_has_ability(attackerUnitId, ABL_DISSOCIATIVE_WAVE)
	)
	{
		combatBonusMultiplier /= getPercentageBonusMultiplier(Rules->combat_comm_jammer_vs_mobile);
	}
	
	// air unit without blink displacer against air tracking
	
	if
	(
		unit_has_ability(defenderUnitId, ABL_AAA)
		&&
		attackerUnit->triad() == TRIAD_AIR
		&&
		!unit_has_ability(attackerUnitId, ABL_DISSOCIATIVE_WAVE)
	)
	{
		combatBonusMultiplier /= getPercentageBonusMultiplier(Rules->combat_AAA_bonus_vs_air);
	}
	
	// soporific unit against conventional unit without blink displacer
	
	if
	(
		unit_has_ability(attackerUnitId, ABL_SOPORIFIC_GAS)
		&&
		!isNativeUnit(defenderUnitId)
		&&
		!unit_has_ability(defenderUnitId, ABL_DISSOCIATIVE_WAVE)
	)
	{
		combatBonusMultiplier *= 1.25;
	}
	
	// interceptor ground strike
	
	if
	(
		attackerUnit->triad() == TRIAD_AIR && unit_has_ability(attackerUnitId, ABL_AIR_SUPERIORITY)
		&&
		defenderUnit->triad() != TRIAD_AIR
	)
	{
		combatBonusMultiplier *= getPercentageBonusMultiplier(-Rules->combat_penalty_air_supr_vs_ground);
	}
	
	// interceptor air-to-air
		
	if
	(
		attackerUnit->triad() == TRIAD_AIR && unit_has_ability(attackerUnitId, ABL_AIR_SUPERIORITY)
		&&
		defenderUnit->triad() == TRIAD_AIR
	)
	{
		combatBonusMultiplier *= getPercentageBonusMultiplier(Rules->combat_bonus_air_supr_vs_air);
	}
	
	// gas
	
	if
	(
		unit_has_ability(attackerUnitId, ABL_NERVE_GAS)
	)
	{
		combatBonusMultiplier *= 1.5;
	}
	
	return combatBonusMultiplier;
	
}

std::string getAbilitiesString(int ability_flags)
{
	std::string abilitiesString;
	
	if ((ability_flags & ABL_AMPHIBIOUS) != 0)
	{
		abilitiesString += " AMPHIBIOUS";
	}
	
	if ((ability_flags & ABL_AIR_SUPERIORITY) != 0)
	{
		abilitiesString += " AIR_SUPERIORITY";
	}
	
	if ((ability_flags & ABL_AAA) != 0)
	{
		abilitiesString += " AAA";
	}
	
	if ((ability_flags & ABL_COMM_JAMMER) != 0)
	{
		abilitiesString += " ECM";
	}
	
	if ((ability_flags & ABL_EMPATH) != 0)
	{
		abilitiesString += " EMPATH";
	}
	
	if ((ability_flags & ABL_ARTILLERY) != 0)
	{
		abilitiesString += " ARTILLERY";
	}
	
	if ((ability_flags & ABL_BLINK_DISPLACER) != 0)
	{
		abilitiesString += " BLINK_DISPLACER";
	}
	
	if ((ability_flags & ABL_TRANCE) != 0)
	{
		abilitiesString += " TRANCE";
	}
	
	if ((ability_flags & ABL_NERVE_GAS) != 0)
	{
		abilitiesString += " NERVE_GAS";
	}
	
	if ((ability_flags & ABL_SOPORIFIC_GAS) != 0)
	{
		abilitiesString += " SOPORIFIC_GAS";
	}
	
	if ((ability_flags & ABL_DISSOCIATIVE_WAVE) != 0)
	{
		abilitiesString += " DISSOCIATIVE_WAVE";
	}
	
	return abilitiesString;
	
}

bool isWithinAlienArtilleryRange(int vehicleId)
{
	VEH *vehicle = &(Vehicles[vehicleId]);
	
	for (int otherVehicleId = 0; otherVehicleId < *total_num_vehicles; otherVehicleId++)
	{
		VEH *otherVehicle = &(Vehicles[otherVehicleId]);
		
		if (otherVehicle->faction_id == 0 && otherVehicle->unit_id == BSC_SPORE_LAUNCHER && map_range(vehicle->x, vehicle->y, otherVehicle->x, otherVehicle->y) <= 2)
			return true;
		
	}
	
	return false;
	
}

/*
Checks if faction is enabled to use WTP algorithms.
*/
bool isUseWtpAlgorithms(int factionId)
{
	return (factionId != 0 && !is_human(factionId) && ai_enabled(factionId) && conf.ai_useWTPAlgorithms);
}

int getCoastalBaseOceanAssociation(MAP *tile, int factionId)
{
	int coastalBaseOceanAssociation = -1;
	
	if (data.geography.faction[factionId].coastalBaseOceanAssociations.count(tile) != 0)
	{
		coastalBaseOceanAssociation = data.geography.faction[factionId].coastalBaseOceanAssociations.at(tile);
	}
	
	return coastalBaseOceanAssociation;
	
}

int getAssociation(MAP *tile, int factionId)
{
	return getAssociation(getExtendedRegion(tile), factionId);
}

int getAssociation(int region, int factionId)
{
	int association;
	
	if (data.geography.faction[factionId].associations.count(region) != 0)
	{
		association = data.geography.faction[factionId].associations.at(region);
	}
	else
	{
		association = data.geography.associations.at(region);
	}
	
	return association;
	
}

std::unordered_set<int> *getConnections(MAP *tile, int factionId)
{
	return getConnections(getExtendedRegion(tile), factionId);
}
	
std::unordered_set<int> *getConnections(int region, int factionId)
{
	std::unordered_set<int> *connections;
	
	if (data.geography.faction[factionId].connections.count(region) != 0)
	{
		connections = &(data.geography.faction[factionId].connections.at(region));
	}
	else
	{
		connections = &(data.geography.connections.at(region));
	}
	
	return connections;
	
}

bool isSameAssociation(MAP *tile1, MAP *tile2, int factionId)
{
	return getAssociation(tile1, factionId) == getAssociation(tile2, factionId);
}

bool isSameAssociation(int association1, MAP *tile2, int factionId)
{
	return association1 == getAssociation(tile2, factionId);
}

/*
Returns region for all map tiles.
Returns shifted map index for polar regions.
*/
int getExtendedRegion(MAP *tile)
{
	if (tile->region == 0x3f || tile->region == 0x7f)
	{
		return 0x80 + getMapIndex(tile);
	}
	else
	{
		return tile->region;
	}
	
}

/*
Checks for polar region
*/
bool isPolarRegion(MAP *tile)
{
	return (tile->region == 0x3f || tile->region == 0x7f);
	
}

/*
Returns vehicle region association.
*/
int getVehicleAssociation(int vehicleId)
{
	VEH *vehicle = &(Vehicles[vehicleId]);
	MAP *vehicleTile = getVehicleMapTile(vehicleId);
	
	int vehicleAssociation = -1;
	
	if (vehicle->triad() == TRIAD_LAND)
	{
		if (isLandRegion(vehicleTile->region))
		{
			vehicleAssociation = getAssociation(vehicleTile, vehicle->faction_id);
		}
		
	}
	else if (vehicle->triad() == TRIAD_SEA)
	{
		if (isOceanRegion(vehicleTile->region))
		{
			vehicleAssociation = getAssociation(vehicleTile, vehicle->faction_id);
		}
		else if (map_has_item(vehicleTile, TERRA_BASE_IN_TILE))
		{
			int coastalBaseOceanAssociation = getCoastalBaseOceanAssociation(vehicleTile, vehicle->faction_id);
			
			if (coastalBaseOceanAssociation != -1)
			{
				vehicleAssociation = coastalBaseOceanAssociation;
			}
			
		}
		
	}
	
	return vehicleAssociation;
	
}

/*
Returns location ocean association.
*/
int getOceanAssociation(MAP *tile, int factionId)
{
	int oceanAssociation = -1;
	
	if (isOceanRegion(tile->region))
	{
		oceanAssociation = getAssociation(tile, factionId);
	}
	else if (map_has_item(tile, TERRA_BASE_IN_TILE))
	{
		int coastalBaseOceanAssociation = getCoastalBaseOceanAssociation(tile, factionId);
		
		if (coastalBaseOceanAssociation != -1)
		{
			oceanAssociation = coastalBaseOceanAssociation;
		}
		
	}
	
	return oceanAssociation;

}

bool isOceanAssociationCoast(int x, int y, int oceanAssociation, int factionId)
{
	MAP *tile = getMapTile(x, y);

	if (is_ocean(tile))
		return false;

	for (MAP *adjacentTile : getAdjacentTiles(x, y, false))
	{
		int adjacentTileOceanAssociation = getOceanAssociation(adjacentTile, factionId);
		
		if (adjacentTileOceanAssociation == oceanAssociation)
			return true;
	}

	return false;

}

int getMaxBaseSize(int factionId)
{
	int maxBaseSize = 0;

	for (int baseId = 0; baseId < *total_num_bases; baseId++)
	{
		BASE *base = &(Bases[baseId]);

		if (base->faction_id != factionId)
			continue;

		maxBaseSize = std::max(maxBaseSize, (int)base->pop_size);

	}

	return maxBaseSize;

}

/*
Returns base ocean regions.
*/
std::unordered_set<int> getBaseOceanRegions(int baseId)
{
	BASE *base = &(Bases[baseId]);
	MAP *baseTile = getBaseMapTile(baseId);

	std::unordered_set<int> baseOceanRegions;

	if (is_ocean(baseTile))
	{
		baseOceanRegions.insert(getExtendedRegion(baseTile));
	}
	else
	{
		for (MAP *adjacentTile : getAdjacentTiles(base->x, base->y, false))
		{
			if (!is_ocean(adjacentTile))
				continue;

			baseOceanRegions.insert(getExtendedRegion(adjacentTile));

		}

	}

	return baseOceanRegions;

}

