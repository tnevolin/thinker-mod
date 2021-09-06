#pragma once

#include "main.h"
#include <float.h>
#include <map>
#include <memory>
#include "game.h"
#include "move.h"
#include "wtp.h"
#include "aiMove.h"
#include "aiMoveFormer.h"
#include "aiMoveCombat.h"
#include "aiMoveColony.h"
#include "aiTransport.h"
#include "terranx_enums.h"

const int COMBAT_ABILITY_FLAGS = ABL_AMPHIBIOUS | ABL_AIR_SUPERIORITY | ABL_AAA | ABL_COMM_JAMMER | ABL_EMPATH | ABL_ARTILLERY | ABL_BLINK_DISPLACER | ABL_TRANCE | ABL_NERVE_GAS | ABL_SOPORIFIC_GAS | ABL_DISSOCIATIVE_WAVE;

struct FactionInfo
{
	double offenseMultiplier;
	double defenseMultiplier;
	double fanaticBonusMultiplier;
	double threatCoefficient;
	int bestWeaponOffenseValue;
	int bestArmorDefenseValue;
	std::vector<Location> airbases;
};

struct Threat
{
	double psi;
	double infantry;
	double psiDefense;
	double psiOffense;
	double conventionalDefense;
	double conventionalOffense;
	double artillery;
	double psiDefenseDemand;
	double psiOffenseDemand;
	double conventionalDefenseDemand;
	double conventionalOffenseDemand;
	double artilleryDemand;
};

struct DefenseDemand
{
	double landPsiOffense;
	double landPsiDefense;
	double seaPsiOffense;
	double seaPsiDefense;
	double airPsiOffense;
	double airPsiDefense;
	double conventionalOffense;
	double conventionalDefense;
	double psiDefense;
	double psiOffense;
	double artillery;
	double psiDefenseDemand;
	double psiOffenseDemand;
	double conventionalDefenseDemand;
	double conventionalOffenseDemand;
	double artilleryDemand;
};

struct UnitTypeStrength
{
	double weight = 0.0;
	double psiOffense = 0.0;
	double psiDefense = 0.0;
	double conventionalOffense = 0.0;
	double conventionalDefense = 0.0;
	
	void normalize(double totalWeight)
	{
		this->psiOffense /= this->weight;
		this->psiDefense /= this->weight;
		this->conventionalOffense /= this->weight;
		this->conventionalDefense /= this->weight;
		this->weight /= totalWeight;
	}
	
};

struct MilitaryStrength
{
	double totalWeight = 0.0;
	std::map<int, UnitTypeStrength> unitTypeStrengths;
	
	void normalize()
	{
		for
		(
			std::map<int, UnitTypeStrength>::iterator unitTypeStrengthIterator = this->unitTypeStrengths.begin();
			unitTypeStrengthIterator != this->unitTypeStrengths.end();
			unitTypeStrengthIterator++
		)
		{
			UnitTypeStrength *unitTypeStrength = &(unitTypeStrengthIterator->second);
			unitTypeStrength->normalize(this->totalWeight);
			
		}
		
	}
	
};

struct BaseStrategy
{
	BASE *base;
	std::vector<int> garrison;
	double nativeProtection;
	double nativeThreat;
	double nativeDefenseDemand;
	int unpopulatedTileCount;
	int unpopulatedTileRangeSum;
	double averageUnpopulatedTileRange;
	double sensorOffenseMultiplier;
	double sensorDefenseMultiplier;
	double intrinsicDefenseMultiplier;
	double conventionalDefenseMultipliers[3];
	double exposure;
	bool inSharedOceanRegion;
	double defenseDemand;
	int targetBaseId;
	MilitaryStrength defenderStrength;
	MilitaryStrength opponentStrength;
};

struct ESTIMATED_VALUE
{
	double demanding;
	double remaining;
};

struct VehicleWeight
{
	int id;
	double weight;
};

