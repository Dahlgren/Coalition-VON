
class CVON_HUD: SCR_InfoDisplay {
	protected SCR_PlayerController m_PlayerController;
	protected ProgressBarWidget m_wVoiceRangeSlider;
	protected TextWidget m_wVoiceRangeText;
	protected InputManager m_InputManager;
	bool m_bIsToggled = false;

	//------------------------------------------------------------------------------------------------

	// Override/static functions

	//------------------------------------------------------------------------------------------------
	
	override event void OnStartDraw(IEntity owner)
	{
		super.OnStartDraw(owner);
		GetGame().GetInputManager().AddActionListener("CVON_ShowVoiceRangeSlider", EActionTrigger.PRESSED, ShowVoiceRangeSlider);
		GetGame().GetInputManager().AddActionListener("CVON_ShowVoiceRangeSlider", EActionTrigger.UP, HideVoiceRangeSlider);
		GetGame().GetInputManager().AddActionListener("VONChannel", EActionTrigger.DOWN, ShowVON);
		GetGame().GetInputManager().AddActionListener("VONLongRange", EActionTrigger.DOWN, ShowVON);
		GetGame().GetInputManager().AddActionListener("VONMediumRange", EActionTrigger.DOWN, ShowVON);
		GetGame().GetInputManager().AddActionListener("VONDirect", EActionTrigger.DOWN, ShowDirect);
		GetGame().GetInputManager().AddActionListener("VONRotateActive", EActionTrigger.DOWN, ShowVONActive);
		GetGame().GetInputManager().AddActionListener("VONRotateActive", EActionTrigger.DOWN, HideVON);
	}
	
	//------------------------------------------------------------------------------------------------
	override event void OnStopDraw(IEntity owner)
	{
		super.OnStopDraw(owner);
		GetGame().GetInputManager().RemoveActionListener("CVON_ShowVoiceRangeSlider", EActionTrigger.PRESSED, ShowVoiceRangeSlider);
		GetGame().GetInputManager().RemoveActionListener("CVON_ShowVoiceRangeSlider", EActionTrigger.UP, HideVoiceRangeSlider);
		GetGame().GetInputManager().RemoveActionListener("VONChannel", EActionTrigger.DOWN, ShowVON);
		GetGame().GetInputManager().RemoveActionListener("VONChannel", EActionTrigger.UP, HideVON);
		GetGame().GetInputManager().RemoveActionListener("VONLongRange", EActionTrigger.DOWN, ShowVON);
		GetGame().GetInputManager().RemoveActionListener("VONLongRange", EActionTrigger.UP, HideVON);
		GetGame().GetInputManager().RemoveActionListener("VONMediumRange", EActionTrigger.DOWN, ShowVON);
		GetGame().GetInputManager().RemoveActionListener("VONMediumRange", EActionTrigger.UP, HideVON);
		GetGame().GetInputManager().RemoveActionListener("VONDirect", EActionTrigger.DOWN, ShowDirect);
		GetGame().GetInputManager().RemoveActionListener("VONDirect", EActionTrigger.UP, HideDirect);
		GetGame().GetInputManager().RemoveActionListener("VONRotateActive", EActionTrigger.DOWN, ShowVONActive);
		GetGame().GetInputManager().RemoveActionListener("VONRotateActive", EActionTrigger.DOWN, HideVON);
	}
	
	//------------------------------------------------------------------------------------------------

	// UI functions

	//------------------------------------------------------------------------------------------------
	
	void ShowDirect()
	{
		if (m_bIsToggled)
			return;
		m_wRoot.FindAnyWidget("Mic").SetOpacity(1);
		m_wRoot.FindAnyWidget("Mic").SetVisible(true);
	}
	
	void ShowDirectToggle()
	{
		GetGame().GetCallqueue().CallLater(DirectToggleDelay, 200, false);
	}
	
	void DirectToggleDelay()
	{
		m_bIsToggled = !m_bIsToggled;
		if (m_bIsToggled)
		{
			m_wRoot.FindAnyWidget("Mic").SetOpacity(1);
			m_wRoot.FindAnyWidget("Mic").SetVisible(true);
			AnimateWidget.StopAnimation(m_wRoot.FindAnyWidget("Mic"), WidgetAnimationOpacity);
		}
		else
			HideDirect();
	}
	
	void HideDirect()
	{
		if (m_bIsToggled)
			return;
		Widget micWidget = m_wRoot.FindAnyWidget("Mic");
		micWidget.SetOpacity(1);
		AnimateWidget.Opacity(micWidget, 0, 1/0.3);
	}
	
	void ShowVONActive()
	{
		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (playerController.m_aRadios.Count() < 2)
			return;
		IEntity radio = playerController.m_aRadios.Get(0);
		if (!radio)
			return;
		m_wRoot.FindAnyWidget("VONEntry").SetOpacity(1);
		m_wRoot.FindAnyWidget("VONEntry").SetVisible(true);
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		TextWidget.Cast(m_wRoot.FindAnyWidget("Radio")).SetText(radioComp.m_sRadioName);
		TextWidget.Cast(m_wRoot.FindAnyWidget("Freq")).SetText("CH " + radioComp.m_iCurrentChannel + " | " + radioComp.m_sFrequency);
	}
	
