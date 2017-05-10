#include "SavegameManager.h"
#include <utils/Utils.h>
#include <utils/logger.h>
#include <json/json.hpp>
#include <fstream>
#include "engine/GameEngine.h"

using json = nlohmann::json;
using namespace Engine;

/**
 * Gameengine-instance pointer
 */
Engine::GameEngine* gameEngine;

// Enures that all folders to save into the given savegame-slot exist
void ensureSavegameFolders(int idx)
{
    std::string userdata = Utils::getUserDataLocation();

	if(!Utils::mkdir(userdata))
		LogError() << "Failed to create userdata-directory at: " << userdata;

    std::string gameType;
    if (gameEngine->getMainWorld().get().getBasicGameType() == World::EGameType::GT_Gothic1)
    {
        gameType = "/Gothic";
    }
    else
    {
        gameType = "/Gothic 2";
    }

    if (Utils::mkdir(userdata + gameType))
        LogError() << "Failed to create gametype-directory at: " << userdata + gameType;

    if(Utils::mkdir(SavegameManager::buildSavegamePath(idx)))
		LogError() << "Failed to create savegame-directory at: " << SavegameManager::buildSavegamePath(idx);
}

std::string SavegameManager::buildSavegamePath(int idx)
{
    std::string userdata = Utils::getUserDataLocation();

    if (gameEngine->getMainWorld().get().getBasicGameType() == World::EGameType::GT_Gothic1)
    {
        return userdata + "/Gothic/savegame_" + std::to_string(idx);
    }

    return userdata + "/Gothic 2/savegame_" + std::to_string(idx);
}

std::vector<std::string> SavegameManager::getSavegameWorlds(int idx)
{
    std::vector<std::string> worlds;

    Utils::forEachFile(buildSavegamePath(idx), [&](const std::string& path, const std::string& name, const std::string& ext){

        // Check if file is empty
        if(Utils::getFileSize(path + "/" + name) == 0)
            return; // Empty, don't bother

        // Valid worldfile
        worlds.push_back(name);
    });

    return worlds;
}

void SavegameManager::clearSavegame(int idx)
{
    if(!isSavegameAvailable(idx))
        return; // Don't touch any files if we don't have to...

    Utils::forEachFile(buildSavegamePath(idx), [](const std::string& path, const std::string& name, const std::string& ext)
    {
        // Make sure this is a REGoth-file
        if(name.find("regoth_") == std::string::npos && name.find("world_") == std::string::npos)
            return; // Better not touch that one

        // Empty the file
        FILE* f = fopen((path + "/" + name).c_str(), "w");
        if(!f)
        {
            LogWarn() << "Failed to clear file: " << path << "/" << name;
            return;
        }

        fclose(f);

    }, false); // For the love of god, dont recurse in case something really goes wrong!
}

bool SavegameManager::isSavegameAvailable(int idx)
{
    return Utils::getFileSize(buildSavegamePath(idx) + "/regoth_save.json") > 0;
}

bool SavegameManager::writeSavegameInfo(int idx, const SavegameInfo& info)
{
    std::string infoFile = buildSavegamePath(idx) + "/regoth_save.json";

    ensureSavegameFolders(idx);

    json j;
    j["version"] = info.LATEST_KNOWN_VERSION;
    j["name"] = info.name;
    j["world"] = info.world;
    j["timePlayed"] = info.timePlayed;

    LogInfo() << "Writing savegame-info: " << infoFile;

    // Save
    std::ofstream f(infoFile);

    if(!f.is_open())
    {
        LogWarn() << "Failed to save data! Could not open file: " << buildSavegamePath(idx) + "/regoth_save.json";
        return false;
    }

    f << j.dump(4);
    f.close();

    return true;
}
        
Engine::SavegameManager::SavegameInfo SavegameManager::readSavegameInfo(int idx)
{
    std::string info = buildSavegamePath(idx) + "/regoth_save.json";

    if(!Utils::getFileSize(info))
        return SavegameInfo();

    LogInfo() << "Reading savegame-info: " << info;

    std::string infoContents = Utils::readFileContents(info);
    json j = json::parse(infoContents); 

    unsigned int version = 0;
    // check can be removed if backwards compability with save games without version number is not needed anymore
    if (j.find("version") != j.end())
    {
        version = j["version"];
    }
    SavegameInfo o;
    o.version = version;
    o.name = j["name"];
    o.world = j["world"];
    o.timePlayed = j["timePlayed"];

    return o;
}

