#include "aiProduction.h"
#include "aiCombat.h"
#include "ai.h"
#include "engine.h"

std::map<int, double> unitTypeProductionDemands;
std::set<int> bestProductionBaseIds;
std::map<int, std::map<int, double>> unitDemands;
std::map<int, PRODUCTION_DEMAND> productionDemands;
std::map<int, PRODUCTION_CHOICE> productionChoices;
double averageFacilityMoraleBoost;
double territoryBonusMultiplier;

/*
Prepares production orders.
*/
void aiProductionStrategy()
{
	// populate lists

	populateProducitonLists();

	// calculate native protection demand

	evaluateNativeProtectionDemand();

//	// calculate conventional defense demand
//
//	evaluateFactionProtectionDemand();
//
	// calculate expansion demand

	calculateExpansionDemand();

	// sort production demands

	setProductionChoices();

}

void populateProducitonLists()
{
	unitTypeProductionDemands.clear();
	bestProductionBaseIds.clear();
	unitDemands.clear();
	productionDemands.clear();
	productionChoices.clear();

	// calculate average base production

	double totalBaseProduction = 0.0;

	for (int id : baseIds)
	{
		BASE *base = &(tx_bases[id]);

		totalBaseProduction += (double)base->mineral_surplus_final;

	}

	double averageBaseProduction = totalBaseProduction / (double)baseIds.size();

	// populate best production bases

	double sumMoraleFacilityBoost = 0.0;

	for (int id : baseIds)
	{
		BASE *base = &(tx_bases[id]);

		if (base->mineral_surplus_final >= averageBaseProduction)
		{
			// add best production base

			bestProductionBaseIds.insert(id);

			// summarize morale facility boost

			if (has_facility(id, FAC_COMMAND_CENTER) || has_facility(id, FAC_NAVAL_YARD) || has_facility(id, FAC_AEROSPACE_COMPLEX))
			{
				sumMoraleFacilityBoost += 2;
			}

			if (has_facility(id, FAC_BIOENHANCEMENT_CENTER))
			{
				sumMoraleFacilityBoost += 2;
			}

		}

	}

	averageFacilityMoraleBoost = sumMoraleFacilityBoost / bestProductionBaseIds.size();

	// calculate territory bonus multiplier

	territoryBonusMultiplier = (1.0 + (double)conf.combat_bonus_territory / 100.0);

}

/*
Calculates need for bases protection against natives.
*/
void evaluateNativeProtectionDemand()
{
	for (int id : baseIds)
	{
		BASE *base = &(tx_bases[id]);
		BASE_STRATEGY *baseStrategy = &(baseStrategies[id]);

		// calculate current native protection

		double totalNativeProtection = 0.0;

		for (int vehicleId : baseStrategy->garrison)
		{
			totalNativeProtection += estimateVehicleBaseLandNativeProtection(base->faction_id, vehicleId);
		}

		// add currently built purely defensive vehicle to protection

		if (base->queue_items[0] >= 0 && isCombatVehicle(base->queue_items[0]) && tx_weapon[tx_units[base->queue_items[0]].weapon_type].offense_value == 1)
		{
			totalNativeProtection += estimateUnitBaseLandNativeProtection(base->faction_id, base->queue_items[0]);
		}

		// add protection demand

		int item = findStrongestNativeDefensePrototype(base->faction_id);

		if (totalNativeProtection < conf.ai_production_min_native_protection)
		{
			addProductionDemand(id, true, item, 1.0);
		}
		else if (totalNativeProtection < conf.ai_production_max_native_protection)
		{
			double protectionPriority = conf.ai_production_native_protection_priority_multiplier * (conf.ai_production_max_native_protection - totalNativeProtection) / (conf.ai_production_max_native_protection - conf.ai_production_min_native_protection);
			addProductionDemand(id, false, item, protectionPriority);
		}

	}

}

