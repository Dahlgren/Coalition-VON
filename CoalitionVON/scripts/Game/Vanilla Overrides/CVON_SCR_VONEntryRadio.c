modded class SCR_VONEntryRadio
{
	string m_sRadioFrequency;
	string m_sRadioName;
	bool m_bIsNotLocal;
	void SetRadioFrequency(string freq)
	{
		m_sRadioFrequency = freq;
	}
	override void InitEntry()
	{
		SetCustomLayout("{033302D7C8158EF8}UI/layouts/HUD/VON/VONEntry.layout");
		if (m_bIsNotLocal)
		{
			SetUsable(true);
			AdjustEntryModif(0);	
			SetChannelText("55500");
			return;
		}
		SetUsable(m_RadioTransceiver.GetRadio().IsPowered());
		
		AdjustEntryModif(0);
				
		SetChannelText(SCR_VONMenu.GetKnownChannel(m_RadioTransceiver.GetFrequency()));
	}
	//Ripped code straight from vanilla, just modified it so its a different look when you open the radial menu and so it supports our radios.
	//==========================================================================================================================================================================
	override void Update()
	{
		super.Update();
				
		SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);			
		if (!entryComp)	// first update procs when this is not fetchable yet
			return;
		
		if (m_bIsNotLocal)
		{
			entryComp.SetFrequencyText(m_sRadioFrequency);
			TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetText(m_sRadioName);
			TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetColor(Color.DarkGreen);
			entryComp.SetTransceiverOpacity(1);
			
			entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
			return;
		}
		
		if (!m_RadioTransceiver)	// TODO can happen for unclear reasons, temp fix
		{
			SCR_VONController vonContr = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
			if (vonContr)
				vonContr.RemoveEntry(this);
			
			return;
		}
		
		BaseRadioComponent radio = m_RadioTransceiver.GetRadio();
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.GetOwner().FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		entryComp.SetTransceiverText("CH" + m_iTransceiverNumber.ToString());
		entryComp.SetFrequencyText(radioComp.m_sFrequency);
		entryComp.SetChannelText("CH" + radioComp.m_iCurrentChannel);
		TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetText(radioComp.m_sRadioName);
		TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetColor(Color.DarkGreen);
		entryComp.SetActiveIcon(m_bIsActive);	
		
		
		SetUsable(radio.IsPowered());	
		entryComp.SetPowerIcon(IsUsable());
		entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
	}
}