bool SavegameManager::writeWorld(int idx, const std::string& worldName, const std::string& data)
{
    std::string file = buildSavegamePath(idx) + "/world_" + worldName + ".json";

    ensureSavegameFolders(idx);
    
    LogInfo() << "Writing world-file: " << file;

    std::ofstream f(file);

    if(!f.is_open())
    {
        LogWarn() << "Failed to save data! Could not open file: " + file; 
        return false;
    }

    f << data;

    return true;
}

std::string SavegameManager::readWorld(int idx, const std::string& worldName)
{
    std::string file = buildSavegamePath(idx) + "/world_" + worldName + ".json";

    if(!Utils::getFileSize(file))
        return ""; // Not found or empty

    LogInfo() << "Reading world-file: " << file;
    return Utils::readFileContents(file);
}
        
std::string SavegameManager::buildWorldPath(int idx, const std::string& worldName)
{
   return buildSavegamePath(idx) + "/world_" + worldName + ".json"; 
}

bool Engine::SavegameManager::init(Engine::GameEngine& engine)
{
    gameEngine = &engine;

    return true;
}

std::vector<std::shared_ptr<const std::string>> SavegameManager::gatherAvailableSavegames()
{
    int numSlots = maxSlots();

    std::vector<std::shared_ptr<const std::string>> names(numSlots, nullptr);

    // Try every slot
    for (int i = 0; i < numSlots; ++i)
    {
        if (isSavegameAvailable(i))
        {
            SavegameInfo info = readSavegameInfo(i);
            names[i] = std::make_shared<const std::string>(info.name);
        } 
    }
    // for log purpose only
    {
        std::vector<std::string> names2;
        for (auto& namePtr : names)
        {
            if (namePtr)
                names2.push_back(*namePtr);
            else
                names2.push_back("");
        }
        LogInfo() << "Available savegames: " << names2;
    }

    return names;
}

std::string Engine::SavegameManager::loadSaveGameSlot(int index) {
    // Lock to number of savegames
    assert(index >= 0 && index < maxSlots());

    if(!isSavegameAvailable(index))
    {
        return "Savegame at slot " + std::to_string(index) + " not available!";
    }

    // Read general information about the saved game. Most importantly the world the player saved in
    SavegameInfo info = readSavegameInfo(index);

    std::string worldPath = buildWorldPath(index, info.world);

    // Sanity check, if we really got a safe for this world. Otherwise we would end up in the fresh version
    // if it was missing. Also, IF the player saved there, there should be a save for this.
    if(!Utils::getFileSize(worldPath))
    {
        return "Target world-file invalid: " + worldPath;
    }

    gameEngine->loadWorld(info.world + ".zen", worldPath);
    gameEngine->getGameClock().setTotalSeconds(info.timePlayed);
    return "";
}

int Engine::SavegameManager::maxSlots() {
    switch(gameEngine->getMainWorld().get().getBasicGameType())
    {
        case World::EGameType::GT_Gothic1:
            return G1_MAX_SLOTS;
        case World::EGameType::GT_Gothic2:
            return G2_MAX_SLOTS;
        default:
            return G2_MAX_SLOTS;
    }
}

void Engine::SavegameManager::saveToSaveGameSlot(int index, std::string savegameName) {
    assert(index >= 0 && index < maxSlots());

    if (savegameName.empty())
        savegameName = std::string("Slot") + std::to_string(index);

    // TODO: Should be writing to a temp-directory first, before messing with the save-files already existing
    // Clean data from old savegame, so we don't load into worlds we haven't been to yet
    Engine::SavegameManager::clearSavegame(index);

    // Write information about the current game-state
    Engine::SavegameManager::SavegameInfo info;
    info.version = Engine::SavegameManager::SavegameInfo::LATEST_KNOWN_VERSION;
    info.name = savegameName;
    info.world = Utils::stripExtension(gameEngine->getMainWorld().get().getZenFile());
    info.timePlayed = gameEngine->getGameClock().getTotalSeconds();
    Engine::SavegameManager::writeSavegameInfo(index, info);

    json j;
    gameEngine->getMainWorld().get().exportWorld(j);

    // Save
    Engine::SavegameManager::writeWorld(index, info.world, Utils::iso_8859_1_to_utf8(j.dump(4)));
}