	void ShowVON()
	{
		SCR_VONController vonController = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
		if (!vonController)
			return;
		
		if (!vonController.m_CurrentVONContainer)
			return;
		
		if (!Replication.FindItem(vonController.m_CurrentVONContainer.m_iRadioId))
			return;
		
		IEntity radio = RplComponent.Cast(Replication.FindItem(vonController.m_CurrentVONContainer.m_iRadioId)).GetEntity();
		if (!radio)
			return;
		m_wRoot.FindAnyWidget("VONEntry").SetOpacity(1);
		m_wRoot.FindAnyWidget("VONEntry").SetVisible(true);
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		TextWidget.Cast(m_wRoot.FindAnyWidget("Radio")).SetText(radioComp.m_sRadioName);
		TextWidget.Cast(m_wRoot.FindAnyWidget("Freq")).SetText("CH " + radioComp.m_iCurrentChannel + " | " + radioComp.m_sFrequency);
	}
	
	void HideVON()
	{
		Widget vonWidget = m_wRoot.FindAnyWidget("VONEntry");
		vonWidget.SetOpacity(1);
		AnimateWidget.Opacity(vonWidget, 0, 1/0.3);
	}
	
	protected void ShowVoiceRangeSlider()
	{
		if (!m_InputManager || !m_wVoiceRangeSlider || !m_wVoiceRangeText) 
		{
			m_InputManager = GetGame().GetInputManager();
			m_wVoiceRangeSlider = ProgressBarWidget.Cast(m_wRoot.FindWidget("VoiceRangeSlider"));
			m_wVoiceRangeText = TextWidget.Cast(m_wRoot.FindWidget("VoiceRangeText"));
		};
		
		m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		
		float currentSliderOpacity = m_wVoiceRangeSlider.GetOpacity();
		float currentTextOpacity = m_wVoiceRangeText.GetOpacity();
		
		if (currentSliderOpacity < 0.65 && currentTextOpacity < 0.65) {
			m_wVoiceRangeSlider.SetOpacity(currentSliderOpacity + 0.025);
			m_wVoiceRangeText.SetOpacity(currentTextOpacity + 0.025);
		};
		
		int actionValueUp = m_InputManager.GetActionValue("CVON_VoiceRangeUp");
		int actionValueDown = m_InputManager.GetActionValue("CVON_VoiceRangeDown");
		
		if (actionValueUp != 0 || actionValueDown != 0) {
			m_PlayerController.ChangeVoiceRange(actionValueUp + actionValueDown);
		};
		
		m_wVoiceRangeSlider.SetCurrent(m_PlayerController.ReturnLocalVoiceRange() + 1);
		
		// Color
		switch (m_PlayerController.ReturnLocalVoiceRange())
		{
			case 0: 
			{ 
				m_wVoiceRangeSlider.SetColor(Color.SpringGreen);
				m_wVoiceRangeText.SetText("Whisper"); 
				m_wVoiceRangeText.SetColor(Color.SpringGreen);
				break; 
			};
			case 1:
			{
				m_wVoiceRangeSlider.SetColor(Color.Green); 
				m_wVoiceRangeText.SetText("Close Contact"); 
				m_wVoiceRangeText.SetColor(Color.Green);
				break; 
			};
			case 2:
			{
				m_wVoiceRangeSlider.SetColor(Color.Yellow); 
				m_wVoiceRangeText.SetText("Normal"); 
				m_wVoiceRangeText.SetColor(Color.Yellow);
				break; 
			};
			case 3:
			{
				m_wVoiceRangeSlider.SetColor(Color.Red); 
				m_wVoiceRangeText.SetText("Yelling"); 
				m_wVoiceRangeText.SetColor(Color.Red);
				break; 
			};
			case 4:
			{
				m_wVoiceRangeSlider.SetColor(Color.DarkRed);
				m_wVoiceRangeText.SetText("May I Speak To Your Manager");  
				m_wVoiceRangeText.SetColor(Color.DarkRed);
				break; 
			}
			default: { m_wVoiceRangeSlider.SetColor(Color.Yellow); m_wVoiceRangeText.SetColor(Color.Yellow); break; };
		};
	};
	
	//------------------------------------------------------------------------------------------------
	protected void HideVoiceRangeSlider()
	{
		GetGame().GetCallqueue().CallLater(HideBar, 0, true);
	};


	//------------------------------------------------------------------------------------------------
	protected void HideBar()
	{
		float currentSliderOpacity = m_wVoiceRangeSlider.GetOpacity();
		float currentTextOpacity = m_wVoiceRangeText.GetOpacity();
		
		if (currentSliderOpacity <= 0 && currentTextOpacity <= 0) {
			GetGame().GetCallqueue().Remove(HideBar);
			return;
		};
		
		m_wVoiceRangeSlider.SetOpacity(currentSliderOpacity - 0.025);
		m_wVoiceRangeText.SetOpacity(currentTextOpacity - 0.025);
	}
};