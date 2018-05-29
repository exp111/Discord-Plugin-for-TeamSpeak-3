/*
* TeamSpeak 3 demo plugin
*
* Copyright (c) 2008-2017 TeamSpeak Systems GmbH
*/

#ifdef _WIN32
#pragma warning (disable : 4100)  /* Disable Unreferenced parameter warning */
#include <Windows.h>
#endif

#pragma warning (disable : 4996)
#pragma comment(lib, "discord-rpc.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "teamspeak/clientlib_publicdefinitions.h"
#include "ts3_functions.h"
#include "plugin.h"
#include "discord_rpc.h"

#include <iostream>
#include <string>
#include <time.h>
#include <vector>

static struct TS3Functions ts3Functions;

#ifdef _WIN32
#define _strcpy(dest, destSize, src) strcpy_s(dest, destSize, src)
#define snprintf sprintf_s
#else
#define _strcpy(dest, destSize, src) { strncpy(dest, src, destSize-1); (dest)[destSize-1] = '\0'; }
#endif

#define PLUGIN_API_VERSION 22

#define PATH_BUFSIZE 512
#define COMMAND_BUFSIZE 128
#define INFODATA_BUFSIZE 128
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128

//Global Variables
static char* pluginID = NULL;
static const char* APPLICATION_ID = "450824928841957386"; // 450824928841957386
static int64_t StartTime;

#ifdef _WIN32
/* Helper function to convert wchar_T to Utf-8 encoded strings on Windows */
static int wcharToUtf8(const wchar_t* str, char** result) {
	int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
	*result = (char*)malloc(outlen);
	if (WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0) {
		*result = NULL;
		return -1;
	}
	return 0;
}
#endif

bool isClientInList(anyID* clientList, anyID* clientID)
{
	for (int i = 0; clientList[i]; i++)
	{
		//ts3Functions.printMessageToCurrentTab(std::string(std::to_string(clientList[i]) + ";" +std::to_string(*clientID) + "\n").c_str());
		if (clientList[i] == *clientID)
			return true;
	}

	return false;
}

int getChannelCount(uint64 serverConnectionHandlerID, uint64 mChannelID)
{
	std::vector<anyID> channelClientVector = std::vector<anyID>();

	anyID* channelClientList;
	ts3Functions.getChannelClientList(serverConnectionHandlerID, mChannelID, &channelClientList);

	anyID* clientList;
	if (ts3Functions.getClientList(serverConnectionHandlerID, &clientList) != ERROR_ok)
		return 0;

	int clientType;
	for (int i = 0; clientList[i]; i++)
	{
		if (ts3Functions.getClientVariableAsInt(serverConnectionHandlerID, clientList[i], CLIENT_TYPE, &clientType) != ERROR_ok)
			continue;

		//if (clientType == 1) //Query
		//	continue;

		if (!isClientInList(channelClientList, &clientList[i]))
			continue;

		channelClientVector.push_back(clientList[i]);
	}

	return (int)channelClientVector.size();
}

int getServerCount(uint64 serverConnectionHandlerID)
{
	int serverClientCount = 0;
	ts3Functions.getServerVariableAsInt(serverConnectionHandlerID, VIRTUALSERVER_CLIENTS_ONLINE, &serverClientCount);
	if (serverClientCount <= 0)
	{
		ts3Functions.requestServerVariables(serverConnectionHandlerID);
		serverClientCount = 0;
		anyID* clientList;
		if (ts3Functions.getClientList(serverConnectionHandlerID, &clientList) == ERROR_ok)
		{
			for (int i = 0; clientList[i]; i++)
			{
				serverClientCount++;
			}
		}
	}

	return serverClientCount;
}

static void handleDiscordReady(void)
{
	printf("\nDiscord: ready\n");
	ts3Functions.logMessage("ready", LogLevel_DEBUG, "DiscordPlugin", 0);
}

static void handleDiscordDisconnected(int errcode, const char* message)
{
	printf("\nDiscord: disconnected (%d: %s)\n", errcode, message);
	ts3Functions.logMessage("disconnected", LogLevel_DEBUG, "DiscordPlugin", 0);
}

