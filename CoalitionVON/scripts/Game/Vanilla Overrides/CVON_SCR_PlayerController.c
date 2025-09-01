//The five volume levels of your voice, tracked here so its at a per player basis.
enum CVON_EVONVolume
{
	WHISPER,
	QUIET,
	NORMAL,
	LOUD,
	YELLING
}

class CVON_RadioSettingObject
{
	string m_sFreq = "";
	CVON_EStereo m_Stereo = CVON_EStereo.BOTH;
	int m_iVolume = 9;
}

class CVON_RadioSettings
{
	ref array<ref CVON_RadioSettingObject> m_aSRRadioSettings = {};
	ref array<ref CVON_RadioSettingObject> m_aMRsRadioSettings = {};
	ref array<ref CVON_RadioSettingObject> m_aLRRadioSettings = {};
}

modded class SCR_PlayerController
{
	//This is how we store who is talking to us and how, we use this in the VONController to populate the JSON.
	ref array<ref CVON_VONContainer> m_aLocalActiveVONEntries = {};
	
	//Used so we can find the entry by playerId
	ref array<int> m_aLocalActiveVONEntriesIds = {};
	
	//Local client and Server track this array. Used to determine radios priority, 0 = SR, 1 = LR, 2 = MR. Keybinds line up like that.
	ref array<IEntity> m_aRadios = {};
	
	//Used to store the Id from the JSON that teamspeak rights to, this is so we can track teamspeak clientIds in game.
	int m_iTeamSpeakClientId = 0;
	
	float m_fTeamspeakPluginVersion = 0;
	
	//How we link what level the enum below should be at.
	int m_iLocalVolume = 15;
	CVON_EVONVolume m_eVONVolume = CVON_EVONVolume.NORMAL;
	int m_aVolumeValues[5] = {3, 10, 15, 25, 40};
	
	//Used so we don't spam the player with initial warnings if their TS crashes, just when they first connect.
	bool m_bHasBeenGivenInitialWarning = false;
	
	//Teamspeak has been detected, also used to not have the annoying game locking popup everytime TS wants to crash
	bool m_bHasConnectedToTeamspeakForFirstTime = false;
	
	//Used so we can keep Stereo and Volume values
	ref CVON_RadioSettings m_RadioSettings = new CVON_RadioSettings();
	
	
	
	//Used to initials the m_aRadio array
	//Delay is needed as it can take a sec for the entity to initialized for a client on the server.
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);
		GetGame().GetCallqueue().CallLater(InitializeRadios, 500, false, to);
	}
	
	//Handles initializing the m_aRadios array for both this client and the server so both are on the same page
	//Also used to load any settings the radios may have had on respawn.
	//Loading settings only works if the radios where pe configured with the CVON_FreqConfig.
	void InitializeRadios(IEntity to)
	{
		m_aRadios.Clear();
		array<RplId> radios = CVON_VONGameModeComponent.GetInstance().GetRadios(to);
		if (!radios)
			return;
		if (radios.Count() == 0)
			return;
		ref array<IEntity> shortRangeRadios = {};
		ref array<IEntity> longRangeRadios = {};
		ref array<IEntity> mediumRangeRadios = {};
		int SRIndex = 0;
		int MRIndex = 0;
		int LRIndex = 0;
		foreach (RplId radio: radios)
		{
			if (!Replication.FindItem(radio))
				continue;
			
			IEntity radioObject = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
			if (!radioObject)
				continue;
			
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioObject.FindComponent(CVON_RadioComponent));
			FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetControlledEntity().FindComponent(FactionAffiliationComponent));
			if (!factionComp)
				return;
			string factionKey = factionComp.GetAffiliatedFactionKey();
			//Used so we can assing settings to frequencies.
			SCR_Faction faction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(factionKey));
			SCR_GroupsManagerComponent groupManager = SCR_GroupsManagerComponent.GetInstance();
			array<SCR_AIGroup> groups = groupManager.GetPlayableGroupsByFaction(faction);
			SCR_AIGroup playersGroup = groupManager.GetPlayerGroup(GetPlayerId());
			CVON_GroupFrequencyContainer freqContainer;
			int index = groups.Find(playersGroup);
			string playersGroupName;
			if (index != -1)
			{
				string company;
				string platoon;
				string squad;
				string character;
				string format;
				playersGroup.GetCallsigns(company, platoon, squad, character, format);
				playersGroupName = string.Format(format, company, platoon, squad, character);
			}
			CVON_VONGameModeComponent gamemodeComp = CVON_VONGameModeComponent.GetInstance();
			if (gamemodeComp.m_FreqConfig)
			{
				foreach (CVON_GroupFrequencyContainer freqItem: gamemodeComp.m_FreqConfig.m_aPresetGroupFrequencyContainers)
				{
					foreach (string groupName: freqItem.m_aGroupNames)
					{
						if (groupName != playersGroupName && groupName != playersGroup.GetCustomNameServer())
							continue;
						
						freqContainer = freqItem;
						break;
					}
				}
			}
			foreach (CVON_GroupFrequencyContainer container: faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
			{
				foreach (string groupName: container.m_aGroupNames)
				{
					if (groupName != playersGroupName && groupName != playersGroup.GetCustomNameServer())
						continue;
					
					freqContainer = container;
					break;
				}
			}
			switch (radioComp.m_eRadioType)
			{
				case CVON_ERadioType.SHORT:
				{
					if (!shortRangeRadios.Contains(radioObject))
					{
						shortRangeRadios.Insert(radioObject);
						if (System.IsConsoleApp())
							break;
						if (!freqContainer)
							break;
						if (!freqContainer.m_aSRFrequencies)
							break;
						if (freqContainer.m_aSRFrequencies.Count() < SRIndex + 1)
							break;
						if (m_RadioSettings.m_aSRRadioSettings)
						{
							if (m_RadioSettings.m_aSRRadioSettings.Count() - 1 < SRIndex)
							{
								ref CVON_RadioSettingObject settings = new CVON_RadioSettingObject();
								settings.m_sFreq = freqContainer.m_aSRFrequencies.Get(SRIndex);
								m_RadioSettings.m_aSRRadioSettings.Insert(settings);
								SRIndex++;
							}
							else
							{
								ref CVON_RadioSettingObject settings = m_RadioSettings.m_aSRRadioSettings.Get(SRIndex);
								radioComp.m_eStereo = settings.m_Stereo;
								radioComp.m_iVolume = settings.m_iVolume;
								SRIndex++;
							}
						}
						else
						{
							ref CVON_RadioSettingObject settings = new CVON_RadioSettingObject();
							settings.m_sFreq = freqContainer.m_aSRFrequencies.Get(SRIndex);
							m_RadioSettings.m_aSRRadioSettings.Insert(settings);
							SRIndex++;
						}
					}
						
					break;
				}
				case CVON_ERadioType.MEDIUM:
				{
					if (!mediumRangeRadios.Contains(radioObject))
					{
						mediumRangeRadios.Insert(radioObject);
						if (System.IsConsoleApp())
							break;
						if (!freqContainer)
							break;
						if (!freqContainer.m_aMRFrequencies)
							break;
						if (freqContainer.m_aMRFrequencies.Count() < SRIndex + 1)
							break;
						if (m_RadioSettings.m_aMRsRadioSettings)
						{
							if (m_RadioSettings.m_aMRsRadioSettings.Count() - 1 < MRIndex)
							{
								ref CVON_RadioSettingObject settings = new CVON_RadioSettingObject();
								settings.m_sFreq = freqContainer.m_aMRFrequencies.Get(MRIndex);
								m_RadioSettings.m_aMRsRadioSettings.Insert(settings);
								MRIndex++;
							}
							else
							{
								ref CVON_RadioSettingObject settings = m_RadioSettings.m_aMRsRadioSettings.Get(MRIndex);
								radioComp.m_eStereo = settings.m_Stereo;
								radioComp.m_iVolume = settings.m_iVolume;
								MRIndex++;
							}
						}
						else
						{
							ref CVON_RadioSettingObject settings = new CVON_RadioSettingObject();
							settings.m_sFreq = freqContainer.m_aMRFrequencies.Get(MRIndex);
							m_RadioSettings.m_aMRsRadioSettings.Insert(settings);
							MRIndex++;
						}
					}
						
					break;
				}
				case CVON_ERadioType.LONG:
				{
					if (!longRangeRadios.Contains(radioObject))
					{
						longRangeRadios.Insert(radioObject);
						if (System.IsConsoleApp())
							break;
						if (!freqContainer)
							break;
						if (!freqContainer.m_aLRFrequencies)
							break;
						if (freqContainer.m_aLRFrequencies.Count() < SRIndex + 1)
							break;
						if (m_RadioSettings.m_aLRRadioSettings)
						{
							if (m_RadioSettings.m_aLRRadioSettings.Count() - 1 < LRIndex)
							{
								ref CVON_RadioSettingObject settings = new CVON_RadioSettingObject();
								settings.m_sFreq = freqContainer.m_aLRFrequencies.Get(LRIndex);
								m_RadioSettings.m_aLRRadioSettings.Insert(settings);
								LRIndex++;
							}
							else
							{
								ref CVON_RadioSettingObject settings = m_RadioSettings.m_aLRRadioSettings.Get(LRIndex);
								radioComp.m_eStereo = settings.m_Stereo;
								radioComp.m_iVolume = settings.m_iVolume;
								LRIndex++;
							}
						}
						else
						{
							ref CVON_RadioSettingObject settings = new CVON_RadioSettingObject();
							settings.m_sFreq = freqContainer.m_aLRFrequencies.Get(LRIndex);
							m_RadioSettings.m_aLRRadioSettings.Insert(settings);
							LRIndex++;
						}
					}
						
					break;
				}
			}
		}
		if (shortRangeRadios)
			m_aRadios.InsertAll(shortRangeRadios);
		if (longRangeRadios)
			m_aRadios.InsertAll(longRangeRadios);
		if (mediumRangeRadios)
			m_aRadios.InsertAll(mediumRangeRadios);
		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radios.Get(0))).GetEntity();
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (GetGame().GetPlayerController())
		{
			SCR_VONController vonController = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
			vonController.m_CharacterController = SCR_CharacterControllerComponent.Cast(to.FindComponent(SCR_CharacterControllerComponent));
			radioComp.WriteJSON(to);
		}
			
	}
	
	//mmmmgetter
	//==========================================================================================================================================================================
	CVON_EVONVolume ReturnLocalVoiceRange()
	{
		return m_eVONVolume;
	}

	//Links an actual volume I can give to teamspeak to the enum in game.
	//==========================================================================================================================================================================
	void ChangeVoiceRange(int input)
	{
		if (m_eVONVolume == 0 && input == -1)
			return;
		
		if (m_eVONVolume == 4 && input == 1)
			return;
		
		m_eVONVolume += input;
		switch(m_eVONVolume)
		{
			case 0: {m_iLocalVolume = m_aVolumeValues[0];  break;}
			case 1: {m_iLocalVolume = m_aVolumeValues[1]; break;}
			case 2: {m_iLocalVolume = m_aVolumeValues[2]; break;}
			case 3: {m_iLocalVolume = m_aVolumeValues[3]; break;}
			case 4: {m_iLocalVolume = m_aVolumeValues[4]; break;}
		}
	}
	
	void OpenHandMicMenuClient(int playerId, RplId handMicEntityId)
	{
		Rpc(RpcAsk_OpenHandMicClient, playerId, handMicEntityId);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_OpenHandMicClient(int playerId, RplId handMicEntityId)
	{
		CVON_VONGameModeComponent.GetInstance().OpenHandMicServer(playerId, handMicEntityId);
	}
	
	void OpenHandMicOwner(array<string> freqs, int amountOfRadios, array<string> radioNames)
	{
		Rpc(RpcDo_OpenHandMicOwner, freqs, amountOfRadios, radioNames);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_OpenHandMicOwner(array<string> freqs, int amountOfRadios, array<string> radioNames)
	{
		SCR_VONController vonController = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
		vonController.GetVONMenu().OpenMenu(freqs, amountOfRadios, radioNames);
	}
	
	//This is how we send our VONEntry to other clients so they can write it in their VONData.json
	//==========================================================================================================================================================================
	void BroadcastLocalVONToServer(CVON_VONContainer VONContainer, array<int> playerIds, int playerId, RplId radioId)
	{
		Rpc(RpcAsk_BroadcastLocalVONToServer, VONContainer, playerIds, playerId, radioId);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_BroadcastLocalVONToServer(CVON_VONContainer VONContainer, array<int> playerIds, int playerId, RplId radioId)
	{
		CVON_VONGameModeComponent.GetInstance().AddLocalVONBroadcasts(VONContainer, playerIds, playerId, radioId);
	}
	
	//This is how the server talks directly to us, by doing an Rpc to the owner, which is the player of this controller, it sends it directly to them.
	//Performant
	//==========================================================================================================================================================================
	void AddLocalVONBroadcast(CVON_VONContainer VONContainer, int playerId, vector senderOrigin, float maxDistance)
	{
		Rpc(RpcAsk_AddLocalVONBroadcast, VONContainer, playerId, senderOrigin, maxDistance);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)] 
	void RpcAsk_AddLocalVONBroadcast(CVON_VONContainer VONContainer, int playerId, vector senderOrigin, float maxDistance)
	{
		VONContainer.m_iMaxDistance = maxDistance;
		VONContainer.m_vSenderLocation = senderOrigin;
		m_aLocalActiveVONEntries.Insert(VONContainer);
		m_aLocalActiveVONEntriesIds.Insert(playerId);
	}
	
	
	//Just tells the clients who have our VON broadcast to remove it.
	//==========================================================================================================================================================================
	void BroadcastRemoveLocalVONToServer(int playerId, int playerIdToRemove)
	{
		Rpc(RpcAsk_BroadcastRemoveLocalVONToServer, playerId, playerIdToRemove);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_BroadcastRemoveLocalVONToServer(int playerId, int playerIdToRemove)
	{
		CVON_VONGameModeComponent.GetInstance().RemoveLocalVONBroadcasts(playerId, playerIdToRemove);
	}
	
	//Same concept as above, just direct communication from server to specific client to remove a VONEntry.
	//==========================================================================================================================================================================
	void RemoveLocalVONBroadcast(int playerIdToRemove)
	{
		Rpc(RpcAsk_RemoveLocalVONBroadcast, playerIdToRemove);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)] 
	void RpcAsk_RemoveLocalVONBroadcast(int playerIdToRemove)
	{
		int index = m_aLocalActiveVONEntriesIds.Find(playerIdToRemove);
		if (index == -1)
			return;
		m_aLocalActiveVONEntries.RemoveOrdered(index);
		m_aLocalActiveVONEntriesIds.RemoveOrdered(index);
	}
	
	//All these are described already in the radio component, this is just how we get the authority to do it from the proxy.
	//==========================================================================================================================================================================
	void ToggleRadioPower(RplId radio)
	{
		Rpc(RpcAsk_ToggleRadioPower, radio);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_ToggleRadioPower(RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;
		
		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.TogglePowerServer();
	}
	
	void UpdateRadioChannel(int input, RplId radio)
	{
		Rpc(RpcAsk_UpdateRadioChannel, input, radio);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_UpdateRadioChannel(int input, RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;
		
		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.UpdateChannelServer(input);
	}
	
	//==========================================================================================================================================================================
	void UpdateRadioFrequency(string input, RplId radio)
	{
		Rpc(RpcAsk_UpdateRadioFrequency, input, radio);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_UpdateRadioFrequency(string input, RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;
		
		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.UpdateFrequncyServer(input);
	}

	//==========================================================================================================================================================================
	void UpdateRadioTimeDeviation(int input, RplId radio)
	{
		Rpc(RpcAsk_UpdateRadioTimeDeviation, input, radio);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_UpdateRadioTimeDeviation(int input, RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;
		
		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.UpdateTimeDeviationServer(input);
	}
	
	//==========================================================================================================================================================================
	void AddChannelServer(RplId radioId)
	{
		Rpc(RpcAsk_AddChannelServer, radioId);
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_AddChannelServer(RplId radioId)
	{
		if (!Replication.FindItem(radioId))
			return;
		
		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radioId)).GetEntity();
		if (!radioEntity)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.AddChannelServer();
	}
	
	//Because m_aRadios is tracked by just us and the authority, whenever we changed it the authority has to know as well.
	//==========================================================================================================================================================================
	void RotateActiveChannelServer()
	{
		Rpc(RpcAsk_RotateActiveChannelServer, SCR_PlayerController.GetLocalPlayerId());
	}
	
	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_RotateActiveChannelServer(int playerId)
	{
		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		IEntity radioEntity = playerController.m_aRadios.Get(playerController.m_aRadios.Count() - 1);
		playerController.m_aRadios.RemoveOrdered(playerController.m_aRadios.Count() - 1);
		playerController.m_aRadios.InsertAt(radioEntity, 0);
	}
	
	void GrabHandMicServer(int playerId, RplId radioId)
	{
		Rpc(RpcAsk_GrabHandMicServer, playerId, radioId);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_GrabHandMicServer(int playerId, RplId radioId)
	{
		if (!Replication.FindItem(radioId))
			return;
		
		IEntity radio = RplComponent.Cast(Replication.FindItem(radioId)).GetEntity();
		if (!radio)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
		radioComp.GrabHandMicServer(playerId);
	}
}