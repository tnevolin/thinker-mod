#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "wtp.h"

/**
Combat calculation placeholder.
All custom combat calculation goes here.
*/
HOOK_API void battle_compute(int attacker_vehicle_id, int defender_vehicle_id, int attacker_strength_pointer, int defender_strength_pointer, int flags)
{
    debug
    (
        "battle_compute(attacker_vehicle_id=%d, defender_vehicle_id=%d, attacker_strength_pointer=%d, defender_strength_pointer=%d, flags=%d)\n",
        attacker_vehicle_id,
        defender_vehicle_id,
        attacker_strength_pointer,
        defender_strength_pointer,
        flags
    )
    ;

    // run original function

    tx_battle_compute(attacker_vehicle_id, defender_vehicle_id, attacker_strength_pointer, defender_strength_pointer, flags);

    debug
    (
        "attacker_strength=%d, defender_strength=%d\n",
        *(int *)attacker_strength_pointer,
        *(int *)defender_strength_pointer
    )
    ;

    // apply_planet_combat_bonus_on_defense

    if (conf.apply_planet_combat_bonus_on_defense)
    {
        // get attacker/defender vehicles

        VEH attacker_vehicle = tx_vehicles[attacker_vehicle_id];
        VEH defender_vehicle = tx_vehicles[defender_vehicle_id];

        // get attacker/defender units

        UNIT attacker_unit = tx_units[attacker_vehicle.proto_id];
        UNIT defender_unit = tx_units[defender_vehicle.proto_id];

        // get attacker/defender weapon/armor id

        R_Weapon attacker_weapon = tx_weapon[attacker_unit.weapon_type];
        R_Defense defender_armor = tx_defense[defender_unit.armor_type];

        // get attacker/defender weapon/armor value

        int attacker_weapon_value = attacker_weapon.offense_value;
        int defender_armor_value = defender_armor.defense_value;

        // check if it is a psi combat

        if (attacker_weapon_value < 0 || defender_armor_value < 0)
        {
            // get defender faction id

            int defender_faction_id = tx_vehicles[defender_vehicle_id].faction_id;

            if (defender_faction_id != 0)
            {
                // get defender planet rating

                int defender_planet_rating = tx_factions[defender_faction_id].SE_planet;

                if (defender_planet_rating != 0)
                {
                    // calculate defender psi combat bonus

                    int defender_psi_combat_bonus = tx_basic->combat_psi_bonus_per_PLANET * defender_planet_rating;

                    // add effect description

                    if (*tx_battle_compute_defender_effect_count < 4)
                    {
                        strcpy((*tx_battle_compute_defender_effect_labels)[*tx_battle_compute_defender_effect_count], tx_label_planet);
                        (*tx_battle_compute_defender_effect_values)[*tx_battle_compute_defender_effect_count] = defender_psi_combat_bonus;

                    }

                    // modify defender strength

                    *(int *)defender_strength_pointer = (int)round((double)(*(int *)defender_strength_pointer) * (100.0 + (double)defender_psi_combat_bonus) / 100.0);

                }

            }

        }

    }

    // adjust summary lines to the bottom

    *tx_battle_compute_attacker_effect_count = 4;
    *tx_battle_compute_defender_effect_count = 4;

}

