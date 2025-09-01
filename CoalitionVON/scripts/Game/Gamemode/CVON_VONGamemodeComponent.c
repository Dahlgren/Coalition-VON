enum CVON_EVONType
{
	DIRECT,
	RADIO
}

[BaseContainerProps()]
class CVON_SharedFrequencyObject
{
	[Attribute()] ref array<string> m_aFactionIds;
	[Attribute()] string m_sSharedFrequency;
}

class CVON_VONGameModeComponentClass: SCR_BaseGameModeComponentClass
{
}

class CVON_VONGameModeComponent: SCR_BaseGameModeComponent
{
	//These are stored from server settings on the server so clients can know what ChannelName and whats the password to join that VOIP channel.
	[RplProp()] string m_sTeamSpeakChannelName = "";
	[RplProp()] string m_sTeamSpeakChannelPassword = "";
	float m_fTeamSpeakPluginVersion = 1.4;
	
	//If disabled everyone shares the same frequencies.
	[Attribute("1")] bool m_bUseFactionEcncryption;
	
	//Mostly used so i don't lose my fucking MIND testing the mod in dedicated.
	[Attribute("1")] bool m_bTeamspeakChecks;
	
	[Attribute()] ref array<ref CVON_SharedFrequencyObject> m_aSharedFrequencies;
	
	//Wow a pointer, so performant
	static CVON_VONGameModeComponent m_Instance;
	
	//Preset Frequency Config resource
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "", "conf class=CVON_FreqConfig")]
	ResourceName m_sFreqConfig;
	
	//The actual Frequency Config Resource loaded OnPostInit
	ref CVON_FreqConfig m_FreqConfig;
	
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
	
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (m_sFreqConfig != "")
			m_FreqConfig = CVON_FreqConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(BaseContainerTools.LoadContainer(m_sFreqConfig).GetResource().ToBaseContainer()));
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
			if (!GetGame().GetPlayerManager().GetPlayerController(playerId))
				continue;
			SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).AddLocalVONBroadcast(VONContainer, playerIdToAdd, GetGame().GetPlayerManager().GetPlayerControlledEntity(playerIdToAdd).GetOrigin(), maxDist);
		}
	}
	
	
	//Same as above braodcasts only to the player that specifically needs to remove the broadcast
	//==========================================================================================================================================================================
	void RemoveLocalVONBroadcasts(int playerId, int playerIdToRemove)
	{
		if (!GetGame().GetPlayerManager().GetPlayerController(playerId))
			return;
		
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
	
	void OpenHandMicServer(int playerId, RplId handMicEntityId)
	{
		if (!Replication.FindItem(handMicEntityId))
			return;
		
		IEntity entity = RplComponent.Cast(Replication.FindItem(handMicEntityId)).GetEntity();
		if (!entity)
			return;
		
		int amountOfRadios = 0;
		ref array<string> freqs = {};
		ref array<string> radioNames = {};
		
		SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(entity);
		ref array<SCR_GadgetComponent> gadgets = gadgetManager.GetGadgetsByType(EGadgetType.RADIO);
		if (!gadgets)
			return;
		foreach (SCR_GadgetComponent gadget: gadgets)
		{
			if (!gadget.GetOwner().FindComponent(CVON_RadioComponent))
				continue;
			
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(gadget.GetOwner().FindComponent(CVON_RadioComponent));
			freqs.Insert(radioComp.m_sFrequency);
			radioNames.Insert(radioComp.m_sRadioName);
			amountOfRadios++;
		}
		
		if (amountOfRadios == 0)
			return;
		
		SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).OpenHandMicOwner(freqs, amountOfRadios, radioNames);
	}
}