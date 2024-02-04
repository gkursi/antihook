/*
  Created on 23.06.18.
*/

#include "common.hpp"
#include <playerlist.hpp>
#include "PlayerTools.hpp"
#include "entitycache.hpp"
#include "settings/Bool.hpp"
#include "MiscTemporary.hpp"

namespace player_tools
{
static settings::Int betrayal_limit{ "player-tools.betrayal-limit", "2" };
static settings::Boolean betrayal_sync{ "player-tools.betrayal-ipc-sync", "true" };

static settings::Boolean ignoreRosnehook{ "player-tools.ignore.rosnehook", "true" }; // why do you have this man.

static std::unordered_map<unsigned, unsigned> betrayal_list{};

static CatCommand forgive_all("pt_forgive_all", "Clear betrayal list", []() { betrayal_list.clear(); });

bool shouldTargetSteamId(unsigned id)
{
    auto &pl = playerlist::AccessData(id);
    if ((pl.state == playerlist::k_EState::CAT))
        return true;
    return false;
}

bool shouldTarget(CachedEntity *entity)
{
    if (entity->m_Type() == ENTITY_PLAYER)
    {
        if (HasCondition<TFCond_HalloweenGhostMode>(entity))
            return false;
        // Don't shoot players in truce
        if (TFGameRules()->IsTruceActive())
            return false;
        if (entity->player_info)
            return shouldTargetSteamId(entity->player_info->friendsID);
        if (HasCondition<TFCond_Bonked> || HasCondition<TFCond_Ubercharged>)
            return false;
    }
    else if (entity->m_Type() == ENTITY_BUILDING)
        // Don't shoot buildings in truce
        if (TFGameRules()->IsTruceActive())
            return false;

    return true;
}
bool shouldAlwaysRenderEspSteamId(unsigned id)
{
    if (id == 0)
        return false;

    auto &pl = playerlist::AccessData(id);
    if (pl.state != playerlist::k_EState::DEFAULT)
        return true;
    return false;
}
bool shouldAlwaysRenderEsp(CachedEntity *entity)
{
    if (entity->m_Type() == ENTITY_PLAYER && entity->player_info)
        return shouldAlwaysRenderEspSteamId(entity->player_info->friendsID);

    return false;
}

#if ENABLE_VISUALS
std::optional<colors::rgba_t> forceEspColorSteamId(unsigned id)
{
    if (id == 0)
        return std::nullopt;

    auto pl = playerlist::Color(id);
    if (pl != colors::empty)
        return std::optional<colors::rgba_t>{ pl };

    return std::nullopt;
}
std::optional<colors::rgba_t> forceEspColor(CachedEntity *entity)
{
    if (entity->m_Type() == ENTITY_PLAYER  && entity->player_info)
        return forceEspColorSteamId(entity->player_info->friendsID);

    return std::nullopt;
}
#endif

void onKilledBy(unsigned id)
{
    auto &pl = playerlist::AccessData(id);
    if (!shouldTargetSteamId(id) && !playerlist::IsFriendly(pl.state))
    {
        // We ignored the gamer, but they still shot us
        if (betrayal_list.find(id) == betrayal_list.end())
            betrayal_list[id] = 0;
        betrayal_list[id]++;
        // Notify other bots
        if (id && betrayal_list[id] == *betrayal_limit && betrayal_sync)
        {
            if (ipc::peer && ipc::peer->connected)
            {
                std::string command = "cat_ipc_exec_all cat_pl_mark_betrayal " + std::to_string(id);
                if (command.length() >= 63)
                    ipc::peer->SendMessage(nullptr, -1, ipc::commands::execute_client_cmd_long, command.c_str(), command.length() + 1);
                else
                    ipc::peer->SendMessage(command.c_str(), -1, ipc::commands::execute_client_cmd, nullptr, 0);

                if (std::ifstream("tf/cfg/betrayals.cfg"))
                {
                    std::ofstream cfg_betrayal;
                    cfg_betrayal.open("tf/cfg/betrayals.cfg", std::ios::app);
                    cfg_betrayal << "cat_pl_add_id " + std::to_string(id) + " ABUSE\n";
                    cfg_betrayal.close();
                }
            }
        }
    }
}

static CatCommand mark_betrayal("pl_mark_betrayal", "Mark a steamid32 as betrayal",
                                [](const CCommand &args)
                                {
                                    if (args.ArgC() < 2)
                                    {
                                        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Please provide a valid steamid32!");
                                        return;
                                    }
                                    try
                                    {
                                        // Grab steamid
                                        unsigned steamid       = std::stoul(args.Arg(1));
                                        betrayal_list[steamid] = *betrayal_limit;
                                    }
                                    catch (const std::invalid_argument &)
                                    {
                                        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Invalid Steamid32 provided.");
                                    }
                                });

void onKilledBy(CachedEntity *entity)
{
    if (entity->player_info)
        onKilledBy(entity->player_info->friendsID);
}

class PlayerToolsEventListener : public IGameEventListener2
{
    void FireGameEvent(IGameEvent *event) override
    {
        int killer_id = GetPlayerForUserID(event->GetInt("attacker"));
        int victim_id = GetPlayerForUserID(event->GetInt("userid"));

        if (victim_id == g_IEngine->GetLocalPlayer())
        {
            onKilledBy(ENTITY(killer_id));
            return;
        }
    }
};

PlayerToolsEventListener &listener()
{
    static PlayerToolsEventListener object{};
    return object;
}

static InitRoutine register_event(
    []()
    {
        g_IEventManager2->AddListener(&listener(), "player_death", false);
        EC::Register(
            EC::Shutdown, []() { g_IEventManager2->RemoveListener(&listener()); }, "playerlist_shutdown");
    });
} // namespace player_tools
