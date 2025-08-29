modded class SCR_GroupsManagerComponent
{
	//Called when spawning in. All this handles automatically assigning frequencies when you join a group.
	//==========================================================================================================================================================================
	override void TunePlayersFrequency(int playerId, IEntity player)
	{
		Print("TUNING  PLAYER");
		GetGame().GetCallqueue().CallLater(TuneFreqDelay, 500, false, playerId, player);
	}
	
	//Needed as if you character has already been spawned and you make a new group this is what it calls, because YES!
	//==========================================================================================================================================================================
	override private void TuneAgentsRadio(AIAgent agentEntity)
	{
		IEntity player = agentEntity.GetControlledEntity();
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(player);
		if (playerId == -1)
			return;
		Print("TUNING AGENT");
		GetGame().GetCallqueue().CallLater(TuneFreqDelay, 500, false, playerId, player);
	}
	
	//Needed so we wait for the group to initialize
	//==========================================================================================================================================================================
	void TuneFreqDelay(int playerId, IEntity player)
	{
		if (!player)
			return;
		
		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!playerController)
			return;
		
		SCR_Faction playerFaction = SCR_Faction.Cast(SCR_FactionManager.Cast(GetGame().GetFactionManager()).GetPlayerFaction(playerId));
		
		array<SCR_AIGroup> groups = GetPlayableGroupsByFaction(playerFaction);
		SCR_AIGroup playersGroup = GetPlayerGroup(playerId);
		int index = groups.Find(playersGroup);
		if (index == -1)
			return;
	
		if (playerFaction.GetCallsignInfo().m_aGroupFrequency.Count() < index + 1)
			return;		
		int SRIndex = 0;
		int MRIndex = 0;
		int LRIndex = 0;
		CVON_GroupFrequencyContainer freqContainer = playerFaction.GetCallsignInfo().m_aGroupFrequency.Get(index);
		for (int i = 0; i < playerController.m_aRadios.Count(); i++)
		{
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(playerController.m_aRadios.Get(i).FindComponent(CVON_RadioComponent));
			switch (radioComp.m_eRadioType)
			{
				case 0: 
				{
					if (!freqContainer.m_aSRFrequencies)
						break;
					if (freqContainer.m_aSRFrequencies.Count() < SRIndex + 1)
						break;
					string freq = freqContainer.m_aSRFrequencies.Get(SRIndex);
					if (!radioComp.m_aChannels.Contains(freq))
						break;
					SRIndex++;
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freq) + 1);
					radioComp.UpdateFrequncyServer(freq);
					break;
				}
				case 1: 
				{
					if (!freqContainer.m_aMRFrequencies)
						break;
					if (freqContainer.m_aMRFrequencies.Count() < SRIndex + 1)
						break;
					string freq = freqContainer.m_aMRFrequencies.Get(MRIndex);
					if (!radioComp.m_aChannels.Contains(freq))
						break;
					MRIndex++;
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freq) + 1);
					radioComp.UpdateFrequncyServer(freq);
					break;
				}
				case 2: 
				{
					if (!freqContainer.m_aLRFrequencies)
						break;
					if (freqContainer.m_aLRFrequencies.Count() < SRIndex + 1)
						break;
					string freq = freqContainer.m_aLRFrequencies.Get(LRIndex);
					if (!radioComp.m_aChannels.Contains(freq))
						break;
					LRIndex++;
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freq) + 1);
					radioComp.UpdateFrequncyServer(freq);
					break;
				}
			}
		}
	}
}