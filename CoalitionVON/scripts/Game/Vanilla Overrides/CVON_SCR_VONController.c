//What type of transmission we are trying to send so we can send the right data to the other clients.
enum CVON_EVONTransmitType
{
	NONE,
	DIRECT,
	SR,
	LR,
	MR
}

modded class SCR_VONController
{
	static const int CVON_DB_ATTEN_VEHICLE  = -18; // speaker inside vehicle
	static const int CVON_DB_ATTEN_BUILDING = -12; // speaker inside building
	
	//MMMM POINTER
	SCR_PlayerController m_PlayerController;
	
	//MMM... stores the gamemode
	CVON_VONGameModeComponent m_VONGameModeComponent;
	PlayerManager m_PlayerManager;
	
	//Who we are currently broadcasting to, this is how we know who we have to send remove calls to
	ref array<int> m_aPlayerIdsBroadcastedTo = {};
	
	//What is our current broadcast
	ref CVON_VONContainer m_CurrentVONContainer = null;
	
	//Am I broadcasting
	bool m_bIsBroadcasting = false;
	
	//Both are used so we can toggle our direct voice and use a radio at the same time
	bool m_bToggleBuffer = false;
	bool m_bToggleTurnedOffByRadio = false;
	
	//Stores the HUD so we can deactivate it after a transmissions end
	CVON_HUD m_VONHud;
	
	//Used to check life state for VON
	SCR_CharacterControllerComponent m_CharacterController;
	
	//Used if we are warning the player their VON is not connected after initial connection
	bool m_bShowingSecondWarning = false;
	
	
	//All these below are just how we assign keybinds to activate certain VON Transmissions
	//==========================================================================================================================================================================
	void ActivateCRFDirect()
	{
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCRFVON();
			m_VONHud.ShowDirectToggle();
			return;
		}
		ActivateCRFVON(CVON_EVONTransmitType.DIRECT);
	}
	
	//==========================================================================================================================================================================
	void ToggleCRFDirect()
	{
		m_bToggleBuffer = !m_bToggleBuffer;
		m_VONHud.ShowDirectToggle();
		ActivateCRFVON(CVON_EVONTransmitType.DIRECT);
	}
	
	//==========================================================================================================================================================================
	void ActivateCRFSR()
	{	
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCRFVON();
			m_VONHud.ShowDirectToggle();
			m_bToggleTurnedOffByRadio = true;
		}
		ActivateCRFVON(CVON_EVONTransmitType.SR);
	}
	
	//==========================================================================================================================================================================
	void ActivateCRFLR()
	{
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCRFVON();
			m_VONHud.ShowDirectToggle();
			m_bToggleTurnedOffByRadio = true;
		}
		ActivateCRFVON(CVON_EVONTransmitType.LR);
	}
	
	//==========================================================================================================================================================================
	void ActivateCRFMR()
	{
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCRFVON();
			m_VONHud.ShowDirectToggle();
			m_bToggleTurnedOffByRadio = true;
		}
		ActivateCRFVON(CVON_EVONTransmitType.MR);
	}
	
	//How we rotate what radio is assigned to caps-lock, aka active.
	//==========================================================================================================================================================================
	void RotateActiveRadio()
	{
		int count = m_PlayerController.m_aRadios.Count();
		if (count < 2) return;
	
		IEntity last = m_PlayerController.m_aRadios[count - 1];
	
	    for (int i = count - 1; i > 0; i--)
	    {
	        m_PlayerController.m_aRadios[i] = m_PlayerController.m_aRadios[i - 1];
	    }
	    m_PlayerController.m_aRadios[0] = last;
	
	    m_PlayerController.RotateActiveChannelServer();
	}
	
	//! Initialize component, done once per controller
	//==========================================================================================================================================================================
	override protected void Init(IEntity owner)
	{	
		if (s_bIsInit || System.IsConsoleApp())	// hosted server will have multiple controllers, init just the first one // dont init on dedicated server
		{
			Deactivate(owner);
			return;
		}
		
		UpdateUnconsciousVONPermitted();

		m_InputManager = GetGame().GetInputManager();
		if (m_InputManager)
		{
			m_InputManager.AddActionListener(ACTION_DIRECT, EActionTrigger.DOWN, ActivateCRFDirect);
			m_InputManager.AddActionListener(ACTION_DIRECT, EActionTrigger.UP, DeactivateCRFVON);
			m_InputManager.AddActionListener(ACTION_DIRECT_TOGGLE, EActionTrigger.DOWN, ToggleCRFDirect);
			m_InputManager.AddActionListener(ACTION_CHANNEL, EActionTrigger.DOWN, ActivateCRFSR);
			m_InputManager.AddActionListener(ACTION_CHANNEL, EActionTrigger.UP, DeactivateCRFVON);
			m_InputManager.AddActionListener(ACTION_TRANSCEIVER_CYCLE, EActionTrigger.DOWN, ActionVONTransceiverCycle);
			m_InputManager.AddActionListener("VONLongRange", EActionTrigger.DOWN, ActivateCRFLR);
			m_InputManager.AddActionListener("VONLongRange", EActionTrigger.UP, DeactivateCRFVON);
			m_InputManager.AddActionListener("VONMediumRange", EActionTrigger.DOWN, ActivateCRFMR);
			m_InputManager.AddActionListener("VONMediumRange", EActionTrigger.UP, DeactivateCRFVON);
			m_InputManager.AddActionListener("VONRotateActive", EActionTrigger.DOWN, RotateActiveRadio);
			m_InputManager.AddActionListener("VONChannelUp", EActionTrigger.DOWN, ChannelUp);
			m_InputManager.AddActionListener("VONChannelDown", EActionTrigger.DOWN, ChannelDown);
			m_InputManager.AddActionListener("VONOpenActive", EActionTrigger.DOWN, OpenActiveRadio);
			m_InputManager.AddActionListener("VONRadioEarRight", EActionTrigger.DOWN, SetActiveEarRight);
			m_InputManager.AddActionListener("VONRadioEarLeft", EActionTrigger.DOWN, SetActiveEarLeft);
			m_InputManager.AddActionListener("VONRadioEarBoth", EActionTrigger.DOWN, SetActiveEarBoth);
		}

		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetOwner());
		if (playerController)
		{
			OnControlledEntityChanged(null, playerController.GetControlledEntity());
			playerController.m_OnControlledEntityChanged.Insert(OnControlledEntityChanged);
		}

		PauseMenuUI.m_OnPauseMenuOpened.Insert(OnPauseMenuOpened);
		PauseMenuUI.m_OnPauseMenuClosed.Insert(OnPauseMenuClosed);
		
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.GetOnPlayerDeleted().Insert(OnPlayerDeleted);
		
		m_DirectSpeechEntry = new SCR_VONEntry(); // Init direct speech entry
		
		ConnectToHandleUpdateVONControllersSystem();
		
		s_bIsInit = true;
		
		if (m_VONMenu)
			m_VONMenu.Init(this);
		
		UpdateSystemState();
		
		GetGame().GetCallqueue().CallLater(GetHud, 500, false);
	}
	
	//Change ear keybind
	//==========================================================================================================================================================================
	void SetActiveEarRight()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		radioComp.m_eStereo = CVON_EStereo.RIGHT;
		switch (radioComp.m_eRadioType)
		{
			case CVON_ERadioType.SHORT: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
			case CVON_ERadioType.MEDIUM: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
			case CVON_ERadioType.LONG: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
		}
		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		m_VONHud.ShowVONChange();
	}
	
	//Change ear keybind
	//==========================================================================================================================================================================
	void SetActiveEarLeft()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		radioComp.m_eStereo = CVON_EStereo.LEFT;
		switch (radioComp.m_eRadioType)
		{
			case CVON_ERadioType.SHORT: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
			case CVON_ERadioType.MEDIUM: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
			case CVON_ERadioType.LONG: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
		}
		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		m_VONHud.ShowVONChange();
	}
	
	//Change ear keybind
	//==========================================================================================================================================================================
	void SetActiveEarBoth()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		radioComp.m_eStereo = CVON_EStereo.BOTH;
		switch (radioComp.m_eRadioType)
		{
			case CVON_ERadioType.SHORT: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aSRRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
			case CVON_ERadioType.MEDIUM: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aMRsRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
			case CVON_ERadioType.LONG: 
			{
				for (int i = 0; i < m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Count(); i++)
				{
					if (m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Get(i).m_sFreq != radioComp.m_sFrequency)
						continue;
					
					m_PlayerController.m_RadioSettings.m_aLRRadioSettings.Get(i).m_Stereo = radioComp.m_eStereo; 
				}
				break;
			}
		}
		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		m_VONHud.ShowVONChange();
	}
	
	//Keybind to open active radio
	//==========================================================================================================================================================================
	void OpenActiveRadio()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		radioComp.OpenMenu();
	}
	
	//Used as input
	//==========================================================================================================================================================================
	void ChannelUp()
	{
		ChangeChannel(1);
	}
	
	//used as input
	//==========================================================================================================================================================================
	void ChannelDown()
	{
		ChangeChannel(-1);
	}
	
	//Used for changing channel keybind to rotate through channels without opening radio
	//==========================================================================================================================================================================
	void ChangeChannel(int input)
	{
		array<IEntity> radios = m_PlayerController.m_aRadios;
		if (radios.Count() == 0)
			return;
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radios.Get(0).FindComponent(CVON_RadioComponent));
		int channelCount = radioComp.m_aChannels.Count();
		if (channelCount < 2)
			return;

		if (radioComp.m_iCurrentChannel == 1 && input == -1)
		{
			radioComp.m_iCurrentChannel = radioComp.m_aChannels.Count();
			radioComp.UpdateChannelClient(radioComp.m_aChannels.Count());
		}
		else if (radioComp.m_iCurrentChannel == 99 && input == 1)
		{
			radioComp.m_iCurrentChannel = 1;
			radioComp.UpdateChannelClient(1);
		}
		else
		{
			radioComp.m_iCurrentChannel += input;
			radioComp.UpdateChannelClient(radioComp.m_iCurrentChannel);
		}
		
		if (radioComp.m_iCurrentChannel > channelCount)
		{
			radioComp.m_iCurrentChannel = 1;
			radioComp.UpdateChannelClient(1);
		}
		string freq = radioComp.m_aChannels.Get(radioComp.m_iCurrentChannel - 1);
		radioComp.m_sFrequency = freq;
		radioComp.UpdateFrequencyClient(freq);
		m_VONHud.ShowVONChange();
	}
	
	//Fetches the VON HUD, delay is necessary.
	//==========================================================================================================================================================================
	void GetHud()
	{
		ref array<BaseInfoDisplay> displays = {};
		GetGame().GetHUDManager().GetInfoDisplays(displays);
		foreach (BaseInfoDisplay display: displays)
		{
			if (!CVON_HUD.Cast(display))
				continue;
			
			m_VONHud = CVON_HUD.Cast(display);
		}
	}
	
	//No more base game VON
	//==========================================================================================================================================================================
	override protected bool ActivateVON(notnull SCR_VONEntry entry, EVONTransmitType transmitType = EVONTransmitType.NONE)
	{
		return false;
	}
	
	//This builds our VON Container with all the data we need to send to other clients based on the data sent out. This starts the talking process.
	//==========================================================================================================================================================================
	void ActivateCRFVON(CVON_EVONTransmitType transmitType = CVON_EVONTransmitType.NONE)
	{
		#ifdef WORKBENCH
		#else
		if (m_PlayerController.m_iTeamSpeakClientId == 0 && m_VONGameModeComponent.m_bTeamspeakChecks)
			return;
		#endif
		if (!SCR_PlayerController.GetLocalControlledEntity())
			return;
		if (m_CharacterController.GetLifeState() != ECharacterLifeState.ALIVE)
			return;
		if (m_CurrentVONContainer)
			DeactivateCRFVON();
		CVON_VONContainer container = new CVON_VONContainer();
		if (transmitType == CVON_EVONTransmitType.NONE)
			return;
		if (transmitType == CVON_EVONTransmitType.DIRECT)
			container.m_eVonType = CVON_EVONType.DIRECT;
		else if (transmitType == CVON_EVONTransmitType.SR || transmitType == CVON_EVONTransmitType.LR || transmitType == CVON_EVONTransmitType.MR)
			container.m_eVonType = CVON_EVONType.RADIO;
		
		container.m_iVolume = m_PlayerController.m_iLocalVolume;
		container.m_SenderRplId = RplComponent.Cast(SCR_PlayerController.GetLocalControlledEntity().FindComponent(RplComponent)).Id();
		container.m_iClientId = m_PlayerController.m_iTeamSpeakClientId;
		if (container.m_eVonType == CVON_EVONType.RADIO)
		{
			switch (transmitType)
			{
				case CVON_EVONTransmitType.SR:
				{
					if (m_PlayerController.m_aRadios.Count() < 1)
						return;
					IEntity radio = m_PlayerController.m_aRadios.Get(0);
					if (radio == null)
						return;
					
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					container.m_sFrequency = radioComp.m_sFrequency;
					container.m_iRadioId = RplComponent.Cast(radio.FindComponent(RplComponent)).Id();
					container.m_sFactionKey = radioComp.m_sFactionKey;
					break;
				}
				case CVON_EVONTransmitType.LR:
				{
					if (m_PlayerController.m_aRadios.Count() < 2)
						return;
					IEntity radio = m_PlayerController.m_aRadios.Get(1);
					if (radio == null)
						return;
					
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					container.m_sFrequency = radioComp.m_sFrequency;
					container.m_iRadioId = RplComponent.Cast(radio.FindComponent(RplComponent)).Id();
					container.m_sFactionKey = radioComp.m_sFactionKey;
					break;
				}
				case CVON_EVONTransmitType.MR:
				{
					if (m_PlayerController.m_aRadios.Count() < 3)
						return;
					IEntity radio = m_PlayerController.m_aRadios.Get(2);
					if (radio == null)
						return;
					
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					container.m_sFrequency = radioComp.m_sFrequency;
					container.m_iRadioId = RplComponent.Cast(radio.FindComponent(RplComponent)).Id();
					container.m_sFactionKey = radioComp.m_sFactionKey;
					break;
				}
			}
			AudioSystem.PlaySound("{E3B4231783ABA914}UI/sounds/beepstart.wav");
		}
		m_CurrentVONContainer = container;
		m_bIsBroadcasting = true;
	}
	
	//No more base game VON
	//==========================================================================================================================================================================
	override protected void DeactivateVON(EVONTransmitType transmitType = EVONTransmitType.NONE)
	{
		return;
	}
	
	//Stops talking and removes our VON entry from all others players that we where broadcasting to.
	//==========================================================================================================================================================================
	void DeactivateCRFVON()
	{
		if (m_bToggleBuffer)
			return;
		if (!m_VONGameModeComponent)
			return;
		if (!m_CurrentVONContainer)
			return;
		if (m_CurrentVONContainer.m_eVonType == CVON_EVONType.RADIO)
		{
			AudioSystem.PlaySound("{B826EAACD5F6B6BB}UI/sounds/beepend.wav");
			m_VONHud.HideVON();
		}
		else
			m_VONHud.HideDirect();
			
		m_bIsBroadcasting = false;
		m_CurrentVONContainer = null;
		if (m_aPlayerIdsBroadcastedTo.Count() > 0)
		{
			foreach (int playerId: m_aPlayerIdsBroadcastedTo)
			{
				m_PlayerController.BroadcastRemoveLocalVONToServer(playerId, SCR_PlayerController.GetLocalPlayerId());
			}
			m_aPlayerIdsBroadcastedTo.Clear();
		}
		
		if (m_bToggleTurnedOffByRadio)
		{
			m_bToggleTurnedOffByRadio = false;
			ToggleCRFDirect();
		}
	}
	
	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (System.IsConsoleApp())
			return;
		SetEventMask(owner, EntityEvent.FIXEDFRAME);
	}
	
	
	//The meat, this is where we determine who we send a VONEntry too and if we've already sent one.
	//Differentiates behavior for Direct and Radio in here as well. If a player is more than 200m away and you try to use direct he will not receive that direct VONEntry.
	//==========================================================================================================================================================================
	float m_bWriteJsonCooldown = 0;
	override void EOnFixedFrame(IEntity owner, float timeSlice)
	{
		super.EOnFixedFrame(owner, timeSlice);
		if (!m_PlayerController)
		{
			m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		}
		if (!m_CharacterController)
			if (SCR_PlayerController.GetLocalControlledEntity())
				m_CharacterController = SCR_CharacterControllerComponent.Cast(SCR_PlayerController.GetLocalControlledEntity().FindComponent(SCR_CharacterControllerComponent));
		if (!m_VONGameModeComponent)
			m_VONGameModeComponent = CVON_VONGameModeComponent.GetInstance();
		if (!m_PlayerManager)
			m_PlayerManager = GetGame().GetPlayerManager();
		
		int maxDistance = 150;
		foreach (CVON_VONContainer container: m_PlayerController.m_aLocalActiveVONEntries)
		{
			if (!container.m_SoundSource)
				continue;
			
			float distance = vector.Distance(container.m_SoundSource.GetOrigin(), SCR_PlayerController.GetLocalControlledEntity().GetOrigin());
			if (distance < maxDistance)
				container.m_fDistanceToSender = distance;
			else
				container.m_fDistanceToSender = -1;
		}
		
		if (m_bIsBroadcasting)
		{
			if (m_CharacterController.GetLifeState() != ECharacterLifeState.ALIVE)
			{
				if (m_bToggleBuffer)
				{
					m_bToggleBuffer = false;
					DeactivateCRFVON();
					m_VONHud.DirectToggleDelay();
				}
				else
					DeactivateCRFVON();
				return;
			}
				
			ref array<int> playerIds = {};
			ref array<int> broadcastToPlayerIds = {};
			m_PlayerManager.GetPlayers(playerIds);
			foreach (int playerId: playerIds)
			{	
				#ifdef WORKBENCH
				#else
				if (playerId == SCR_PlayerController.GetLocalPlayerId())
					continue;
				#endif
				
				if (m_CurrentVONContainer.m_eVonType == CVON_EVONType.DIRECT)
				{
					IEntity player = m_PlayerManager.GetPlayerControlledEntity(playerId);
					if (!player)
						continue;

					if (vector.Distance(player.GetOrigin(), SCR_PlayerController.GetLocalControlledEntity().GetOrigin()) > maxDistance)
					{
						if (m_aPlayerIdsBroadcastedTo.Contains(playerId))
						{
							m_aPlayerIdsBroadcastedTo.RemoveItem(playerId);
							m_PlayerController.BroadcastRemoveLocalVONToServer(playerId, SCR_PlayerController.GetLocalPlayerId());
						}
						continue;
					}
				}
				
				if (m_aPlayerIdsBroadcastedTo.Contains(playerId))
					continue;
				
				broadcastToPlayerIds.Insert(playerId);
				m_aPlayerIdsBroadcastedTo.Insert(playerId);
			}
			if (broadcastToPlayerIds.Count() > 0)
			{
				if (m_CurrentVONContainer.m_eVonType == CVON_EVONType.DIRECT)
					m_PlayerController.BroadcastLocalVONToServer(m_CurrentVONContainer, broadcastToPlayerIds, SCR_PlayerController.GetLocalPlayerId(), RplId.Invalid());
				else
					m_PlayerController.BroadcastLocalVONToServer(m_CurrentVONContainer, broadcastToPlayerIds, SCR_PlayerController.GetLocalPlayerId(), m_CurrentVONContainer.m_iRadioId);
			}
				
		}
		WriteJSON();
	}
	
	//Thank god for CHATGPT
	//Computes how much of the direct voice you hear in your left ear vs your right ear.
	// Stereo spatializer (no distance falloff)
	// Geometry only: pan + rear shadow + elevation + bleed.
	// Multiply the returned L/R by your own plugin volume afterward.
	//==========================================================================================================================================================================
	static const float SPEECH_NORMAL_M       = 15.0;   // "normal talk" meters
	static const float INTENSITY_FADE_FACTOR = 0.6;    // how quickly yelling falls off
	static const float MAX_OUT_GAIN          = 2.0;    // safety cap; raise or set -1 for no cap
	
	static float LoudnessIntensity(float volume_m, float distance_m)
	{
	    if (volume_m <= 0.01) volume_m = 0.01;
	    if (distance_m <  0.0) distance_m = 0.0;
	
	    // How "loud" compared to normal talk (15 m):
	    float intent = volume_m / SPEECH_NORMAL_M;       // e.g. 40/15 ≈ 2.67 for screaming
	    float base   = Math.Pow(intent, 0.5);            // gentle growth (sqrt): 1..~1.63
	
	    // Fade that extra loudness with distance, scaled by the loudness range itself
	    float denom = INTENSITY_FADE_FACTOR * volume_m;  // larger volume_m → longer fade
	    if (denom < 0.01) denom = 0.01;
	    float x = distance_m / denom;
		float fade = ExpNegFast(x);
	
	    return 1.0 + (base - 1.0) * fade;                // 1.0 at normal; >1 near loud sources
	}
	
	// Approx exp(-x) ~ (1 + 0.5*x + 0.12*x*x) / (1 + x + 0.48*x*x)
	// Max rel. error ~1–2% on [0, 8]
	static float ExpNegFast(float x)
	{
	    float x2 = x * x;
	    return (1.0 + 0.5 * x + 0.12 * x2) / (1.0 + x + 0.48 * x2);
	}
	
	// 0 dB at d=0, −45 dB at d=inaudible_m (volume_m).
	static float AttenuationDb(float d_m, float inaudible_m, float shapeExp = 1.6)
	{
	    if (inaudible_m <= 0.01) inaudible_m = 0.01;
	    if (d_m <= 0.0)          return 0.0;
	
	    float x = d_m / inaudible_m;        // 0..1
	    if (x >= 1.0) return -45.0;
	    float db = -45.0 * Math.Pow(x, shapeExp);
	
	    if (db >  0.0)  db = 0.0;
	    if (db < -45.0) db = -45.0;
	    return db;
	}
	
	static float GainFromDb(float db)
	{
	    return Math.Pow(10.0, db / 20.0);
	}
		
	// --- Stereo with front/back cues + −45 dB distance law + LoudnessIntensity ---
	void ComputeStereoLR(
	    IEntity listener,
	    vector  sourcePos,
	    float   volume_m,            // interpret as the inaudible distance (≈ −45 dB)
	    out float outLeft,
	    out float outRight,
	    out int  silencedDecibels = 0,
	    float   rearPanBoost   = 0.55,
	    float   rearShadow     = 0.12,
	    float   elevNarrow     = 0.25,
	    float   bleed          = 0.10,
	    bool    normalizePeak  = true
	)
	{
	    // ---- Listener pose ----
	    vector Lpos  = listener.GetOrigin();
	    vector Right = listener.GetTransformAxis(0); // +X
	    vector Up    = listener.GetTransformAxis(1); // +Y
	    vector Fwd   = listener.GetTransformAxis(2); // +Z
	
	    // ---- Direction ----
	    vector toSrc = sourcePos - Lpos;
	    float  dist  = toSrc.Length();
	    if (dist < 0.0001) dist = 0.0001;
	    vector dir   = toSrc / dist;
	
	    // ---- Azimuth (project out elevation) ----
	    vector horiz = dir - Up * vector.Dot(dir, Up);
	    float  hlen  = horiz.Length();
	    if (hlen < 0.0001) { horiz = Fwd; hlen = 1.0; }
	    horiz /= hlen;
	
	    // ---- Pan & front/back cues ----
	    float pan    = Math.Clamp(vector.Dot(horiz, Right), -1.0, 1.0);
	    float front  = Math.Clamp(vector.Dot(horiz, Fwd),   -1.0, 1.0);
	    float back01 = Math.Pow(Math.Max(0.0, -front), 1.4);
	
	    // Boost panning behind to help "behindness"
	    float panScale = 1.0 + rearPanBoost * back01;
	    pan = Math.Clamp(pan * panScale, -1.0, 1.0);
	
	    // Narrow width when high/low
	    float elevAbs  = Math.AbsFloat(vector.Dot(dir, Up)); // 0..1
	    float width    = 1.0 - elevNarrow * elevAbs;
	    pan = Math.Clamp(pan * width, -1.0, 1.0);
	
	    // ---- Equal-power pan ----
	    float L = Math.Sqrt(0.5 * (1.0 - pan));
	    float R = Math.Sqrt(0.5 * (1.0 + pan));
	
	    // ---- Cross-feed bleed ----
	    float Lb = L, Rb = R;
	    L = (1.0 - bleed) * Lb + bleed * Rb;
	    R = (1.0 - bleed) * Rb + bleed * Lb;
	
	    // ---- Peak-normalize AFTER bleed (so center ≈ 1/1 per ear) ----
	    if (normalizePeak) {
	        float peak = Math.Max(L, R);
	        if (peak > 0.0001) {
	            float s = 1.0 / peak;
	            L *= s;
	            R *= s;
	        }
	    }
	
	    // ---- Rear shadow (linear softening behind) ----
	    float rearAtt = 1.0 - rearShadow * back01;
	
	    // ---- Baseline distance law (−45 dB at volume_m)
	    float distDb   = AttenuationDb(dist, volume_m, 1.6);
	
	    // If you have occlusion, subtract it here in dB BEFORE converting to linear:
	    // distDb -= silencedDecibels; // set silencedDecibels elsewhere
	
	    float distGain = GainFromDb(distDb);
	
	    // ---- Loudness push (near-field boost that fades → 1.0 with distance)
	    float loud = LoudnessIntensity(volume_m, dist);
	
	    // ---- Final per-ear gains
	    float gain = rearAtt * distGain * loud;
	    outLeft  = L * gain;
	    outRight = R * gain;
	
	    if (MAX_OUT_GAIN > 0.0) {
	        outLeft  = Math.Clamp(outLeft,  0.0, MAX_OUT_GAIN);
	        outRight = Math.Clamp(outRight, 0.0, MAX_OUT_GAIN);
	    }
	}
	
	// Convert attenuation in dB → linear (treats +/−dB the same)
	static float AttenDbToLin(float dB)
	{
	    float a = Math.AbsFloat(dB);
	    return Math.Pow(10.0, -a / 20.0);
	}
	
	//Also bless ChatGPT, handles the arcade signal calulations.
	// distance: current distance from transmitter
	// maxDist: effective range
	// Returns: signal strength (0.0–1.0)
	//==========================================================================================================================================================================
	float GetSignalStrength(float d, float maxRangeMeters)
	{
	    if (d <= 0)                return 1.0;
	    if (d >= maxRangeMeters)   return 0.0;
	
	    float x = d / maxRangeMeters; // 0..1
	    float res;
	
	    if (x <= 0.2) {
	        float t = x / 0.2;                // 0..1 over [0.0, 0.2]
	        res = 1.0 + t * (0.9 - 1.0);      // 1.0 -> 0.9
	    }
	    else if (x <= 0.4) {
	        float t = (x - 0.2) / 0.2;        // 0..1 over (0.2, 0.4]
	        res = 0.9 + t * (0.8 - 0.9);      // 0.9 -> 0.8
	    }
	    else if (x <= 0.6) {
	        float t = (x - 0.4) / 0.2;        // 0..1 over (0.4, 0.6]
	        res = 0.8 + t * (0.5 - 0.8);      // 0.8 -> 0.5
	    }
	    else if (x <= 0.8) {
	        float t = (x - 0.6) / 0.2;        // 0..1 over (0.6, 0.8]
	        res = 0.5 + t * (0.3 - 0.5);      // 0.5 -> 0.3
	    }
	    else {
	        float t = (x - 0.8) / 0.2;        // 0..1 over (0.8, 1.0]
	        res = 0.3 + t * (0.0 - 0.3);      // 0.3 -> 0.0
	    }
	
	    // clamp to [0,1]
	    if (res < 0.0) res = 0.0;
	    if (res > 1.0) res = 1.0;
	    return res;
	}
	
	vector GetHeadHeight(IEntity entity)
	{
		Animation anim = entity.GetAnimation();
		TNodeId headIndex = anim.GetBoneIndex("head");
		vector matPos[4];
		anim.GetBoneMatrix(headIndex, matPos);
		return entity.CoordToParent(matPos[3]);
		
	}
	
	bool IsInBuildingOrVehicle(IEntity senderEntity, out IEntity building = null)
	{
		
		autoptr TraceParam p = new TraceParam;
		vector end = senderEntity.GetOrigin();
		end[1] = end[1] + 100;
		p.Exclude = senderEntity;
		p.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
		p.Start = GetHeadHeight(senderEntity);
		p.End = end;
		float distance = GetGame().GetWorld().TraceMove(p, null);
		if (p.TraceEnt == null)
			return false;
		
		building = p.TraceEnt;
		return true;
	}
	
	bool CanPlayerSeeSender(IEntity senderEntity)
	{
		
		autoptr TraceParam p = new TraceParam;
		IEntity player = SCR_PlayerController.GetLocalControlledEntity();
		vector end = senderEntity.GetOrigin();
		p.Exclude = player;
		p.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
		p.Start = GetHeadHeight(senderEntity);
		p.End = GetHeadHeight(player);
		float distance = GetGame().GetWorld().TraceMove(p, null);
		if (distance == 1)
			return true;
		return false;
	}
	
	void DetermineHearingWindow(IEntity entity, out float top, out float bottom)
	{
		autoptr TraceParam p = new TraceParam;
		vector end = entity.GetOrigin();
		end[1] = end[1] + 100;
		p.Exclude = entity;
		p.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
		p.Start = GetHeadHeight(entity);
		p.End = end;
		float distanceUp = GetGame().GetWorld().TraceMove(p, null) * 100;
		
		end[1] = end[1] + -200;
		p.End = end;
		float distanceDown = GetGame().GetWorld().TraceMove(p, null) * 100;
		vector origin = GetHeadHeight(entity);
		top = origin[1] + distanceUp;
		bottom = origin[1] - distanceDown;
	}
	
	bool ShouldMuffleAudio(IEntity senderEntity, out int loweredDecibles = 0)
	{
		if (CanPlayerSeeSender(senderEntity))
			return false;
		
		IEntity player = SCR_PlayerController.GetLocalControlledEntity();
		IEntity receiverBuilding;
		IEntity senderBuilding;
		bool isSenderInBuilding = IsInBuildingOrVehicle(senderEntity, senderBuilding);
		bool isPlayerInBuilding = IsInBuildingOrVehicle(player, receiverBuilding);
		
		if (!isSenderInBuilding && !isPlayerInBuilding)
			return false;
		
		if (isPlayerInBuilding != isSenderInBuilding)
		{
			loweredDecibles = CVON_DB_ATTEN_BUILDING;
			return true;
		}
		
		if (senderBuilding != receiverBuilding)
		{
			loweredDecibles = CVON_DB_ATTEN_BUILDING * 2;
			return true;
		}
		float top;
		float bottom;
		
		DetermineHearingWindow(player, top, bottom);
		vector senderOrigin = GetHeadHeight(senderEntity);
		if (senderOrigin[1] > top || senderOrigin[1] < bottom)
		{
			loweredDecibles = CVON_DB_ATTEN_BUILDING;
			return true;
		}
		return false;
	}

	
	//VONServerData.JSON
	//InGame, this is how teamspeak knows you are in game and need to be moved to the VON Channel.
	//This JSON handles everything we get from the server and teamspeak, so our TSCLientID we use to send to other clients so they know its us talking in TS.
	//VONChannelName and password so we know which channel to connect to and how to join if it has a password.
	//VONServerData
	//This iterates over all the entries in m_aLocalActiveVONEntries in our player controlller
	//ISTransmitting is how we communicate to TS that we need to open our mic in teamspeak.
	//VONType is how teamspeak knows what to do with our VONEntry, if its direct it just uses the data we give it to simualte 3D sound.
	//If its radio, it checks to see if we are on the frequency, if so applies the filters. If not then if distance is not -1 it plays it as a direct entry.
	//Frequency, this is how Teamspeak checks if a radio entry is heard on radio by you by comparing it to the data in RadioData.json.
	//Left gain and Right gain is how teamspeak determines how much each ear hears during a direct broadcast.
	//Volume is in meters your voice should travel to be heard at a decent volume, so 40 at 40m you can still hear the voice really well.
	//Distance, teamspeak uses this and volume to build a sound fall off curve and adjust the voice coming in.
	//ConnectionQuality, this handles how well your signal is to another radio, the lower the value the more distorted and quiet your voice is, as well as increasing the noise.
	//FactionKey, this always gets set but it just determines if encryption is enabled that we can hear the radio broadcast coming in.
	
	//Chain from broadcast is
	//You broadcast to selected players -> They receive it in their player controller and add it locally -> 
	//This WriteJson() method is being constantly called it will then write the data to the json for teamspeak to interpret ->
	//Teamspeak reads the JSON, parses the data and compares it using the clientId to see if they should hear this baffoon ->
	//The entry is removed from the local array and is no longer written to the JSON, there for teamspeak just mutes that client as he has no data in the JSON.
	//==========================================================================================================================================================================
	void WriteJSON()
	{
		if (!GetGame().GetPlayerController())
			return;
		SCR_JsonLoadContext VONLoad = new SCR_JsonLoadContext();
		if (!VONLoad.LoadFromFile("$profile:/VONServerData.json"))
		{
			SCR_JsonSaveContext VONServerData = new SCR_JsonSaveContext();
			VONServerData.StartObject("ServerData");
			VONServerData.SetMaxDecimalPlaces(3);
			VONServerData.WriteValue("InGame", true);
			VONServerData.WriteValue("TSClientID", m_PlayerController.m_iTeamSpeakClientId);
			VONServerData.WriteValue("TSPluginVersion", m_PlayerController.m_fTeamspeakPluginVersion);
			VONServerData.WriteValue("VONChannelName", m_VONGameModeComponent.m_sTeamSpeakChannelName);
			VONServerData.WriteValue("VONChannelPassword", m_VONGameModeComponent.m_sTeamSpeakChannelPassword);
			VONServerData.EndObject();
			VONServerData.SaveToFile("$profile:/VONServerData.json");
		}
		else
		{
			string ChannelName;
			string ChannelPassword;
			bool InGame;
			VONLoad.StartObject("ServerData");
			VONLoad.ReadValue("InGame", InGame);
			VONLoad.ReadValue("VONChannelName", ChannelName);
			VONLoad.ReadValue("VONChannelPassword", ChannelPassword);
			VONLoad.ReadValue("TSPluginVersion", m_PlayerController.m_fTeamspeakPluginVersion);
			VONLoad.ReadValue("TSClientID", m_PlayerController.m_iTeamSpeakClientId);
			VONLoad.EndObject();
			if (ChannelName != m_VONGameModeComponent.m_sTeamSpeakChannelName || ChannelPassword != m_VONGameModeComponent.m_sTeamSpeakChannelPassword || m_PlayerController.m_fTeamspeakPluginVersion != m_VONGameModeComponent.m_fTeamSpeakPluginVersion || InGame != true)
			{
				SCR_JsonSaveContext VONServerData = new SCR_JsonSaveContext();
				VONServerData.StartObject("ServerData");
				VONServerData.SetMaxDecimalPlaces(3);
				VONServerData.WriteValue("InGame", true);
				VONServerData.WriteValue("TSClientID", m_PlayerController.m_iTeamSpeakClientId);
				VONServerData.WriteValue("TSPluginVersion", m_PlayerController.m_fTeamspeakPluginVersion);
				VONServerData.WriteValue("VONChannelName", m_VONGameModeComponent.m_sTeamSpeakChannelName);
				VONServerData.WriteValue("VONChannelPassword", m_VONGameModeComponent.m_sTeamSpeakChannelPassword);
				VONServerData.EndObject();
				VONServerData.SaveToFile("$profile:/VONServerData.json");
			}
		}
		#ifdef WORKBENCH
		#else
		//Hijack this whole process to load the initial warning menu
		if (m_VONGameModeComponent.m_bTeamspeakChecks)
		{
			if (m_PlayerController.m_fTeamspeakPluginVersion != 0 && m_PlayerController.m_fTeamspeakPluginVersion != m_VONGameModeComponent.m_fTeamSpeakPluginVersion)
			{
				m_VONHud.ShowWarning();
			}
			if (m_PlayerController.m_iTeamSpeakClientId == 0 && !m_PlayerController.m_bHasBeenGivenInitialWarning && SCR_PlayerController.GetLocalControlledEntity())
			{
				m_PlayerController.m_bHasBeenGivenInitialWarning = true;
				GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.CVON_WarningMenu);
			}
			else if (!m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime && m_PlayerController.m_iTeamSpeakClientId != 0)
			{
				m_PlayerController.m_bHasBeenGivenInitialWarning = true;
				m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime = true;
			}
			else if (m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime && m_PlayerController.m_iTeamSpeakClientId == 0 && SCR_PlayerController.GetLocalControlledEntity())
			{
				m_VONHud.ShowWarning();
				m_bShowingSecondWarning = true;
	
			}
			else if (m_bShowingSecondWarning)
			{
				m_VONHud.HideWarning();
				m_bShowingSecondWarning = false;
			}
		}
		#endif
		SCR_JsonSaveContext VONSave = new SCR_JsonSaveContext();
		VONSave.WriteValue("IsTransmitting", m_bIsBroadcasting);
		IEntity localEntity = GetGame().GetCameraManager().CurrentCamera();
		foreach (CVON_VONContainer container: m_PlayerController.m_aLocalActiveVONEntries)
		{
			IEntity soundSource;
			float left = 0;
			float right = 0;
			int loweredDecibels = 0;
			string frequency = container.m_sFrequency;
			if (m_CharacterController.GetLifeState() == ECharacterLifeState.DEAD)
			{
				//Cuts off all incoming audio, cause we're dead.
				left = 0;
				right = 0;
				frequency = "";
			}
			else if (Replication.FindItem(container.m_SenderRplId) && !container.m_SoundSource && container.m_fDistanceToSender != -1)
			{
				soundSource = RplComponent.Cast(Replication.FindItem(container.m_SenderRplId)).GetEntity();
				container.m_SoundSource = soundSource;
				ShouldMuffleAudio(container.m_SoundSource, loweredDecibels);
				if (loweredDecibels < 0)
					ComputeStereoLR(localEntity, GetHeadHeight(soundSource), container.m_iVolume/1.25, left, right);
				else
					ComputeStereoLR(localEntity, GetHeadHeight(soundSource), container.m_iVolume, left, right);
			}
			else if (container.m_SoundSource && container.m_fDistanceToSender != -1)
			{
				ShouldMuffleAudio(container.m_SoundSource, loweredDecibels);
				if (loweredDecibels < 0)
					ComputeStereoLR(localEntity, GetHeadHeight(container.m_SoundSource), container.m_iVolume/1.25, left, right);
				else
					ComputeStereoLR(localEntity, GetHeadHeight(container.m_SoundSource), container.m_iVolume, left, right);
			}
			
			if (container.m_eVonType == CVON_EVONType.RADIO)
			{
				if (container.m_SoundSource)
				{
					container.m_fConnectionQuality = GetSignalStrength(vector.Distance(localEntity.GetOrigin(), container.m_SoundSource.GetOrigin()), container.m_iMaxDistance);
				}
				else
					container.m_fConnectionQuality = GetSignalStrength(vector.Distance(localEntity.GetOrigin(), container.m_vSenderLocation), container.m_iMaxDistance);
			}
				
				
			VONSave.StartObject(container.m_iClientId.ToString());
			VONSave.SetMaxDecimalPlaces(3);
			VONSave.WriteValue("VONType", container.m_eVonType);
			VONSave.WriteValue("Frequency", frequency);
			VONSave.WriteValue("LeftGain", left);
			VONSave.WriteValue("RightGain", right);
			VONSave.WriteValue("MuffledDecibels", loweredDecibels);
			VONSave.WriteValue("ConnectionQuality", container.m_fConnectionQuality);
			VONSave.WriteValue("FactionKey", container.m_sFactionKey);
			VONSave.EndObject();
		}
		VONSave.SaveToFile("$profile:/VONData.json");
	}
	
	
	//Resets these values so we can leave the channel on teamspeak
	//It checks if the bool InGame is true, and if so moves you to the voip channel
	//==========================================================================================================================================================================
	void ~SCR_VONController()
	{
		SCR_JsonSaveContext VONServerData = new SCR_JsonSaveContext();
		VONServerData.StartObject("ServerData");
		VONServerData.WriteValue("InGame", false);
		VONServerData.WriteValue("TSClientID", 0);
		VONServerData.WriteValue("TSPluginVersion", 0);
		VONServerData.WriteValue("VONChannelName", "");
		VONServerData.WriteValue("VONChannelPassword", "");
		VONServerData.EndObject();
		VONServerData.SaveToFile("$profile:/VONServerData.json");
	}
}