/**
Composes combat effect value percentage.
Overwrites zero with empty string.
*/
HOOK_API void battle_compute_compose_value_percentage(int output_string_pointer, int input_string_pointer)
{
    debug
    (
        "battle_compute_compose_value_percentage:input(output_string=%s, input_string=%s)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

    // call original function

    tx_strcat(output_string_pointer, input_string_pointer);

    debug
    (
        "battle_compute_compose_value_percentage:output(output_string=%d, input_string=%d)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

    // replace zero with empty string

    if (strcmp((char *)output_string_pointer, " +0%") == 0)
    {
        ((char *)output_string_pointer)[0] = '\x0';

    }

    debug
    (
        "battle_compute_compose_value_percentage:corrected(output_string=%d, input_string=%d)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

}

/**
Prototype cost calculation.

Select primary and secondary weapon/module/armor items
primary item = one with higher true cost
secondary item = one with lower true cost

Calculate unit base cost
unit base cost = primary item cost + secondary item cost / 2 - 1

Multiply by reactor cost factor
reactor cost factor = reactor cost / Fission reactor cost

Multiply by chassis cost factor
chassis cost factor = chassis cost / 2

Multiply by ability factor or add ability flat cost
ability bytes 0-3 is unit cost factor
ability bytes 4-7 is unit cost flat addition

Special rules:
Colony on foil/cruiser costs same as if on infantry/speeder.
Former on foil/cruiser costs same as if on infantry/speeder.
Supply on foil/cruiser costs same as if on infantry/speeder.

*/
HOOK_API int proto_cost(int chassis_id, int weapon_id, int armor_id, int abilities, int reactor_level)
{
    // Special rules

    if
    (
        tx_weapon[weapon_id].mode == WMODE_COLONIST
        ||
        tx_weapon[weapon_id].mode == WMODE_TERRAFORMER
        ||
        tx_weapon[weapon_id].mode == WMODE_CONVOY
    )
    {
        switch(chassis_id)
        {
        case CHS_FOIL:
            chassis_id = CHS_INFANTRY;
            break;

        case CHS_CRUISER:
            chassis_id = CHS_SPEEDER;
            break;

        }

    }

    // get Fission reactor cost factor

    double fission_reactor_cost_factor = (double)conf.reactor_cost_factors[REC_FISSION - 1];

    // get reactor cost factor

    double reactor_cost_factor = (double)conf.reactor_cost_factors[reactor_level - 1];

    // set minimal cost to reactor level (this is checked in some other places so we should do this here to avoid any conflicts)

    int minimal_cost = reactor_level;

    // get pieces cost

    double chassis_cost = (double)tx_chassis[chassis_id].cost;
    double weapon_cost = (double)tx_weapon[weapon_id].cost;
    double armor_cost = (double)tx_defense[armor_id].cost;

    // select primary and secondary item cost

    double primary_item_cost = max(weapon_cost, armor_cost);
    double secondary_item_cost = min(weapon_cost, armor_cost);

    // get abilities cost modifications

    double abilities_cost_factor = 0;
    int abilities_cost_addition = 0;
    for (int ability_id = 0; ability_id < 32; ability_id++)
    {
        if (((abilities >> ability_id) & 0x1) == 0x1)
        {
            int ability_cost = tx_ability[ability_id].cost;

            // take ability cost factor bits: 0-3

            int ability_cost_factor = (ability_cost & 0xF);

            if (ability_cost_factor > 0)
            {
                abilities_cost_factor += (double)ability_cost_factor;

            }

            // take ability cost addition bits: 4-7

            int ability_cost_addition = ((ability_cost >> 4) & 0xF);

            if (ability_cost_addition > 0)
            {
                abilities_cost_addition += ability_cost_addition;

            }

        }

    }

    int cost =
        max
        (
            // minimal cost
            minimal_cost,
            (int)
            round
            (
                (
                    // primary item cost
                    primary_item_cost
                    +
                    // secondary item cost scaled and halved
                    (secondary_item_cost - 1.0) / 2.0
                )
                *
                // chassis cost factor
                (chassis_cost / 2.0)
                *
                // abilities cost factor
                (1.0 + 0.25 * abilities_cost_factor)
                *
                // reactor cost factor
                (reactor_cost_factor / fission_reactor_cost_factor)
            )
            +
            // abilities flat cost
            abilities_cost_addition
        )
    ;

    return cost;

}

HOOK_API int upgrade_cost(int faction_id, int new_unit_id, int old_unit_id)
{
    // get old unit cost

    int old_unit_cost = tx_units[old_unit_id].cost;

    // get new unit cost

    int new_unit_cost = tx_units[new_unit_id].cost;

    // double new unit cost if not prototyped

    if ((tx_units[new_unit_id].unit_flags & 0x4) == 0)
    {
        new_unit_cost *= 2;
    }

    // calculate cost difference

    int cost_difference = new_unit_cost - old_unit_cost;

    // calculate base upgrade cost
    // x4 if positive
    // x2 if negative

    int upgrade_cost = cost_difference * (cost_difference >= 0 ? 4 : 2);

    // halve upgrade cost if positive and faction possesses The Nano Factory

    if (upgrade_cost > 0)
    {
        // get The Nano Factory base id

        int nano_factory_base_id = tx_secret_projects[0x16];

        // get The Nano Factory faction id

        if (nano_factory_base_id >= 0)
        {
            if (tx_bases[nano_factory_base_id].faction_id == faction_id)
            {
                upgrade_cost /= 2;

            }

        }

    }

    return upgrade_cost;

}

/**
Multipurpose combat roll function.
Determines which side wins this round. Returns 1 for attacker, 0 for defender.
*/
HOOK_API int combat_roll
(
    int attacker_strength,
    int defender_strength,
    int attacker_vehicle_offset,
    int defender_vehicle_offset,
    int attacker_initial_power,
    int defender_initial_power,
    int *attacker_won_last_round
)
{
    debug("combat_roll(attacker_strength=%d, defender_strength=%d, attacker_vehicle_offset=%d, defender_vehicle_offset=%d, attacker_initial_power=%d, defender_initial_power=%d, *attacker_won_last_round=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_vehicle_offset,
        defender_vehicle_offset,
        attacker_initial_power,
        defender_initial_power,
        *attacker_won_last_round
    )
    ;

    // normalize strength values

    if (attacker_strength < 1) attacker_strength = 1;
    if (defender_strength < 1) defender_strength = 1;

    // calculate outcome

    int attacker_wins;

    if (!conf.alternative_combat_mechanics)
    {
        attacker_wins =
            standard_combat_mechanics_combat_roll
            (
                attacker_strength,
                defender_strength
            )
        ;

    }
    else
    {
        attacker_wins =
            alternative_combat_mechanics_combat_roll
            (
                attacker_strength,
                defender_strength,
                attacker_vehicle_offset,
                defender_vehicle_offset,
                attacker_initial_power,
                defender_initial_power,
                attacker_won_last_round
            )
        ;

    }

    // return outcome

    return attacker_wins;

}

/**
Standard combat mechanics.
Determines which side wins this round. Returns 1 for attacker, 0 for defender.
*/
int standard_combat_mechanics_combat_roll
(
    int attacker_strength,
    int defender_strength
)
{
    debug("standard_combat_mechanics_combat_roll(attacker_strength=%d, defender_strength=%d)\n",
        attacker_strength,
        defender_strength
    )
    ;

    // generate random roll

    int random_roll = random(attacker_strength + defender_strength);
    debug("\trandom_roll=%d\n", random_roll);

    // calculate outcome

    int attacker_wins = (random_roll < attacker_strength ? 1 : 0);
    debug("\tattacker_wins=%d\n", attacker_wins);

    return attacker_wins;

}

/**
Alternative combat mechanics.
Determines which side wins this round. Returns 1 for attacker, 0 for defender.
*/
int alternative_combat_mechanics_combat_roll
(
    int attacker_strength,
    int defender_strength,
    int attacker_vehicle_offset,
    int defender_vehicle_offset,
    int attacker_initial_power,
    int defender_initial_power,
    int *attacker_won_last_round
)
{
    debug("alternative_combat_mechanics_combat_roll(attacker_strength=%d, defender_strength=%d, attacker_initial_power=%d, defender_initial_power=%d, *attacker_won_last_round=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_initial_power,
        defender_initial_power,
        *attacker_won_last_round
    )
    ;

    // calculate probabilities

    double p, q, pA, qA, pD, qD;

    alternative_combat_mechanics_probabilities
    (
        attacker_strength,
        defender_strength,
        &p, &q, &pA, &qA, &pD, &qD
    )
    ;

    // determine whether we are on first round

    VEH attacker_vehicle = tx_vehicles[attacker_vehicle_offset / 0x34];
    VEH defender_vehicle = tx_vehicles[defender_vehicle_offset / 0x34];

    UNIT attacker_unit = tx_units[attacker_vehicle.proto_id];
    UNIT defender_unit = tx_units[defender_vehicle.proto_id];

    int attacker_power = attacker_unit.reactor_type * 0xA - attacker_vehicle.damage_taken;
    int defender_power = defender_unit.reactor_type * 0xA - defender_vehicle.damage_taken;

    bool first_round = (attacker_power == attacker_initial_power) && (defender_power == defender_initial_power);
    debug("\tfirst_round=%d\n", (first_round ? 1 : 0));

    // determine effective p

    double effective_p = (first_round ? p : (*attacker_won_last_round == 1 ? pA : pD));
    debug("\teffective_p=%f\n", effective_p);

    // generate random roll

    double random_roll = (double)rand() / (double)RAND_MAX;
    debug("\trandom_roll=%f\n", random_roll);

    // calculate outcome

    int attacker_wins = (random_roll < effective_p ? 1 : 0);
    debug("\tattacker_wins=%d\n", attacker_wins);

    // set round winner

    *attacker_won_last_round = attacker_wins;

    return attacker_wins;

}

/**
Alternative combat mechanics.
Calculates probabilities.
*/
void alternative_combat_mechanics_probabilities
(
    int attacker_strength,
    int defender_strength,
    double *p, double *q, double *pA, double *qA, double *pD, double *qD
)
{
    // determine first round probability

    *p = (double)attacker_strength / ((double)attacker_strength + (double)defender_strength);
    *q = 1 - *p;

    // determine following rounds probabilities

    *qA = *q / conf.alternative_combat_mechanics_loss_divider;
    *pA = 1 - *qA;
    *pD = *p / conf.alternative_combat_mechanics_loss_divider;
    *qD = 1 - *pD;

}

/**
Calculates odds.
*/
HOOK_API void calculate_odds
(
    int attacker_vehicle_offset,
    int defender_vehicle_offset,
    int attacker_strength,
    int defender_strength,
    int *attacker_odd,
    int *defender_odd
)
{
    debug("calculate_odds(attacker_vehicle_offset=%d, defender_vehicle_offset=%d, attacker_strength=%d, defender_strength=%d)\n",
        attacker_vehicle_offset,
        defender_vehicle_offset,
        attacker_strength,
        defender_strength
    )
    ;

    // get attacker and defender vehicles

    VEH attacker_vehicle = tx_vehicles[attacker_vehicle_offset / sizeof(VEH)];
    VEH defender_vehicle = tx_vehicles[defender_vehicle_offset / sizeof(VEH)];

    // get attacker and defender units

    UNIT attacker_unit = tx_units[attacker_vehicle.proto_id];
    UNIT defender_unit = tx_units[defender_vehicle.proto_id];

    // calculate attacker and defender power
    // artifact gets 1 HP regardless of reactor

    int attacker_power = (attacker_unit.unit_plan == PLAN_ALIEN_ARTIFACT ? 1 : attacker_unit.reactor_type * 10 - attacker_vehicle.damage_taken);
    int defender_power = (defender_unit.unit_plan == PLAN_ALIEN_ARTIFACT ? 1 : defender_unit.reactor_type * 10 - defender_vehicle.damage_taken);

    // determine if we are ignoring reactor power

    bool ignore_reactor_power =
        conf.ignore_reactor_power_in_combat
        ||
        tx_weapon[attacker_unit.weapon_type].offense_value < 0
        ||
        tx_defense[defender_unit.armor_type].defense_value < 0
    ;

    // calculate firepower

    int attacker_fp =
        (ignore_reactor_power ? defender_unit.reactor_type : 1)
    ;
    int defender_fp =
        (ignore_reactor_power ? attacker_unit.reactor_type : 1)
    ;

    // calculate HP

    int attacker_hp = (int)ceil((double)attacker_power / defender_fp);
    int defender_hp = (int)ceil((double)defender_power / attacker_fp);

    // calculate attacker winning probability

    double attacker_winning_probability;

    if (defender_hp <= 0)
    {
        attacker_winning_probability = 1.0;

    }
    else if (attacker_hp <= 0)
    {
        attacker_winning_probability = 0.0;

    }
    else
    {
        if (!conf.alternative_combat_mechanics)
        {
            attacker_winning_probability =
                standard_combat_mechanics_calculate_attacker_winning_probability
                (
                    attacker_strength,
                    defender_strength,
                    attacker_hp,
                    defender_hp
                )
            ;

        }
        else
        {
            attacker_winning_probability =
                alternative_combat_mechanics_calculate_attacker_winning_probability
                (
                    attacker_strength,
                    defender_strength,
                    attacker_hp,
                    defender_hp
                )
            ;

        }

    }

    /*
    // calculate odds

    double odds = 1.0 / (1.0 / attacker_winning_probability - 1.0);

    bool attacker_advantage = (odds >= 1.0);
    double normalized_odds = (attacker_advantage ? odds : 1.0 / odds);

    // find good odds ratio

    int best_numerator = 0;
    int best_denominator = 0;
    double best_difference = -1.0;

    for (int denominator = 0x1; denominator <= 0xA; denominator++)
    {
        int numerator = (int)round(normalized_odds * denominator);
        double odds_ratio = (double)numerator / (double)denominator;
        double difference = abs(odds_ratio - normalized_odds);

        if (best_difference == -1 || difference < best_difference)
        {
            best_difference = difference;
            best_numerator = numerator;
            best_denominator = denominator;

        }

    }

    // set odds

    if (attacker_advantage)
    {
        *attacker_odd = best_numerator;
        *defender_odd = best_denominator;

    }
    else
    {
        *attacker_odd = best_denominator;
        *defender_odd = best_numerator;

    }
    */

    // set attacker odd to attacker winning probability percentage
    // set defender odd to 1 to disable fraction simplification

    *attacker_odd = (int)round(attacker_winning_probability * 100.0);
    *defender_odd = 1;

}

/**
Standard combat mechanics.
Calculates attacker winning probability.
*/
double standard_combat_mechanics_calculate_attacker_winning_probability
(
    int attacker_strength,
    int defender_strength,
    int attacker_hp,
    int defender_hp
)
{
    debug("standard_combat_mechanics_calculate_attacker_winning_probability(attacker_strength=%d, defender_strength=%d, attacker_hp=%d, defender_hp=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_hp,
        defender_hp
    )
    ;

    // determine round probability

    double p = (double)attacker_strength / ((double)attacker_strength + (double)defender_strength);
    double q = 1 - p;

    // calculate attacker winning probability

    double attacker_winning_probability = 0.0;
    double p1 = pow(p, defender_hp);

    for (int alp = 0; alp < attacker_hp; alp++)
    {
        double q1 = pow(q, alp);

        double c = binomial_koefficient(defender_hp - 1 + alp, alp);

        attacker_winning_probability += p1 * q1 * c;

    }

    debug("p(round)=%f, p(battle)=%f\n", p, attacker_winning_probability);

    return attacker_winning_probability;

}

double binomial_koefficient(int n, int k)
{
    double c = 1.0;

    for (int i = 0; i < k; i++)
    {
        c *= (double)(n - i) / (double)(k - i);

    }

    return c;

}

/**
Alternative combat mechanics.
Calculates attacker winning probability.
*/
double alternative_combat_mechanics_calculate_attacker_winning_probability
(
    int attacker_strength,
    int defender_strength,
    int attacker_hp,
    int defender_hp
)
{
    debug("alternative_combat_mechanics_calculate_attacker_winning_probability(attacker_strength=%d, defender_strength=%d, attacker_hp=%d, defender_hp=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_hp,
        defender_hp
    )
    ;

    double attacker_winning_probability;

    // cannot calculate too long tree for HP > 10
    // return 0.5 instead
    if (attacker_hp > 10 || defender_hp > 10)
    {
        attacker_winning_probability = 0.5;

    }
    else if (attacker_hp <= 0 && defender_hp <= 0)
    {
        attacker_winning_probability = 0.5;

    }
    else if (defender_hp <= 0)
    {
        attacker_winning_probability = 1.0;

    }
    else if (attacker_hp <= 0)
    {
        attacker_winning_probability = 0.0;

    }
    else
    {
        // determine first round probability

        double p = (double)attacker_strength / ((double)attacker_strength + (double)defender_strength);
        double q = 1 - p;

        // determine following rounds probabilities

        double qA = q / conf.alternative_combat_mechanics_loss_divider;
        double pA = 1 - qA;
        double pD = p / conf.alternative_combat_mechanics_loss_divider;
        double qD = 1 - pD;

        debug("p=%f, q=%f, pA=%f, qA=%f, pD=%f, qD=%f\n", p, q, pA, qA, pD, qD);

        // calculate attacker winning probability

        attacker_winning_probability =
            // attacker wins first round
            p
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                true,
                pA, qA, pD, qD,
                attacker_hp,
                defender_hp - 1
            )
            +
            // defender wins first round
            q
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                false,
                pA, qA, pD, qD,
                attacker_hp - 1,
                defender_hp
            )
        ;

    }

    debug("attacker_winning_probability=%f\n", attacker_winning_probability);

    return attacker_winning_probability;

}

double alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
(
    bool attacker_won,
    double pA, double qA, double pD, double qD,
    int attacker_hp,
    int defender_hp
)
{
    double attacker_winning_probability;

    if (attacker_hp <= 0 && defender_hp <= 0)
    {
        attacker_winning_probability = 0.5;

    }
    else if (defender_hp <= 0)
    {
        attacker_winning_probability = 1.0;

    }
    else if (attacker_hp <= 0)
    {
        attacker_winning_probability = 0.0;

    }
    else
    {
        attacker_winning_probability =
            // attacker wins round
            (attacker_won ? pA : pD)
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                true,
                pA, qA, pD, qD,
                attacker_hp,
                defender_hp - 1
            )
            +
            // defender wins round
            (attacker_won ? qA : qD)
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                false,
                pA, qA, pD, qD,
                attacker_hp - 1,
                defender_hp
            )
        ;

    }

    return attacker_winning_probability;

}