/*
Calculate demand for protection against other factions.
*/
void evaluateFactionProtectionDemand()
{
	debug("%-24s evaluateFactionProtectionDemand\n", tx_metafactions[aiFactionId].noun_faction);

	// hostile conventional offense

	debug("\tconventional offense\n");

	double hostileConventionalToConventionalOffenseStrength = 0.0;
	double hostileConventionalToPsiOffenseStrength = 0.0;

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(tx_vehicles[id]);
		MAP *location = getMapTile(vehicle->x, vehicle->y);
		int triad = veh_triad(id);

		// exclude own vehicles

		if (vehicle->faction_id == aiFactionId)
			continue;

		// exclude natives

		if (vehicle->faction_id == 0)
			continue;

		// exclude non combat

		if (!isCombatVehicle(id))
			continue;

		// find vehicle weapon offense and defense values

		int vehicleOffenseValue = getVehicleOffenseValue(id);
		int vehicleDefenseValue = getVehicleDefenseValue(id);

		// only conventional offense

		if (vehicleOffenseValue == -1)
			continue;

		// exclude conventional vehicle with too low attack/defense ratio

		if (vehicleOffenseValue != -1 && vehicleDefenseValue != -1 && vehicleOffenseValue < vehicleDefenseValue / 2)
			continue;

		// get morale modifier for attack

		double moraleModifierAttack = getMoraleModifierAttack(id);

		// get PLANET bonus on attack

		double planetModifierAttack = getSEPlanetModifierAttack(vehicle->faction_id);

		// get vehicle region

		int region = (triad == TRIAD_AIR ? -1 : location->region);

		// find nearest own base

		int nearestOwnBaseId = findNearestOwnBaseId(vehicle->x, vehicle->y, region);

		// base not found

		if (nearestOwnBaseId == -1)
			continue;

		// find range to nearest own base

		BASE *nearestOwnBase = &(tx_bases[nearestOwnBaseId]);
		int nearestOwnBaseRange = map_range(vehicle->x, vehicle->y, nearestOwnBase->x, nearestOwnBase->y);

		// calculate vehicle speed
		// for land units assuming there are roads everywhere

		double vehicleSpeed = (triad == TRIAD_LAND ? getLandVehicleSpeedOnRoads(id) : getVehicleSpeedWithoutRoads(id));

		// calculate threat time koefficient

		double timeThreatKoefficient = max(0.0, (MAX_THREAT_TURNS - ((double)nearestOwnBaseRange / vehicleSpeed)) / MAX_THREAT_TURNS);

		// calculate conventional anti conventional defense demand

		double conventionalCombatStrength =
			(double)vehicleOffenseValue
			* factionInfos[vehicle->faction_id].offenseMultiplier
			* factionInfos[vehicle->faction_id].fanaticBonusMultiplier
			* moraleModifierAttack
			* factionInfos[vehicle->faction_id].threatKoefficient
			/ getBaseDefenseMultiplier(nearestOwnBaseId, triad, true, true)
			* timeThreatKoefficient
		;

		// calculate psi anti conventional defense demand

		double psiCombatStrength =
			getPsiCombatBaseOdds(triad)
			* factionInfos[vehicle->faction_id].offenseMultiplier
			* moraleModifierAttack
			* planetModifierAttack
			* factionInfos[vehicle->faction_id].threatKoefficient
			/ getBaseDefenseMultiplier(nearestOwnBaseId, triad, false, true)
			* timeThreatKoefficient
		;

		debug
		(
			"\t\t[%3d](%3d,%3d), %-25s\n",
			id,
			vehicle->x,
			vehicle->y,
			tx_metafactions[vehicle->faction_id].noun_faction
		);
		debug
		(
			"\t\t\tvehicleOffenseValue=%2d, offenseMultiplier=%4.1f, fanaticBonusMultiplier=%4.1f, moraleModifierAttack=%4.1f, threatKoefficient=%4.1f, nearestOwnBaseRange=%3d, baseDefenseMultiplier=%4.1f, timeThreatKoefficient=%4.2f, conventionalCombatStrength=%4.1f\n",
			vehicleOffenseValue,
			factionInfos[vehicle->faction_id].offenseMultiplier,
			factionInfos[vehicle->faction_id].fanaticBonusMultiplier,
			moraleModifierAttack,
			factionInfos[vehicle->faction_id].threatKoefficient,
			nearestOwnBaseRange,
			getBaseDefenseMultiplier(nearestOwnBaseId, triad, true, true),
			timeThreatKoefficient,
			conventionalCombatStrength
		);
		debug
		(
			"\t\t\tpsiCombatBaseOdds=%4.1f, offenseMultiplier=%4.1f, moraleModifierAttack=%4.1f, planetModifierAttack=%4.1f, threatKoefficient=%4.1f, nearestOwnBaseRange=%3d, baseDefenseMultiplier=%4.1f, timeThreatKoefficient=%4.2f, psiCombatStrength=%4.1f\n",
			getPsiCombatBaseOdds(triad),
			factionInfos[vehicle->faction_id].offenseMultiplier,
			moraleModifierAttack,
			planetModifierAttack,
			factionInfos[vehicle->faction_id].threatKoefficient,
			nearestOwnBaseRange,
			getBaseDefenseMultiplier(nearestOwnBaseId, triad, false, true),
			timeThreatKoefficient,
			psiCombatStrength
		);

		// update total

		hostileConventionalToConventionalOffenseStrength += conventionalCombatStrength;
		hostileConventionalToPsiOffenseStrength += psiCombatStrength;

	}

	debug
	(
		"\thostileConventionalToConventionalOffenseStrength=%.0f, hostileConventionalToPsiOffenseStrength=%.0f\n",
		hostileConventionalToConventionalOffenseStrength,
		hostileConventionalToPsiOffenseStrength
	);

	// own defense strength

	debug("\tanti conventional defense\n");

	double ownConventionalToConventionalDefenseStrength = 0.0;
	double ownConventionalToPsiDefenseStrength = 0.0;

	for (int id : combatVehicleIds)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		// find vehicle defense and offense values

		int vehicleDefenseValue = getVehicleDefenseValue(id);

		// conventional defense

		if (vehicleDefenseValue == -1)
			continue;

		// get vehicle morale multiplier

		double moraleModifierDefense = getMoraleModifierDefense(id);

		// calculate vehicle defense strength

		double vehicleDefenseStrength =
			(double)vehicleDefenseValue
			* factionInfos[*active_faction].defenseMultiplier
			* moraleModifierDefense
			* territoryBonusMultiplier
		;

		debug
		(
			"\t\t[%3d](%3d,%3d) vehicleDefenseValue=%2d, defenseMultiplier=%4.1f, moraleModifierDefense=%4.1f, vehicleDefenseStrength=%4.1f\n",
			id,
			vehicle->x,
			vehicle->y,
			vehicleDefenseValue,
			factionInfos[vehicle->faction_id].defenseMultiplier,
			moraleModifierDefense,
			vehicleDefenseStrength
		);

		// update total

		ownConventionalToConventionalDefenseStrength += vehicleDefenseStrength;

	}

	for (int id : combatVehicleIds)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		// find vehicle defense and offense values

		int vehicleDefenseValue = getVehicleDefenseValue(id);

		// psi defense only

		if (vehicleDefenseValue != -1)
			continue;

		// get vehicle psi combat defense strength

		double psiDefenseStrength = getVehiclePsiDefenseStrength(id);

		// calculate vehicle defense strength

		double vehicleDefenseStrength =
			psiDefenseStrength
			* factionInfos[*active_faction].defenseMultiplier
			* territoryBonusMultiplier
		;

		debug
		(
			"\t\t[%3d](%3d,%3d) psiDefenseStrength=%4.1f, defenseMultiplier=%4.1f, territoryBonusMultiplier=%4.1f, vehicleDefenseStrength=%4.1f\n",
			id,
			vehicle->x,
			vehicle->y,
			psiDefenseStrength,
			factionInfos[vehicle->faction_id].defenseMultiplier,
			territoryBonusMultiplier,
			vehicleDefenseStrength
		);

		// update total

		ownConventionalToPsiDefenseStrength += vehicleDefenseStrength;

	}

	debug
	(
		"\townConventionalToConventionalDefenseStrength=%.0f, ownConventionalToPsiDefenseStrength=%.0f\n",
		ownConventionalToConventionalDefenseStrength,
		ownConventionalToPsiDefenseStrength
	);

	// we have enough protection

	if
	(
		(ownConventionalToConventionalDefenseStrength >= hostileConventionalToConventionalOffenseStrength)
		||
		(ownConventionalToPsiDefenseStrength >= hostileConventionalToPsiOffenseStrength)
	)
	{
		return;
	}