static void handleDiscordError(int errcode, const char* message)
{
	printf("\nDiscord: error (%d: %s)\n", errcode, message);
	ts3Functions.logMessage("error", LogLevel_DEBUG, "DiscordPlugin", 0);
}

static void discordInit()
{
	DiscordEventHandlers handlers;
	memset(&handlers, 0, sizeof(handlers));
	handlers.ready = handleDiscordReady;
	handlers.disconnected = handleDiscordDisconnected;
	handlers.errored = handleDiscordError;
	handlers.joinGame = nullptr;
	handlers.spectateGame = nullptr;
	handlers.joinRequest = nullptr;
	Discord_Initialize(APPLICATION_ID, &handlers, 1, NULL);
}

DiscordRichPresence discordPresence;

static void updateDiscordPresence()
{
	static bool needsClear = false;
	uint64 serverConnectionHandlerID = ts3Functions.getCurrentServerConnectionHandlerID();
	int connected = 0;

	if (ts3Functions.getServerVariableAsInt(serverConnectionHandlerID, VIRTUALSERVER_UNIQUE_IDENTIFIER, &connected) != ERROR_not_connected)
		connected = 1;
	else
		connected = 0;

	if (connected == 1) //We assume that we're connected and can access everything
	{
		needsClear = true;
		anyID mClientID = 0;
		ts3Functions.getClientID(serverConnectionHandlerID, &mClientID);
		uint64 mChannelID = 0;
		ts3Functions.getChannelOfClient(serverConnectionHandlerID, mClientID, &mChannelID);

		memset(&discordPresence, 0, sizeof(discordPresence));

		char* cBuffer = new char[CHANNELINFO_BUFSIZE];

		ts3Functions.getChannelVariableAsString(serverConnectionHandlerID, mChannelID, ChannelProperties::CHANNEL_NAME, &cBuffer);
		discordPresence.state = cBuffer; //channelname ex: Dj's Kotstube
		
		ts3Functions.getServerVariableAsString(serverConnectionHandlerID, VIRTUALSERVER_NAME, &cBuffer);
		std::string buffer;
		buffer = cBuffer;
		unsigned short port = 0;
		char* shit = new char[128];
		ts3Functions.getServerConnectInfo(serverConnectionHandlerID, cBuffer, &port, shit, 512);
		//buffer += " - " + std::string(cBuffer) + ":" + std::to_string(port);
		discordPresence.details = buffer.c_str(); //connection: servername/ip; ex: Connected: macoga.de
		discordPresence.startTimestamp = StartTime;
		discordPresence.partyId = std::to_string(mChannelID).c_str(); //Channel id

		discordPresence.partySize = getChannelCount(serverConnectionHandlerID, mChannelID); //Channel clients
		discordPresence.partyMax = getServerCount(serverConnectionHandlerID); //server clients
		discordPresence.largeImageKey = "logo";
		char* cBuffer2 = new char[TS3_MAX_SIZE_CLIENT_NICKNAME];
		ts3Functions.getClientSelfVariableAsString(serverConnectionHandlerID, CLIENT_NICKNAME, &cBuffer2);
		std::string buffer2;
		buffer2 = cBuffer2;
		discordPresence.largeImageText = buffer2.c_str();
		Discord_UpdatePresence(&discordPresence);

		delete cBuffer;
		//delete cBuffer2;
		delete shit;
	}
	else 
	{
		if (needsClear)
		{
			Discord_ClearPresence();
			needsClear = false;
		}
	}
}

/*********************************** Required functions ************************************/


const char* ts3plugin_name() {
#ifdef _WIN32
	static char* result = NULL;  /* Static variable so it's allocated only once */
	if (!result) {
		const wchar_t* name = L"Discord Rich Presence";
		if (wcharToUtf8(name, &result) == -1) {  /* Convert name into UTF-8 encoded result */
			result = "Discord Rich Presence";  /* Conversion failed, fallback here */
		}
	}
	return result;
#else
	return "Discord Rich Presence";
#endif
}

const char* ts3plugin_version() {
	return "1.0";
}

