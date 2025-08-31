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
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.GetOwner().FindComponent(CVON_RadioComponent));
		
		if (!radioComp)
			return;
		
		entryComp.UpdateCVONRadioWidget(radioComp, false);
	}
}