//	// get defenders
//
//	int conventionalDefenderUnitId = findDefenderPrototype();
//	int defenderUnitCost = tx_units[defenderUnitId].cost;
//
//	int defenderUnitDefenseValue = getUnitDefenseValue(defenderUnitId);
//	double defenderUnitMoraleModifierDefense = getNewVehicleMoraleModifierDefense(*active_faction, averageFacilityMoraleBoost);
//	double defenderUnitStrength =
//		(double)defenderUnitDefenseValue
//		* factionInfos[*active_faction].defenseMultiplier
//		* defenderUnitMoraleModifierDefense
//		* territoryBonusMultiplier
//	;
//
//
//
//	// set defensive unit production demand
//
//	if (ownConventionalDefenseStrength < hostileConventionalOffenseStrength)
//	{
//		double priority = (hostileConventionalOffenseStrength - ownConventionalDefenseStrength) / ownConventionalDefenseStrength;
//		int item = findDefenderPrototype();
//
//		for (int id : bestProductionBaseIds)
//		{
//			addProductionDemand(id, true, item, priority);
//		}
//
//	}
//
	debug("\n");

}

/*
Calculates need for colonies.
*/
void calculateExpansionDemand()
{
	// calculate unpopulated tiles per region

	std::map<int, int> unpopulatedTileCounts;

	for (int x = 0; x < *map_axis_x; x++)
	{
		for (int y = 0; y < *map_axis_y; y++)
		{
			MAP *tile = getMapTile(x, y);

			if (tile == NULL)
				continue;

			// exclude territory claimed by someone else

			if (!(tile->owner == -1 || tile->owner == *active_faction))
				continue;

			// exclude base and base radius

			if (tile->items & (TERRA_BASE_IN_TILE | TERRA_BASE_RADIUS))
				continue;

			// inhabited regions only

			if (regionBases.find(tile->region) == regionBases.end())
				continue;

			// get region bases

			std::vector<int> *bases = &(regionBases[tile->region]);

			// calculate distance to nearest own base within region

			BASE *nearestBase = NULL;
			int nearestBaseRange = 0;

			for (int id : *bases)
			{
				BASE *base = &(tx_bases[id]);

				// calculate range to tile

				int range = map_range(x, y, base->x, base->y);

				if (nearestBase == NULL || range < nearestBaseRange)
				{
					nearestBase = base;
					nearestBaseRange = range;
				}

			}

			// exclude too far or uncertain tiles

			if (nearestBase == NULL || nearestBaseRange > conf.ai_production_max_unpopulated_range)
				continue;

			// count available tiles

			if (unpopulatedTileCounts.find(tile->region) == unpopulatedTileCounts.end())
			{
				unpopulatedTileCounts[tile->region] = 0;
			}

			unpopulatedTileCounts[tile->region]++;

		}

	}

	if (DEBUG)
	{
		debug("unpopulatedTileCounts\n");

		for
		(
			std::map<int, int>::iterator unpopulatedTileCountsIterator = unpopulatedTileCounts.begin();
			unpopulatedTileCountsIterator != unpopulatedTileCounts.end();
			unpopulatedTileCountsIterator++
		)
		{
			int region = unpopulatedTileCountsIterator->first;
			int unpopulatedTileCount = unpopulatedTileCountsIterator->second;

			debug("\t[%3d] %3d\n", region, unpopulatedTileCount);

		}

		debug("\n");

	}

	// set colony production demand per region

	for
	(
		std::map<int, std::vector<int>>::iterator regionBasesIterator = regionBases.begin();
		regionBasesIterator != regionBases.end();
		regionBasesIterator++
	)
	{
		int region = regionBasesIterator->first;
		std::vector<int> *regionBaseIds = &(regionBasesIterator->second);

		// evaluate need for expansion

		if (unpopulatedTileCounts.find(region) == unpopulatedTileCounts.end())
			continue;

		if (unpopulatedTileCounts[region] < conf.ai_production_min_unpopulated_tiles)
			continue;

		// select item

		int item = (isOceanRegion(region) ? BSC_SEA_ESCAPE_POD : BSC_COLONY_POD);

		for (int id : *regionBaseIds)
		{
			BASE *base = &(tx_bases[id]);

			// do not build colony in base size 1 or base size 2 building colony already

			if (base->pop_size <= 1)
				continue;
			if (base->pop_size <= 2 && base->queue_items[0] >= 0 && isUnitColony(base->queue_items[0]))
				continue;

			if (productionDemands.find(id) == productionDemands.end())
			{
				productionDemands[id] = PRODUCTION_DEMAND();
			}

			addProductionDemand(id, false, item, conf.ai_production_colony_priority);

		}

	}

}