int ts3plugin_apiVersion() {
	return PLUGIN_API_VERSION;
}

const char* ts3plugin_author() {
	return "Exp, Bluscream";
}

const char* ts3plugin_description() {
	return "Shows your Teamspeak connection on Discord through Rich Presence.";
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
	ts3Functions = funcs;
}

int ts3plugin_init() {
	/*char appPath[PATH_BUFSIZE];
	char resourcesPath[PATH_BUFSIZE];
	char configPath[PATH_BUFSIZE];
	char pluginPath[PATH_BUFSIZE];

	ts3Functions.getAppPath(appPath, PATH_BUFSIZE);
	ts3Functions.getResourcesPath(resourcesPath, PATH_BUFSIZE);
	ts3Functions.getConfigPath(configPath, PATH_BUFSIZE);
	ts3Functions.getPluginPath(pluginPath, PATH_BUFSIZE, pluginID);*/

	StartTime = time(0);
	discordInit();

	return 0;
}

void ts3plugin_shutdown() {
	if (pluginID) {
		free(pluginID);
		pluginID = NULL;
	}
}

int ts3plugin_offersConfigure() {
	return PLUGIN_OFFERS_NO_CONFIGURE;
}

void ts3plugin_registerPluginID(const char* id) {
	const size_t sz = strlen(id) + 1;
	pluginID = (char*)malloc(sz * sizeof(char));
	_strcpy(pluginID, sz, id);
}

const char* ts3plugin_commandKeyword() {
	//return "test";
	return NULL; //Command Suffix for chat; example: /test
}

const char* ts3plugin_infoTitle() {
	return "Discord Rich Presence";
}

void ts3plugin_freeMemory(void* data) {
	free(data);
}
int ts3plugin_requestAutoload() {
	return 0;
}

//Global Variables

//Helper Functions

/* Helper function to create a menu item */
static struct PluginMenuItem* createMenuItem(enum PluginMenuType type, int id, const char* text, const char* icon) {
	struct PluginMenuItem* menuItem = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
	menuItem->type = type;
	menuItem->id = id;
	_strcpy(menuItem->text, PLUGIN_MENU_BUFSZ, text);
	_strcpy(menuItem->icon, PLUGIN_MENU_BUFSZ, icon);
	return menuItem;
}

/* Some makros to make the code to create menu items a bit more readable */
#define BEGIN_CREATE_MENUS(x) const size_t sz = x + 1; size_t n = 0; *menuItems = (struct PluginMenuItem**)malloc(sizeof(struct PluginMenuItem*) * sz);
#define CREATE_MENU_ITEM(a, b, c, d) (*menuItems)[n++] = createMenuItem(a, b, c, d);
#define END_CREATE_MENUS (*menuItems)[n++] = NULL; assert(n == sz);


enum {
	MENU_ID_GLOBAL_1 = 1,
	MENU_ID_MAX
};

void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {

	BEGIN_CREATE_MENUS(MENU_ID_MAX - 1); //Needs to be correct
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_1, "Refresh", "");
	END_CREATE_MENUS;

	*menuIcon = (char*)malloc(PLUGIN_MENU_BUFSZ * sizeof(char));
	_strcpy(*menuIcon, PLUGIN_MENU_BUFSZ, ""); //PLUGIN MENU IMAGE
}

/************************** TeamSpeak callbacks ***************************/


