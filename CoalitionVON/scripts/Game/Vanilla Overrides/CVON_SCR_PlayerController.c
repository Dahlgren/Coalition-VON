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
	ref array<ref CVON_RadioSettingObject> m_aRadioSettings = {};
	
	
	
	//Used to initials the m_aRadio array
	//Delay is needed as it can take a sec for the entity to initialized for a client on the server.
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		GetGame().GetCallqueue().CallLater(InitializeRadios, 500, false, to);
	}
	
	override void OnDestroyed(notnull Instigator killer)
	{
		super.OnDestroyed(killer);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		#ifdef WORKBENCH
		#else
		if (!System.IsConsoleApp())
			return;
		#endif		
		m_aRadioSettings.Clear();
		
		foreach (IEntity radio: m_aRadios)
		{
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
			SCR_FactionManager factionMan = SCR_FactionManager.Cast(GetGame().GetFactionManager());
			if (radioComp.m_sFactionKey != "" && radioComp.m_sFactionKey != factionMan.GetPlayerFaction(GetPlayerId()).GetFactionKey())
				return;
			ref CVON_RadioSettingObject setting = new CVON_RadioSettingObject();
			setting.m_sFreq = radioComp.m_sFrequency;
			setting.m_Stereo = radioComp.m_eStereo;
			setting.m_iVolume = radioComp.m_iVolume;
			m_aRadioSettings.Insert(setting);
		}
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
		foreach (RplId radio: radios)
		{
			if (!Replication.FindItem(radio))
				continue;
			
			IEntity radioObject = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
			if (!radioObject)
				continue;
			
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioObject.FindComponent(CVON_RadioComponent));
			
			
			switch (radioComp.m_eRadioType)
			{
				case CVON_ERadioType.SHORT:
				{
					if (!shortRangeRadios.Contains(radioObject))
						shortRangeRadios.Insert(radioObject);
					break;
				}
				case CVON_ERadioType.LONG:
				{
					if (!longRangeRadios.Contains(radioObject))
						longRangeRadios.Insert(radioObject);
					break;
				}	
			}
		}
		if (shortRangeRadios)
			m_aRadios.InsertAll(shortRangeRadios);
		if (longRangeRadios)
			m_aRadios.InsertAll(longRangeRadios);
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
	
	void SetVolumeFromServer(int volume, int radioIndex)
	{
		Rpc(RpcDo_SetVolumeFromServer, volume, radioIndex);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_SetVolumeFromServer(int volume, int radioIndex)
	{
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_aRadios.Get(radioIndex).FindComponent(CVON_RadioComponent));
		radioComp.m_iVolume = volume;
		radioComp.WriteJSON(GetLocalControlledEntity());
	}
	
	void SetStereoFromServer(CVON_EStereo stereo, int radioIndex)
	{
		Rpc(RpcDo_SetStereoFromServer, stereo, radioIndex);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_SetStereoFromServer(CVON_EStereo stereo, int radioIndex)
	{
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_aRadios.Get(radioIndex).FindComponent(CVON_RadioComponent));
		radioComp.m_eStereo = stereo;
		radioComp.WriteJSON(GetLocalControlledEntity());
	}
}