/*
Set base production choices.
*/
void setProductionChoices()
{
	for
	(
		std::map<int, PRODUCTION_DEMAND>::iterator productionDemandsIterator = productionDemands.begin();
		productionDemandsIterator != productionDemands.end();
		productionDemandsIterator++
	)
	{
		int id = productionDemandsIterator->first;
		PRODUCTION_DEMAND *baseProductionDemands = &(productionDemandsIterator->second);

		// immediate priorities

		if (baseProductionDemands->immediate)
		{
			// create immediate production choice

			productionChoices[id] = {baseProductionDemands->immediateItem, true};

		}

		// not immediate priorities

		else if (baseProductionDemands->regular.size() > 0)
		{
			std::vector<PRODUCTION_PRIORITY> *regularBaseProductionPriorities = &(baseProductionDemands->regular);

			// summarize priorities

			double sumPriorities = 0.0;

			for
			(
				std::vector<PRODUCTION_PRIORITY>::iterator regularBaseProductionPrioritiesIterator = regularBaseProductionPriorities->begin();
				regularBaseProductionPrioritiesIterator != regularBaseProductionPriorities->end();
				regularBaseProductionPrioritiesIterator++
			)
			{
				PRODUCTION_PRIORITY *productionPriority = &(*regularBaseProductionPrioritiesIterator);

				sumPriorities += productionPriority->priority;

			}

			// normalize priorities

			if (sumPriorities > 1.0)
			for
			(
				std::vector<PRODUCTION_PRIORITY>::iterator regularBaseProductionPrioritiesIterator = regularBaseProductionPriorities->begin();
				regularBaseProductionPrioritiesIterator != regularBaseProductionPriorities->end();
				regularBaseProductionPrioritiesIterator++
			)
			{
				PRODUCTION_PRIORITY *productionPriority = &(*regularBaseProductionPrioritiesIterator);

				productionPriority->priority /= sumPriorities;

			}

			// random roll

			double randomRoll = (double)rand() / (double)(RAND_MAX + 1);

			// choose item

			for
			(
				std::vector<PRODUCTION_PRIORITY>::iterator regularBaseProductionPrioritiesIterator = regularBaseProductionPriorities->begin();
				regularBaseProductionPrioritiesIterator != regularBaseProductionPriorities->end();
				regularBaseProductionPrioritiesIterator++
			)
			{
				PRODUCTION_PRIORITY *productionPriority = &(*regularBaseProductionPrioritiesIterator);

				if (randomRoll < productionPriority->priority)
				{
					// create non-immediate production choice

					productionChoices[id] = {productionPriority->item, false};

					break;

				}
				else
				{
					randomRoll -= productionPriority->priority;
				}

			}

		}

	}

	if (DEBUG)
	{
		debug("productionDemands:\n");

		for
		(
			std::map<int, PRODUCTION_DEMAND>::iterator productionDemandsIterator = productionDemands.begin();
			productionDemandsIterator != productionDemands.end();
			productionDemandsIterator++
		)
		{
			int id = productionDemandsIterator->first;
			PRODUCTION_DEMAND *baseProductionDemands = &(productionDemandsIterator->second);

			debug("\t%-25s\n", tx_bases[id].name);

			debug("\t\timmediate\n");

			if (baseProductionDemands->immediate)
			{
				debug("\t\t\t%-25s %4.2f\n", prod_name(baseProductionDemands->immediateItem), baseProductionDemands->immediatePriority);

			}

			debug("\t\tregular\n");

			for
			(
				std::vector<PRODUCTION_PRIORITY>::iterator immediateBaseProductionPrioritiesIterator = baseProductionDemands->regular.begin();
				immediateBaseProductionPrioritiesIterator != baseProductionDemands->regular.end();
				immediateBaseProductionPrioritiesIterator++
			)
			{
				PRODUCTION_PRIORITY *productionPriority = &(*immediateBaseProductionPrioritiesIterator);

				debug("\t\t\t%-25s %4.2f\n", prod_name(productionPriority->item), productionPriority->priority);

			}

			debug("\t\tselected\n");

			if (productionChoices.find(id) != productionChoices.end())
			{
				debug("\t\t\t%-25s %s\n", prod_name(productionChoices[id].item), (productionChoices[id].immediate ? "immediate" : "regular"));
			}

		}

		debug("\n");

	}

}

