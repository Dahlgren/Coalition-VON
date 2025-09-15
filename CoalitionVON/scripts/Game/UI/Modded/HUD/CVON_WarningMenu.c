modded enum ChimeraMenuPreset
{
	CVON_WarningMenu
}

class CVON_WarningMenu: ChimeraMenuBase
{
	const string LINK_TO_COPY = "https://github.com/CoalitionArma/Coalition-VON/releases/tag/Release";
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
		if (m_PlayerController.GetTeamspeakClientId() == 0)
		{
			m_wExplanation.SetText("Teamspeak Plugin Not Detected!");
		}
		else if (m_PlayerController.m_sTeamspeakPluginVersion != m_VONGamemode.m_sTeamspeakPluginVersion)
		{
			m_wExplanation.SetText("Teamspeak Plugin Version Mismatch!");
		}
		if (m_PlayerController.GetTeamspeakClientId() != 0 && m_PlayerController.m_sTeamspeakPluginVersion == m_VONGamemode.m_sTeamspeakPluginVersion)
		{
			m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime = true;
			Close();
		}
	}
	
	override void OnMenuClose()
	{
		if (m_PlayerController.GetTeamspeakClientId() == 0 && m_PlayerController.m_sTeamspeakPluginVersion != m_VONGamemode.m_sTeamspeakPluginVersion)
			GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.CVON_WarningMenu);
	}
	
	void CopyToClipboard()
	{
		System.ExportToClipboard(LINK_TO_COPY);
	}
}