void ts3plugin_onMenuItemEvent(uint64 serverConnectionHandlerID, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
	switch (type) {
	case PLUGIN_MENU_TYPE_GLOBAL:
		switch (menuItemID) {
		case MENU_ID_GLOBAL_1:
			updateDiscordPresence();
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber)
{
	if (newStatus == STATUS_CONNECTION_ESTABLISHED || newStatus == STATUS_DISCONNECTED)
	{
		updateDiscordPresence();
	}
}

void inline shit(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID)
{
	anyID mClientID = 0;
	ts3Functions.getClientID(serverConnectionHandlerID, &mClientID);
	uint64 mChannelID = 0;
	ts3Functions.getChannelOfClient(serverConnectionHandlerID, mClientID, &mChannelID);

	//we're gone || client was in our channel || client is now in our channel
	if (mClientID == clientID || oldChannelID == mChannelID || newChannelID == mChannelID)
		updateDiscordPresence();
}

void ts3plugin_onTalkStatusChangeEvent(uint64 serverConnectionHandlerID, int status, int isReceivedWhisper, anyID clientID) {
	anyID mClientID = 0;
	ts3Functions.getClientID(serverConnectionHandlerID, &mClientID);
	if (clientID != mClientID) return;
	int isChannelCommander = 0;
	ts3Functions.getClientSelfVariableAsInt(serverConnectionHandlerID, CLIENT_IS_CHANNEL_COMMANDER, &isChannelCommander);
	if        (status == 1 && isChannelCommander == 1) {
		discordPresence.smallImageKey = "player_commander_on";
		discordPresence.smallImageText = "Talking with Channel Commander";
	} else if (status == 1 && isChannelCommander == 0) {
			discordPresence.smallImageKey = "player_on";
			discordPresence.smallImageText = "Talking";
	} else if (status == 0 && isChannelCommander == 1) {
		discordPresence.smallImageKey = "player_commander_off";
		discordPresence.smallImageText = "Silent with Channel Commander";
	} else if (status == 0 && isChannelCommander == 0) {
		discordPresence.smallImageKey = "player_off";
		discordPresence.smallImageText = "Silent";
	}
	Discord_UpdatePresence(&discordPresence);
}
char* oldnick = new char[TS3_MAX_SIZE_CLIENT_NICKNAME];
void ts3plugin_onUpdateClientEvent(uint64 serverConnectionHandlerID, anyID clientID, anyID invokerID, const char* invokerName, const char* invokerUniqueIdentifier) {
	anyID mClientID = 0;
	ts3Functions.getClientID(serverConnectionHandlerID, &mClientID);
	if (clientID != mClientID) return;
	char* cBuffer2 = new char[TS3_MAX_SIZE_CLIENT_NICKNAME];
	ts3Functions.getClientSelfVariableAsString(serverConnectionHandlerID, CLIENT_NICKNAME, &cBuffer2);
	if (!strcmp(oldnick, cBuffer2)) {
		oldnick = cBuffer2;
		std::string buffer2;
		buffer2 = cBuffer2;
		discordPresence.largeImageText = buffer2.c_str();
		Discord_UpdatePresence(&discordPresence);
		delete cBuffer2;
	}
}

void ts3plugin_onClientMoveEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char * moveMessage)
{
	shit(serverConnectionHandlerID, clientID, oldChannelID, newChannelID);
}

void ts3plugin_onClientMoveTimeoutEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char * timeoutMessage)
{
	shit(serverConnectionHandlerID, clientID, oldChannelID, newChannelID);
}

void ts3plugin_onClientMoveMovedEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID moverID, const char * moverName, const char * moverUniqueIdentifier, const char * moveMessage)
{
	shit(serverConnectionHandlerID, clientID, oldChannelID, newChannelID);
}

void ts3plugin_onClientKickFromChannelEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID kickerID, const char * kickerName, const char * kickerUniqueIdentifier, const char * kickMessage)
{
	shit(serverConnectionHandlerID, clientID, oldChannelID, newChannelID);
}

void ts3plugin_onClientKickFromServerEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID kickerID, const char * kickerName, const char * kickerUniqueIdentifier, const char * kickMessage)
{
	shit(serverConnectionHandlerID, clientID, oldChannelID, newChannelID);
}

void ts3plugin_onClientBanFromServerEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID kickerID, const char * kickerName, const char * kickerUniqueIdentifier, uint64 time, const char * kickMessage)
{
	shit(serverConnectionHandlerID, clientID, oldChannelID, newChannelID);
}

const char* ts3plugin_keyDeviceName(const char* keyIdentifier) {
	return NULL;
}

const char* ts3plugin_displayKeyText(const char* keyIdentifier) {
	return NULL;
}

const char* ts3plugin_keyPrefix() {
	return NULL;
}