int findNearestOwnBaseId(int x, int y, int region)
{
	int nearestOwnBaseId = -1;
	int nearestOwnBaseRange = 9999;

	for (int id : baseIds)
	{
		BASE *base = &(tx_bases[id]);
		MAP *location = getMapTile(base->x, base->y);

		// skip bases in other region

		if (region != -1 && location->region != region)
			continue;

		// calculate range

		int range = map_range(base->x, base->y, x, y);

		// update closest base

		if (nearestOwnBaseId == -1 || range < nearestOwnBaseRange)
		{
			nearestOwnBaseId = id;
			nearestOwnBaseRange = range;
		}

	}

	return nearestOwnBaseId;

}

/*
Selects base production.
*/
int suggestBaseProduction(int id, bool productionDone, int choice)
{
	BASE *base = &(tx_bases[id]);

	debug("suggestBaseProduction: %-25s, productionDone=%s, (%s)\n", base->name, (productionDone ? "y" : "n"), prod_name(base->queue_items[0]));

	std::map<int, PRODUCTION_CHOICE>::iterator productionChoicesIterator = productionChoices.find(id);

	if (productionChoicesIterator != productionChoices.end())
	{
		PRODUCTION_CHOICE *productionChoice = &(productionChoicesIterator->second);

		// determine if production change is allowed

		if (productionChoice->immediate || productionDone)
		{
			debug("\t%-25s\n", prod_name(productionChoice->item));

			choice = productionChoice->item;

		}

	}

	debug("\n");

	return choice;

}

