modded enum ChimeraMenuPreset
{
	CVON_WarningMenu
}

class CVON_WarningMenu: ChimeraMenuBase
{
	const string LINK_TO_COPY = "https://discord.gg/the-coalition";
	SCR_ButtonTextComponent m_wCopyButton;
	Widget m_wRoot;
	SCR_PlayerController m_PlayerController;
	CVON_VONGameModeComponent m_VONGamemode;
	TextWidget m_wExplanation;
	
	override void OnMenuOpen()
	{
		m_wRoot = GetRootWidget();
		m_wCopyButton = SCR_ButtonTextComponent.Cast(m_wRoot.FindAnyWidget("CopyButton").FindHandler(SCR_ButtonTextComponent));
		m_wCopyButton.m_OnClicked.Insert(CopyToClipboard);
		m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		m_wExplanation = TextWidget.Cast(m_wRoot.FindAnyWidget("Explanation"));
		m_VONGamemode = CVON_VONGameModeComponent.GetInstance();
	}
	
	override void OnMenuUpdate(float tDelta)
	{
		if (m_PlayerController.m_iTeamSpeakClientId == 0)
		{
			m_wExplanation.SetText("Teamspeak Plugin Not Detected!");
		}
		else if (m_PlayerController.m_fTeamspeakPluginVersion != m_VONGamemode.m_fTeamSpeakPluginVersion)
		{
			m_wExplanation.SetText("Teamspeak Plugin Version Mismatch!");
		}
		if (m_PlayerController.m_iTeamSpeakClientId != 0 && m_PlayerController.m_fTeamspeakPluginVersion == m_VONGamemode.m_fTeamSpeakPluginVersion)
		{
			m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime = true;
			Close();
		}
	}
	
	void CopyToClipboard()
	{
		System.ExportToClipboard(LINK_TO_COPY);
	}
}