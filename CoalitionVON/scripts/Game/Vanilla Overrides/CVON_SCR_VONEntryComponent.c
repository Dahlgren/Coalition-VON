//------------------------------------------------------------------------------------------------
// Attached to layout used for radial menu VON entries
modded class SCR_VONEntryComponent
{
	void UpdateCVONRadioWidget(CVON_RadioComponent radioComp, bool showWindow)
	{
		CVON_HUD.UpdateCVONRadioWidget(radioComp, showWindow, m_wRoot);
	}
};