void addProductionDemand(int id, bool immediate, int item, double priority)
{
	// create entry if not created yet

	if (productionDemands.find(id) == productionDemands.end())
	{
		productionDemands[id] = PRODUCTION_DEMAND();
	}

	PRODUCTION_DEMAND *productionDemand = &(productionDemands[id]);

	if (immediate)
	{
		if (!productionDemand->immediate)
		{
			productionDemand->immediate = true;
			productionDemand->immediateItem = item;
			productionDemand->immediatePriority = priority;
		}
		else if (priority > productionDemand->immediatePriority)
		{
			productionDemand->immediateItem = item;
			productionDemand->immediatePriority = priority;
		}
	}
	else
	{
		// lookup for existing item

		bool found = false;

		for
		(
			std::vector<PRODUCTION_PRIORITY>::iterator productionPrioritiesIterator = productionDemand->regular.begin();
			productionPrioritiesIterator != productionDemand->regular.end();
			productionPrioritiesIterator++
		)
		{
			PRODUCTION_PRIORITY *productionPriority = &(*productionPrioritiesIterator);

			if (productionPriority->item == item)
			{
				productionPriority->priority = max(productionPriority->priority, priority);
				found = true;
				break;
			}

		}

		if (!found)
		{
			productionDemand->regular.push_back({item, priority});
		}

	}

}