struct ActiveFactionInfo
{
	FactionInfo factionInfos[8];
	int bestWeaponOffenseValue;
	int bestArmorDefenseValue;
	std::vector<int> baseIds;
	std::unordered_map<MAP *, int> baseLocations;
	std::unordered_set<int> presenceRegions;
	std::unordered_map<int, std::unordered_set<int>> regionBaseIds;
	std::map<int, std::vector<int>> regionBaseGroups;
	std::map<int, BaseStrategy> baseStrategies;
	std::vector<int> vehicleIds;
	std::vector<int> combatVehicleIds;
	std::vector<int> scoutVehicleIds;
	std::vector<int> outsideCombatVehicleIds;
	std::vector<int> unitIds;
	std::vector<int> combatUnitIds;
	std::vector<int> colonyVehicleIds;
	std::vector<int> formerVehicleIds;
	double threatLevel;
	std::unordered_map<int, std::vector<int>> regionSurfaceCombatVehicleIds;
	std::unordered_map<int, std::vector<int>> regionSurfaceScoutVehicleIds;
	std::unordered_map<int, double> baseAnticipatedNativeAttackStrengths;
	std::unordered_map<int, double> baseRemainingNativeProtectionDemands;
	int mostVulnerableBaseId;
	double mostVulnerableBaseDefenseDemand;
	std::unordered_map<int, double> regionDefenseDemand;
	int maxMineralSurplus;
	std::unordered_map<int, int> regionMaxMineralSurpluses;
	int bestLandUnitId;
	int bestSeaUnitId;
	int bestAirUnitId;
	std::vector<int> landCombatUnitIds;
	std::vector<int> seaCombatUnitIds;
	std::vector<int> airCombatUnitIds;
	std::vector<int> landAndAirCombatUnitIds;
	std::vector<int> seaAndAirCombatUnitIds;
	std::unordered_map<int, int> regionAlienHunterDemands;
	std::unordered_map<int, int> seaTransportRequests;
	std::unordered_map<int, std::shared_ptr<Task>> tasks;
	
	void clear()
	{
		baseIds.clear();
		baseLocations.clear();
		presenceRegions.clear();
		regionBaseIds.clear();
		regionBaseGroups.clear();
		baseStrategies.clear();
		vehicleIds.clear();
		combatVehicleIds.clear();
		scoutVehicleIds.clear();
		outsideCombatVehicleIds.clear();
		unitIds.clear();
		combatUnitIds.clear();
		colonyVehicleIds.clear();
		formerVehicleIds.clear();
		threatLevel = 0.0;
		regionSurfaceCombatVehicleIds.clear();
		regionSurfaceScoutVehicleIds.clear();
		baseAnticipatedNativeAttackStrengths.clear();
		baseRemainingNativeProtectionDemands.clear();
		regionDefenseDemand.clear();
		regionMaxMineralSurpluses.clear();
		landCombatUnitIds.clear();
		seaCombatUnitIds.clear();
		airCombatUnitIds.clear();
		landAndAirCombatUnitIds.clear();
		seaAndAirCombatUnitIds.clear();
		regionAlienHunterDemands.clear();
		seaTransportRequests.clear();
		tasks.clear();
	}
	
};

extern int wtpAIFactionId;

extern int currentTurn;
extern int aiFactionId;
extern ActiveFactionInfo activeFactionInfo;

int aiFactionUpkeep(const int factionId);
void aiStrategy();
void populateGlobalVariables();
void setSharedOceanRegions();
VEH *getVehicleByAIId(int aiId);
Location getNearestPodLocation(int vehicleId);
void designUnits();
void proposeMultiplePrototypes(int factionId, std::vector<int> chassisIds, std::vector<int> weaponIds, std::vector<int> armorIds, std::vector<int> abilitiesSets, int reactorId, int plan, char *name);
void checkAndProposePrototype(int factionId, int chassisId, int weaponId, int armorId, int abilities, int reactorId, int plan, char *name);
void evaluateBaseNativeDefenseDemands();
int getNearestFactionBaseRange(int factionId, int x, int y);
int getNearestOtherFactionBaseRange(int factionId, int x, int y);
int getNearestBaseId(int x, int y, std::unordered_set<int> baseIds);
int getNearestBaseRange(int x, int y, std::unordered_set<int> baseIds);
void evaluateBaseExposures();
int getFactionBestPrototypedWeaponOffenseValue(int factionId);
int getFactionBestPrototypedArmorDefenseValue(int factionId);
double evaluateCombatStrength(double offenseValue, double defenseValue);
double evaluateOffenseStrength(double offenseValue);
double evaluateDefenseStrength(double DefenseValue);
//void evaluateBaseDefenseDemands();
//void evaluateLandBaseDefenseDemand(int baseId);
//void evaluateOceanBaseDefenseDemand(int baseId);
bool isVehicleThreatenedByEnemyInField(int vehicleId);
bool isDestinationReachable(int vehicleId, int x, int y, bool accountForSeaTransport);
int getRangeToNearestFactionAirbase(int x, int y, int factionId);
void populateBaseExposures();
int getNearestEnemyBaseDistance(int baseId);
void evaluateDefenseDemand();
int packUnitType(int unitId);
int isUnitTypeNative(int unitType);
int getUnitTypeChassisType(int unitType);
int isUnitTypeHasChassisType(int unitType, int chassisType);
int getUnitTypeTriad(int unitType);
int isUnitTypeHasTriad(int unitType, int triad);
int isUnitTypeHasAbility(int unitType, int abilityFlag);
double getConventionalCombatBonusMultiplier(int attackerUnitType, int defenderUnitType);
std::string getUnitTypeAbilitiesString(int unitType);
bool isWithinAlienArtilleryRange(int vehicleId);
bool isUseWtpAlgorithms(int factionId);

