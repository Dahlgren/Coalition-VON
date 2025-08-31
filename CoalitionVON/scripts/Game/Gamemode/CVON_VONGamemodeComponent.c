enum CVON_EVONType
{
	DIRECT,
	RADIO
}

class CVON_VONGameModeComponentClass: SCR_BaseGameModeComponentClass
{
}

class CVON_VONGameModeComponent: SCR_BaseGameModeComponent
{
	//These are stored from server settings on the server so clients can know what ChannelName and whats the password to join that VOIP channel.
	[RplProp()] string m_sTeamSpeakChannelName = "";
	[RplProp()] string m_sTeamSpeakChannelPassword = "";
	float m_fTeamSpeakPluginVersion = 1.3;
	
	//If disabled everyone shares the same frequencies.
	[Attribute("1")] bool m_bUseFactionEcncryption;
	
	//Wow a pointer, so performant
	static CVON_VONGameModeComponent m_Instance;
	
	//When the gamemode is created on the server we need to create a server setting file.
	//If already created lets populate that data into the gamemode for clients to us.
	//Delay is necessary
	//==========================================================================================================================================================================
	void CVON_VONGameModeComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		m_Instance = this;
		if (!System.IsConsoleApp())
			return;
		
		GetGame().GetCallqueue().CallLater(InitializeChannelId, 500, false);
	}
	
	//Explained above
	//==========================================================================================================================================================================
	void InitializeChannelId()
	{
		SCR_JsonLoadContext JSONLoad = new SCR_JsonLoadContext();
		if (!JSONLoad.LoadFromFile("$profile:/VONServerSettings.json"))
		{
			SCR_JsonSaveContext ServerJSON = new SCR_JsonSaveContext();
			ServerJSON.StartObject("Server Settings");
			ServerJSON.WriteValue("VONChannelName", "");
			ServerJSON.WriteValue("VONChannelPassword", "");
			ServerJSON.EndObject();
			ServerJSON.SaveToFile("$profile:/VONServerSettings.json");
		}
		else
		{
			int channelId;
			JSONLoad.StartObject("Server Settings");
			JSONLoad.ReadValue("VONChannelName", m_sTeamSpeakChannelName);
			JSONLoad.ReadValue("VONChannelPassword", m_sTeamSpeakChannelPassword);
			JSONLoad.EndObject();
			Replication.BumpMe();
		}
	}
	
	//mmmm more pointer
	//==========================================================================================================================================================================
	static CVON_VONGameModeComponent GetInstance()
	{
		return m_Instance;
	}
	
	//I go into more detail in the VONController but this broadcasts to any player in the playerId array
	//This makes it so only the players that need to get the VONEntry get it, saving on that sweet sweet sweet network.
	//Broadcasts are only tracked on the clients, server doesn't track it anywhere.
	//==========================================================================================================================================================================
	void AddLocalVONBroadcasts(CVON_VONContainer VONContainer, array<int> playerIds, int playerIdToAdd, RplId radioId)
	{
		int maxDist = 0;
		if (radioId != RplId.Invalid())
		{
			if (!Replication.FindItem(radioId))
			return;
		
			IEntity radio = RplComponent.Cast(Replication.FindItem(radioId)).GetEntity();
			if (!radio)
				return;
			
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
			if (!radioComp)
				return;
			
			maxDist = radioComp.m_iRadioRange;
		}
		
		foreach (int playerId: playerIds)
		{
			SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).AddLocalVONBroadcast(VONContainer, playerIdToAdd, GetGame().GetPlayerManager().GetPlayerControlledEntity(playerIdToAdd).GetOrigin(), maxDist);
		}
	}
	
	
	//Same as above braodcasts only to the player that specifically needs to remove the broadcast
	//==========================================================================================================================================================================
	void RemoveLocalVONBroadcasts(int playerId, int playerIdToRemove)
	{
		SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).RemoveLocalVONBroadcast(playerIdToRemove);
	}
	
	//Just an easy way for me to get all the radios on an entity.
	//==========================================================================================================================================================================
	array<RplId> GetRadios(IEntity entity)
	{
		if (!entity)
			return null;
		
		ref array<RplId> radios = {};
		SCR_InventoryStorageManagerComponent inventoryComp = SCR_InventoryStorageManagerComponent.Cast(entity.FindComponent(SCR_InventoryStorageManagerComponent));
		ref array<IEntity> items = {};
		inventoryComp.GetItems(items);
		foreach (IEntity item: items)
		{
			if (!item.FindComponent(CVON_RadioComponent))
				continue;
			
			radios.Insert(RplComponent.Cast(item.FindComponent(RplComponent)).Id());
		}
		return radios;
	}
}