int findStrongestNativeDefensePrototype(int factionId)
{
	// initial prototype

    int bestUnitId = BSC_SCOUT_PATROL;
    UNIT *bestUnit = &(tx_units[bestUnitId]);

    for (int i = 0; i < 128; i++)
	{
        int id = (i < 64 ? i : (factionId - 1) * 64 + i);

        UNIT *unit = &tx_units[id];

		// skip current

		if (id == bestUnitId)
			continue;

        // skip empty

        if (strlen(unit->name) == 0)
			continue;

		// skip not enabled

		if (id < 64 && !has_tech(factionId, unit->preq_tech))
			continue;

		// skip non land

		if (unit_triad(id) != TRIAD_LAND)
			continue;

		// prefer trance or police unit

		if
		(
			((unit->ability_flags & ABL_TRANCE) && (~bestUnit->ability_flags & ABL_TRANCE))
			||
			((unit->ability_flags & ABL_POLICE_2X) && (~bestUnit->ability_flags & ABL_POLICE_2X))
		)
		{
			bestUnitId = id;
			bestUnit = unit;
		}

	}

    return bestUnitId;

}

int findDefenderPrototype()
{
	int bestUnitId = BSC_SCOUT_PATROL;
	double bestUnitDefenseEffectiveness = evaluateUnitDefenseEffectiveness(bestUnitId);

	for (int id : prototypes)
	{
		// skip non combat units

		if (!isCombatUnit(id))
			continue;

		// skip air units

		if (unit_triad(id) == TRIAD_AIR)
			continue;

		double unitDefenseEffectiveness = evaluateUnitDefenseEffectiveness(id);

		if (bestUnitId == -1 || unitDefenseEffectiveness > bestUnitDefenseEffectiveness)
		{
			bestUnitId = id;
			bestUnitDefenseEffectiveness = unitDefenseEffectiveness;
		}

	}

	return bestUnitId;

}

int findFastAttackerUnit()
{
	int bestUnitId = BSC_SCOUT_PATROL;
	double bestUnitOffenseEffectiveness = evaluateUnitOffenseEffectiveness(bestUnitId);

	for (int id : prototypes)
	{
		UNIT *unit = &(tx_units[id]);

		// skip non combat units

		if (!isCombatUnit(id))
			continue;

		// skip infantry units

		if (unit->chassis_type == CHS_INFANTRY)
			continue;

		double unitOffenseEffectiveness = evaluateUnitOffenseEffectiveness(id);

		if (bestUnitId == -1 || unitOffenseEffectiveness > bestUnitOffenseEffectiveness)
		{
			bestUnitId = id;
			bestUnitOffenseEffectiveness = unitOffenseEffectiveness;
		}

	}

	return bestUnitId;

}

