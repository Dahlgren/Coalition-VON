modded class SCR_GroupsManagerComponent
{
	//Called when spawning in. All this handles automatically assigning frequencies when you join a group.
	//==========================================================================================================================================================================
	override void TunePlayersFrequency(int playerId, IEntity player)
	{
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
		GetGame().GetCallqueue().CallLater(TuneFreqDelay, 500, false, playerId, player);
	}
	
	//Needed so we wait for the group to initialize
	//==========================================================================================================================================================================
	void TuneFreqDelay(int playerId, IEntity player)
	{
		Print("tuning");
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
	
		Print(playerController.m_aRadios.Count());
		if (playerFaction.GetCallsignInfo().m_aGroupFrequency.Count() < index + 1)
			return;		
		CRF_GroupFrequencyContainer freqContainer = playerFaction.GetCallsignInfo().m_aGroupFrequency.Get(index);
		for (int i = 0; i < playerController.m_aRadios.Count(); i++)
		{
			CRF_RadioComponent radioComp = CRF_RadioComponent.Cast(playerController.m_aRadios.Get(i).FindComponent(CRF_RadioComponent));
			switch (i)
			{
				case 0: 
				{
					if (!radioComp.m_aChannels.Contains(freqContainer.m_sSRFrequency))
						break;
					radioComp.UpdateFrequncyServer(freqContainer.m_sSRFrequency);
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freqContainer.m_sSRFrequency) + 1);
					break;
				}
				case 1: 
				{
					if (!radioComp.m_aChannels.Contains(freqContainer.m_sLRFrequency))
						break;
					radioComp.UpdateFrequncyServer(freqContainer.m_sLRFrequency);
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freqContainer.m_sLRFrequency) + 1);
					break;
				}
				case 2: 
				{
					if (!radioComp.m_aChannels.Contains(freqContainer.m_sMRFrequency))
						break;
					radioComp.UpdateFrequncyServer(freqContainer.m_sMRFrequency);
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freqContainer.m_sMRFrequency) + 1);
					break;
				}
			}
			
		}
	}
}