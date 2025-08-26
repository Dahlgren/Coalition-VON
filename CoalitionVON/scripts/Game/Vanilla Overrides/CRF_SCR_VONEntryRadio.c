modded class SCR_VONEntryRadio
{
	
	//Ripped code straight from vanilla, just modified it so its a different look when you open the radial menu and so it supports our radios.
	//==========================================================================================================================================================================
	override void Update()
	{
		super.Update();
				
		SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);			
		if (!entryComp)	// first update procs when this is not fetchable yet
			return;
		
		if (!m_RadioTransceiver)	// TODO can happen for unclear reasons, temp fix
		{
			SCR_VONController vonContr = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
			if (vonContr)
				vonContr.RemoveEntry(this);
			
			return;
		}
		
		BaseRadioComponent radio = m_RadioTransceiver.GetRadio();
		CRF_RadioComponent radioComp = CRF_RadioComponent.Cast(radio.GetOwner().FindComponent(CRF_RadioComponent));
		entryComp.SetTransceiverText("CH" + m_iTransceiverNumber.ToString());
		entryComp.SetFrequencyText(radioComp.m_sFrequency);
		entryComp.SetChannelText("CH" + radioComp.m_iCurrentChannel);
		TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetText(radioComp.m_sRadioName);
		TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetColor(Color.DarkGreen);
		entryComp.SetActiveIcon(m_bIsActive);
		
		
		SetUsable(radio.IsPowered());	
		entryComp.SetPowerIcon(IsUsable());
		
		if (m_bIsActive)
		{
			entryComp.SetTransceiverOpacity(1);
			
			if (m_bIsSelected)
				entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
			else 
				entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
		}
		else 
		{
			entryComp.SetTransceiverOpacity(0.5);
			
			if (m_bIsSelected)
				entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
			else 
				entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
		}
	}
}